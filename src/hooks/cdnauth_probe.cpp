// cdnauth_probe.cpp — EXPERIMENT (2026-09-09). Is GetCDNAuthToken ownership-gated?
//
// GetManifestRequestCode is measured ownership-gated server-side: our fully
// spoofed session is denied for paid unowned content. GetCDNAuthToken is the
// OTHER content-authorization RPC in ContentServerDirectory — the legacy
// per-depot CDN token. It has never been probed. If Valve issues a token for an
// unowned paid depot, and some CDN accepts the token-query manifest URL, that is
// a path around the request code.
//
// Signature (from Ghidra decompile of FUN_013624c0 on build 1788652215, and the
// GMRC-parallel `*(req+0x18) = app_id`; the cache key "%d\CDN\%s\token" fixes
// depot=%d, host=%s):
//     bool BYieldingGetCDNAuthToken(void* this, uint32 app_id, uint32 depot_id,
//                                   char* host_name, CUtlString* out)
//     return true  -> granted, *out holds the token
//     return false -> denied (the Access Denied branch)
//
// BUILD-PINNED: the target RVA (0x13524c0) was read from THIS build's
// steamclient.so. This is a throwaway experiment, not production — on any other
// build the RVA is wrong and Install() bails on the sanity check rather than
// hooking a random address.
//
// Armed by env LUMA_CDNAUTH_PROBE=1 AND the marker file
// ~/.config/lumalinux/cdnauth_probe, whose lines are:
//     case=<app_id>:<depot_id>       one probe target per line
//     host=<cdn hostname>            optional; defaults to a steamcontent host
// Runs the matrix ONCE, on the first GetCDNAuthToken call after arming, then
// gets out of the way (always passes through to the real function).
//
// Risk: we call the real function with an `out` we allocate. GetCDNAuthToken
// writes the token into it via a CUtlString-style assign (frees *out first).
// A zeroed buffer reads as an empty CUtlString {char*=null}, which the assign
// frees safely (free(NULL)). This holds for CUtlString; if Steam ever changes
// `out` to a libstdc++ std::string this can fault — acceptable on a codespace,
// never shipped (gated off by default, build-pinned).
#include "cdnauth_probe.hpp"
#include "../vaddr_xlate.hpp"
#include "../globals.hpp"
#include "../lmhook.hpp"
#include "../log.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace Hooks::CdnAuthProbe {
namespace {

// file vaddr of BYieldingGetCDNAuthToken on build 1788652215 (sha 237495b4).
constexpr uintptr_t kCdnAuthRva = 0x13524c0;

using CdnFn = bool (*)(void*, uint32_t, uint32_t, char*, void*);
CdnFn g_origFn = nullptr;
std::atomic<bool> g_done{false};

// A CUtlString is a single pointer on i386; a zeroed 64-byte buffer is a valid
// "empty" out that the callee's assign can overwrite without freeing garbage.
struct OutBuf { uint8_t bytes[64]; };

std::string markerPath() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.config/lumalinux/cdnauth_probe" : std::string();
}

// Armed by the marker FILE, not an env var: the Steam runtime re-execs the
// client through a filtered environment and drops LUMA_* (measured 2026-09-09),
// so an env-var gate never reaches the process that loads us. The file does.
bool probeArmed() {
    const std::string mk = markerPath();
    if (mk.empty()) return false;
    return std::ifstream(mk).good();
}

// Best-effort: read the token as a C string if the first pointer in the out
// buffer looks like a readable heap pointer. Never dereferences a wild pointer
// blindly — checks the range and caps the length.
// Try to read a printable C string at `p` (guarded). Returns len, fills buf.
std::size_t tryReadStr(uintptr_t p, char* buf, std::size_t cap) {
    if (p < 0x1000 || p > 0xfffff000) return 0;
    const char* s = reinterpret_cast<const char*>(p);
    std::size_t n = 0;
    for (; n < cap - 1; ++n) {
        char c = s[n];
        if (c == '\0') break;
        if (c < 0x20 || c > 0x7e) return 0;   // not a clean string
        buf[n] = c;
    }
    buf[n] = '\0';
    return n;
}

void logToken(const char* label, const OutBuf& out, bool rc) {
    // Hexdump the first 32 bytes of the out struct so we can locate the token
    // regardless of the exact CUtlString/std::string layout.
    char hex[32 * 3 + 1]; int hp = 0;
    for (int i = 0; i < 32; ++i) hp += std::snprintf(hex + hp, sizeof(hex) - hp, "%02x ", out.bytes[i]);
    Log::Info("CDNAUTH_PROBE:   %-20s rc=%d  out[0..32]= %s", label, (int)rc, hex);

    // Token candidates: (a) inline at offset 0 (SSO / CUtlString small), and
    // (b) pointer at each of the first 4 dwords dereferenced as char*.
    char tok[96];
    if (std::size_t n = tryReadStr(reinterpret_cast<uintptr_t>(out.bytes), tok, sizeof(tok))) {
        Log::Info("CDNAUTH_PROBE:     token(inline)='%s'", tok);
    }
    for (int off = 0; off <= 12; off += 4) {
        uintptr_t p = 0; std::memcpy(&p, out.bytes + off, 4);
        if (std::size_t n = tryReadStr(p, tok, sizeof(tok)))
            Log::Info("CDNAUTH_PROBE:     token(*+0x%x)='%s'", off, tok);
    }
}

void runMatrix(void* this_, char* realHost) {
    std::ifstream mf(markerPath());

    struct Case { uint32_t app; uint32_t depot; };
    std::vector<Case> cases;
    std::vector<std::string> hosts;
    for (std::string line; std::getline(mf, line); ) {
        if (line.rfind("host=", 0) == 0) { if (line.size() > 5) hosts.push_back(line.substr(5)); continue; }
        if (line.rfind("case=", 0) != 0) continue;
        unsigned long a = 0, d = 0;
        if (std::sscanf(line.c_str() + 5, "%lu:%lu", &a, &d) == 2)
            cases.push_back({ (uint32_t)a, (uint32_t)d });
    }
    // Always include the host Steam really used, plus whatever the marker lists.
    if (realHost && *realHost) hosts.insert(hosts.begin(), std::string(realHost));
    if (hosts.empty()) hosts.push_back("cache1-par1.steamcontent.com");

    Log::Info("CDNAUTH_PROBE: start — %zu host(s) x %zu case(s); this=%p",
              hosts.size(), cases.size(), this_);

    // Iterate host x case: we're hunting a host that returns a NON-empty token.
    for (const auto& h : hosts) {
        std::vector<char> hostbuf(h.begin(), h.end());
        hostbuf.push_back('\0');
        for (const auto& c : cases) {
            OutBuf out; std::memset(&out, 0, sizeof(out));
            bool rc = g_origFn(this_, c.app, c.depot, hostbuf.data(), &out);
            char label[128];
            std::snprintf(label, sizeof(label), "host=%s app=%u depot=%u",
                          h.c_str(), c.app, c.depot);
            logToken(label, out, rc);
        }
    }
    Log::Info("CDNAUTH_PROBE: end");
}

bool HookFn(void* this_, uint32_t app_id, uint32_t depot_id, char* host_name, void* out) {
    if (!g_done.load(std::memory_order_relaxed) && probeArmed()) {
        std::ifstream mf(markerPath());
        if (mf.good() && !g_done.exchange(true)) {
            Log::Info("CDNAUTH_PROBE: intercepted real call app=%u depot=%u host='%s'",
                      app_id, depot_id, host_name ? host_name : "(null)");
            runMatrix(this_, host_name);
        }
    }
    return g_origFn(this_, app_id, depot_id, host_name, out);
}

} // namespace

bool Install() {
    if (!probeArmed()) return false;   // never touch anything unless explicitly armed

    // VaddrXlate must be Init'd before ToRuntime — RvaFeed does this internally,
    // but this probe resolves the RVA directly so it has to init it too. Path is
    // the loaded steamclient.so, populated by InstallHooks() via LM_FindModule.
    const char* scPath = g_modSteamClient.path;
    if (!scPath || !*scPath || !VaddrXlate::Init(scPath)) {
        Log::Warn("CDNAUTH_PROBE: VaddrXlate::Init failed (path=%s) — not hooking",
                  scPath ? scPath : "(null)");
        return false;
    }
    const uintptr_t target = VaddrXlate::ToRuntime(kCdnAuthRva);
    if (!target) {
        Log::Warn("CDNAUTH_PROBE: RVA 0x%lx did not translate — wrong build? not hooking",
                  (unsigned long)kCdnAuthRva);
        return false;
    }
    // Sanity: an i386 function prologue almost always starts with a push/endbr/
    // sub-esp. If the byte at target is 0x00 the RVA is stale for this build.
    const uint8_t first = *reinterpret_cast<const uint8_t*>(target);
    if (first == 0x00) {
        Log::Warn("CDNAUTH_PROBE: byte at 0x%lx is 0x00 — RVA stale for this build, not hooking",
                  (unsigned long)target);
        return false;
    }
    void* tramp = nullptr;
    if (!LmHook::Install(target, reinterpret_cast<void*>(&HookFn), &tramp)) {
        Log::Warn("CDNAUTH_PROBE: LmHook::Install failed at 0x%lx", (unsigned long)target);
        return false;
    }
    g_origFn = reinterpret_cast<CdnFn>(tramp);
    Log::Info("CDNAUTH_PROBE: hooked GetCDNAuthToken at 0x%lx (RVA 0x%lx) — armed",
              (unsigned long)target, (unsigned long)kCdnAuthRva);
    return true;
}

} // namespace Hooks::CdnAuthProbe
