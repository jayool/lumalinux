#pragma once

#include "gmrc_xref_core.hpp"   // Region

#include <cstddef>
#include <cstdint>
#include <cstring>

// Pure, address-space-agnostic locator for CUser::NotifyLicensesUpdated — the
// "Reconcile" function (src/license_reconcile.cpp). Shared by the runtime
// rescue (src/reconcile_anchor.cpp, LOADED memory) and the offline self-test
// (tools/reconcile_anchor_selftest.cpp, the mmap'd file). Port of
// tools/derive_reconcile_byanchor.py, the CI locator that derives the byte
// pattern; the two must keep giving the same answer (tools/test_reconcile_anchor.py
// runs both on the same synthetic bytes).
//
// Why an anchor at all: the function has no string, no RTTI reference and no
// vtable slot — it posts the LicensesUpdated_t callback by NUMBER. So the number
// is the anchor. It is not a guess: LicensesUpdated_t::k_iCallback is
// k_iSteamUserCallbacks + 25 = 125 in the public Steamworks SDK, compiled into
// every game that listens for it, which is why Valve keeps it. (The private
// layout numbers — the member offset the function reads, its frame size — are
// exactly what moved between bc54101b and 9cf4720f and are NOT criteria.)
//
// Criteria, measured on both builds (tools/experiment_reconcile_xref.py):
//   c1  a `push 0x7d ; push eax ; call rel32` site  (6A 7D 50 E8)
//       — 11 functions per build post that callback; c1 names candidates only
//   c2  the function's head reads a field of `this` first:
//       mov eax,[ebp+8] ; mov r,[eax+disp32]           (8B 45 08 8B /r, mod=10 rm=eax)
//       disp32 FREE
//   c3  and bails when it is <= 0 within 16 bytes:   test r,r ; jle   (85 ?? 0F 8E)
// c1+c2+c3 keeps exactly ONE function on both builds (0x188c950 / 0x1a0d010).
// Zero or two -> 0, never a guess: the caller would detour the answer.
//
// The function containing a c1 site is the CALLER's business (FnOf): at runtime
// .eh_frame_hdr (src/eh_frame.hpp), offline the same table parsed from the file.
namespace ReconcileAnchorCore {

using GmrcXrefCore::Region;

// LicensesUpdated_t::k_iCallback (Steamworks SDK: k_iSteamUserCallbacks + 25).
constexpr uint8_t kLicensesUpdatedCallback = 125;
// push imm8 (0x7d) ; push eax ; call rel32 — c1.
constexpr uint8_t kCallbackPost[4] = { 0x6A, kLicensesUpdatedCallback, 0x50, 0xE8 };

constexpr std::size_t kHeadBytes  = 96;   // how far into the function c2/c3 may sit
constexpr std::size_t kJleWindow  = 16;   // bytes after the this-read for test+jle
constexpr std::size_t kMaxSites   = 64;   // c1 sites per module (11 seen); more -> refuse

struct Info {
    std::size_t sites = 0;          // c1 sites found
    std::size_t candidates = 0;     // distinct functions containing one
    std::size_t kept = 0;           // that also pass c2+c3
    uintptr_t   first = 0, second = 0;   // the kept ones (for the log when ambiguous)
    uint32_t    memberDisp = 0;     // informational only, never a criterion
};

// c2 + c3 on the first bytes of a function. `disp` receives the member
// displacement (informational). Mirrors derive_reconcile_byanchor.reads_this_then_bails:
// the FIRST c2 occurrence decides; a later one is not tried.
inline bool ReadsThisThenBails(const uint8_t* head, std::size_t len, uint32_t* disp) {
    if (len < 10) return false;
    for (std::size_t k = 0; k + 9 < len; ++k) {
        if (head[k] == 0x8B && head[k + 1] == 0x45 && head[k + 2] == 0x08 &&
            head[k + 3] == 0x8B && (head[k + 4] & 0xC0) == 0x80 && (head[k + 4] & 0x07) == 0) {
            uint32_t d = 0;
            std::memcpy(&d, head + k + 5, 4);
            if (disp) *disp = d;
            const std::size_t w0 = k + 9;
            const std::size_t w1 = (w0 + kJleWindow < len) ? w0 + kJleWindow : len;
            for (std::size_t m = w0; m + 3 < w1; ++m) {
                if (head[m] == 0x85 && head[m + 2] == 0x0F && head[m + 3] == 0x8E) return true;
            }
            return false;
        }
    }
    return false;
}

// Every c1 site in `rx`, as address-space addresses. Returns how many exist;
// writes at most `maxOut`.
inline std::size_t FindCallbackPostSites(Region rx, uintptr_t* out, std::size_t maxOut) {
    std::size_t n = 0;
    if (!rx || rx.size < sizeof(kCallbackPost)) return 0;
    for (std::size_t i = 0; i + sizeof(kCallbackPost) <= rx.size; ++i) {
        if (rx.p[i] == kCallbackPost[0] && std::memcmp(rx.p + i, kCallbackPost, sizeof(kCallbackPost)) == 0) {
            if (n < maxOut) out[n] = rx.addr + i;
            ++n;
        }
    }
    return n;
}

// The locator. `fnOf(site)` returns the entry of the function containing
// `site`, or 0 if unknown. Returns the ONE function passing c1+c2+c3, else 0.
template <class FnOf>
inline uintptr_t LocateNotifyLicensesUpdated(Region rx, FnOf fnOf, Info* info = nullptr) {
    Info local;
    Info& I = info ? *info : local;
    I = Info{};
    if (!rx) return 0;

    uintptr_t sites[kMaxSites];
    const std::size_t total = FindCallbackPostSites(rx, sites, kMaxSites);
    I.sites = total;
    if (total == 0 || total > kMaxSites) return 0;     // none, or not the module we know

    // Distinct containing functions, in site order (small N: linear dedupe).
    uintptr_t fns[kMaxSites];
    std::size_t nf = 0;
    for (std::size_t i = 0; i < total; ++i) {
        const uintptr_t f = fnOf(sites[i]);
        if (!f) continue;
        bool seen = false;
        for (std::size_t j = 0; j < nf; ++j) if (fns[j] == f) { seen = true; break; }
        if (!seen) fns[nf++] = f;
    }
    I.candidates = nf;

    uintptr_t keep = 0;
    for (std::size_t i = 0; i < nf; ++i) {
        const uintptr_t f = fns[i];
        if (f < rx.addr || f >= rx.addr + rx.size) continue;
        const std::size_t off = static_cast<std::size_t>(f - rx.addr);
        const std::size_t len = (rx.size - off < kHeadBytes) ? rx.size - off : kHeadBytes;
        uint32_t disp = 0;
        if (!ReadsThisThenBails(rx.p + off, len, &disp)) continue;
        ++I.kept;
        if (I.kept == 1) { keep = f; I.first = f; I.memberDisp = disp; }
        else if (I.kept == 2) { I.second = f; }
    }
    return (I.kept == 1) ? keep : 0;
}

} // namespace ReconcileAnchorCore
