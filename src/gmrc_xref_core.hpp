#pragma once

// Pure, address-space-agnostic scanners for the GMRC job-name xref locator.
// Shared by the runtime locator (src/gmrc_xref.cpp, operating on LOADED memory
// via /proc/self/maps) and the offline self-test (tools/gmrc_xref_selftest.cpp,
// operating on the mmap'd file via ELF program headers). Both feed Regions whose
// `addr` is the address-space value of `p[0]` (loaded address at runtime, file
// vaddr offline); every returned/accepted address is in that same space, so the
// arithmetic (disp = S - GOT is a link-time constant) is identical either way.
//
// Header-only + inline so the self-test links nothing from the project. See
// gmrc_xref.hpp for the algorithm and its ground-truth validation.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace GmrcXrefCore {

struct Region {
    const uint8_t* p = nullptr;   // readable bytes
    std::size_t size = 0;
    uintptr_t addr = 0;           // address-space value corresponding to p[0]
    explicit operator bool() const { return p != nullptr && size != 0; }
};

// First occurrence of `needle` in `r`, as an address-space address, or 0.
inline uintptr_t FindBytes(Region r, const void* needle, std::size_t nlen) {
    if (!r || nlen == 0 || r.size < nlen) return 0;
    const uint8_t* nd = reinterpret_cast<const uint8_t*>(needle);
    const std::size_t span = r.size - nlen;
    for (std::size_t i = 0; i <= span; ++i) {
        if (r.p[i] == nd[0] && std::memcmp(r.p + i, nd, nlen) == 0) {
            return r.addr + i;
        }
    }
    return 0;
}

// Module GOT base by consensus over PIC preambles: `E8 rel32` (call get_pc_thunk)
// immediately followed by `add reg,imm32` (05 imm32 = eax, or 81 /0 = 81 C? for
// the rest). GOT = (addr after the call) + imm32 = _GLOBAL_OFFSET_TABLE_, a single
// module-wide value, so the correct answer dominates the vote.
inline uintptr_t DeriveGotBaseConsensus(Region rx) {
    if (!rx || rx.size < 11) return 0;
    const uint8_t* p = rx.p;
    const std::size_t n = rx.size - 11;
    std::unordered_map<uintptr_t, uint32_t> votes;
    for (std::size_t i = 0; i <= n; ++i) {
        if (p[i] != 0xE8) continue;
        const uint8_t op = p[i + 5];
        const uintptr_t ret = rx.addr + i + 5;
        int32_t imm;
        if (op == 0x05) {
            std::memcpy(&imm, p + i + 6, 4);
        } else if (op == 0x81 && (p[i + 6] & 0xF8) == 0xC0) {
            std::memcpy(&imm, p + i + 7, 4);
        } else {
            continue;
        }
        ++votes[ret + static_cast<uintptr_t>(static_cast<intptr_t>(imm))];
    }
    uintptr_t best = 0;
    uint32_t bestCount = 0;
    for (const auto& kv : votes) {
        if (kv.second > bestCount) { bestCount = kv.second; best = kv.first; }
    }
    return best;
}

// The UNIQUE `lea reg,[base + disp32]` (8D, mod=10, base != SIB) with disp32 ==
// disp, as an address-space address; 0 on zero OR multiple matches (fail-closed).
inline uintptr_t ScanLeaUnique(Region rx, int32_t disp) {
    if (!rx || rx.size < 6) return 0;
    const uint8_t* p = rx.p;
    const std::size_t n = rx.size - 6;
    uintptr_t hit = 0;
    uint32_t count = 0;
    for (std::size_t i = 0; i <= n; ++i) {
        if (p[i] != 0x8D) continue;
        const uint8_t m = p[i + 1];
        if ((m & 0xC0) != 0x80 || (m & 0x07) == 0x04) continue;
        int32_t d;
        std::memcpy(&d, p + i + 2, 4);
        if (d != disp) continue;
        if (++count > 1) return 0;
        hit = rx.addr + i;
    }
    return count == 1 ? hit : 0;
}

// Nearest preceding PIC prologue (`E8 rel32; add reg,imm32`) = function entry.
inline uintptr_t WalkBackToPrologue(Region rx, uintptr_t site,
                                    std::size_t maxBack = 0x8000) {
    if (!rx || site < rx.addr) return 0;
    const std::size_t off = static_cast<std::size_t>(site - rx.addr);
    if (off >= rx.size) return 0;
    const uint8_t* p = rx.p;
    const std::size_t lo = (off > maxBack) ? off - maxBack : 0;
    for (std::size_t k = off; ; --k) {
        if (k + 6 < rx.size && p[k] == 0xE8) {
            const uint8_t nb = p[k + 5];
            if (nb == 0x05 || (nb == 0x81 && (p[k + 6] & 0xF8) == 0xC0)) {
                return rx.addr + k;
            }
        }
        if (k == lo) break;
    }
    return 0;
}

// Full derivation given the executable region and the resolved string address.
// Optional out-params expose intermediates for logging / tests. Returns the
// getter's entry address, or 0 on any miss/ambiguity.
inline uintptr_t DeriveGmrcEntry(Region rx, uintptr_t strAddr,
                                 uintptr_t* outGot = nullptr,
                                 uintptr_t* outSite = nullptr) {
    const uintptr_t got = DeriveGotBaseConsensus(rx);
    if (outGot) *outGot = got;
    if (!got) return 0;
    const int32_t disp = static_cast<int32_t>(static_cast<intptr_t>(strAddr - got));
    const uintptr_t site = ScanLeaUnique(rx, disp);
    if (outSite) *outSite = site;
    if (!site) return 0;
    return WalkBackToPrologue(rx, site);
}

} // namespace GmrcXrefCore
