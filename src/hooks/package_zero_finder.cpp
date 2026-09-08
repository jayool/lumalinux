#include "package_zero_finder.hpp"
#include "load_package_hook.hpp"
#include "../patterns.hpp"
#include "../license_reconcile.hpp"
#include "../log.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace Hooks::PackageZeroFinder {
namespace {

// =============================================================================
// Cache layout — STABLE offsets (class layout, verified to match across the
// 7c4ac73e and db0d79c2 steamclient builds).
// =============================================================================
// CPackageInfoCache stores packages in an index-based search tree:
//   cache + 0xc58   int32   root node index (-1 = empty)
//   cache + 0xc6c   T*      node array base
// Each node is 0x18 bytes:
//   +0x00  int32  left  child index (-1 = none)
//   +0x04  int32  right child index
//   +0x10  uint32 packageId        ← search key
//   +0x14  PackageInfo*            ← what we want
constexpr std::size_t kCacheRootIdxOff = 0xc58;
constexpr std::size_t kCacheNodesOff   = 0xc6c;
constexpr std::size_t kNodeLeftOff     = 0x00;
constexpr std::size_t kNodeRightOff    = 0x04;
constexpr std::size_t kNodePkgIdOff    = 0x10;
constexpr std::size_t kNodePkgInfoOff  = 0x14;
constexpr std::size_t kNodeSize        = 0x18;

constexpr int kMaxTreeDepth = 64;

// =============================================================================
// KNOWN LIMITS — audited 2026-09-08, accepted rather than fixed
// =============================================================================
// Recorded so they are not rediscovered one at a time. Numbering matches the
// audit table in RESEARCH §13.5.
//
// [14] FIVE OF THE SEVEN OFFSETS ABOVE ARE VERIFIED BY NOBODY. CI checks 0xc58
//      only, and only as a byte anchor inside the cache-access idiom — never as
//      a struct offset. 0xc6c, 0x10, 0x14, 0x18 and load_package_hook.hpp's
//      0x38 are checked by no tool at all; "verified across 7c4ac73e and
//      db0d79c2" above means a human did it once, two builds ago.
//      This is not an oversight we can close: field offsets inside a node are
//      invisible in a stripped binary without decompiling it, so CI cannot
//      derive them. MITIGATED, not fixed: FindPackage0 now cross-checks the
//      object it reaches against its own PackageId before returning it, so a
//      walk that went wrong ends in a refusal instead of a write. The offsets
//      themselves are still unverified.
//
// [15] THE TREE WALK HAS NO CONSISTENCY CONTROL. `nodes` is read once and then
//      indexed for up to kMaxTreeDepth iterations while Steam may be mutating
//      or reallocating the array from another thread. IsReadable() stops a
//      crash on unmapped memory, but not garbage read out of freed-but-still-
//      mapped memory. Realistically that yields a STALE-but-real PackageInfo
//      (harmless to append to); reaching an object of another type takes more
//      coincidence. Same mitigation as [14] — and note the finder retries here
//      rather than giving up, because unlike the code scans this read really
//      can come out consistent on the next poll.
//
// [17] GetSteamclientRx() returns lo..hi across every r-x mapping of the
//      module, so it would read across a non-readable gap between two of them.
//      Safe on every build seen: steamclient.so has a SINGLE executable
//      PT_LOAD (readelf -lW on bc54101b29). If that ever stops being true this
//      is a SIGSEGV, not a wrong answer.
//
// [16] …and it looks for "r-x" anywhere in the maps line rather than in the
//      permissions field, so a path containing "r-x" would match. No such path
//      exists today.
//
// [19] In FindPackage0's descent, `cur = (0 < key) ? left : right` always takes
//      the left branch: key is unsigned and the key==0 case returned already.
//      Correct here — 0 is the minimum, so package 0 is the leftmost node — but
//      it is written as a general search that is not general. Do not copy it.
//
// [20] kMaxTreeDepth assumes a roughly balanced tree (log2(a few thousand) is
//      ~12). A degenerate leftmost chain longer than 64 would make us give up
//      on a package that is there. Never observed.
//
// [21] IsReadable(pkg, 0x50) — 0x50 is a round number that covers AppIdVec
//      (0x38 + 16 = 0x48) with margin, not a measured object size.

// Worker cadence. We poll FOREVER (never give up): PackageId=0 only exists once
// the user has logged in and Steam has loaded the licence set, and a slow login
// must NOT break the install — the old 5-min cap did exactly that (log in at
// minute 6 → the worker had already given up → nothing injected). After the
// first hit we keep watching at a slower cadence and re-inject if Steam rebuilds
// the package (re-login, licence refresh); InjectDepots is idempotent, so each
// re-check is a no-op unless our depots actually went missing.
constexpr int kPollSec     = 2;   // cadence until PackageId=0 first appears
constexpr int kReinjectSec = 15;  // slower re-check cadence after the first hit

// =============================================================================
// Runtime-derived addressing
// =============================================================================
// The cache-global pointer lives at GOTbase + X, where X is build-specific
// (it shifts with the GOT layout across Steam builds — confirmed: 0x3a1bc on
// 7c4ac73e, 0x3967c on db0d79c2). We derive BOTH at runtime so the finder
// needs no per-build constants:
//
//   GOTbase: from the GMRC function prologue. lumalinux already locates GMRC,
//            whose prologue is `E8 <call thunk>; 05 <imm32 add eax>`. The thunk
//            returns the address after the call (G+5); the add sets eax = GOT.
//            So GOT = G + 5 + *(int32*)(G+6). (Verified: equals the .got VA.)
//
//   X:       scan the r-x mapping for the cache-access idiom
//            `lea r1,[GOT+X]; mov r2,[r1]; mov r3,[r2+0xc58]`. The 0xc58 tail is
//            the stable tree-root offset, which anchors the match. Extract X
//            from the lea's disp32. cache_global = GOT + X.

struct ScRange { uintptr_t base = 0; std::size_t size = 0; };

// Aggregate steamclient.so's r-x mappings into one [base, size) span.
ScRange GetSteamclientRx() {
    std::ifstream maps("/proc/self/maps");
    if (!maps.is_open()) return {};
    uintptr_t lo = ~uintptr_t(0), hi = 0;
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find("steamclient.so") == std::string::npos) continue;
        if (line.find("r-x") == std::string::npos) continue;
        auto dash = line.find('-');
        auto sp   = line.find(' ');
        if (dash == std::string::npos || sp == std::string::npos) continue;
        uintptr_t s = std::strtoul(line.substr(0, dash).c_str(), nullptr, 16);
        uintptr_t e = std::strtoul(line.substr(dash + 1, sp - dash - 1).c_str(),
                                    nullptr, 16);
        if (e <= s) continue;
        if (s < lo) lo = s;
        if (e > hi) hi = e;
    }
    if (hi <= lo) return {};
    return { lo, hi - lo };
}

// Derive the GOT base from the GMRC prologue. GMRC is:
//   E8 <call thunk> ; 05 <imm32 add eax> ; 55 89 e5 57 56 53 81 ec 10 01 00 00
//   8b 7d 08 8b 4d 20 ...
// lumalinux's OWN GMRC hook overwrites the leading 5-byte `E8 <call>` with its
// detour jmp, so Patterns::FindGmrcFunction() (which anchors on E8) stops
// matching once the hooks are installed — and the finder runs after that. But
// the `05 add eax,imm32` at GMRC+5 survives (the detour is only 5 bytes). So
// scan for the surviving prologue tail and compute:
//   GOT = (runtime addr of the 0x05 byte) + imm32
// because the get_pc_thunk returns GMRC+5 and `add eax,imm32` makes eax = GOT.
// Verified to reproduce the exact .got VA on the 7c4ac73e and db0d79c2 builds.
uintptr_t DeriveGotBase(ScRange rx) {
    if (!rx.base || rx.size < 23) return 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(rx.base);
    const std::size_t n = rx.size - 23;
    // bytes after the wildcarded imm32: 55 89 E5 57 56 53 81 EC 10 01 00 00
    //                                   8B 7D 08 8B 4D 20
    static const uint8_t tail[] = {
        0x55, 0x89, 0xE5, 0x57, 0x56, 0x53, 0x81, 0xEC, 0x10, 0x01, 0x00, 0x00,
        0x8B, 0x7D, 0x08, 0x8B, 0x4D, 0x20
    };
    // Like FindCacheGlobalDisp below, this must not take the first match and
    // run: it used to `return` from inside the loop, so a second site would
    // have been silently ignored and the lowest address would have won a coin
    // toss — and this one is UPSTREAM, so a wrong GOT poisons everything after
    // it (cache_global = GOT + X, however perfect X is).
    //
    // But the axis to classify on is the derived GOT, NOT the match count. If
    // this prologue shape appears in a second function, that function is PIC
    // too and its own `add eax,imm32` computes THE SAME GOT — so several sites
    // agreeing is harmless, exactly as several cache-access idiom sites naming
    // the same X are. What we cannot survive is sites that disagree.
    uintptr_t distinct[4] = {0};
    int  nDistinct = 0;
    int  sites     = 0;
    bool overflow  = false;

    for (std::size_t i = 0; i <= n; ++i) {
        if (p[i] != 0x05) continue;                          // add eax, imm32
        if (std::memcmp(p + i + 5, tail, sizeof(tail)) != 0) continue;
        int32_t imm = *reinterpret_cast<const int32_t*>(p + i + 1);
        const uintptr_t cand =
            rx.base + i + static_cast<uintptr_t>(static_cast<intptr_t>(imm));
        ++sites;
        bool seen = false;
        for (int k = 0; k < nDistinct; ++k) {
            if (distinct[k] == cand) { seen = true; break; }
        }
        if (seen) continue;
        if (nDistinct < 4) distinct[nDistinct++] = cand;
        else               overflow = true;
    }

    if (sites == 0) {
        Log::Error("PKG0_FINDER: GOT NOT_FOUND — the GMRC prologue tail is not "
                   "in the r-x span. Not injecting");
        return 0;
    }
    if (nDistinct == 1) {
        Log::Info("PKG0_FINDER: GOT UNIQUE — %d site(s), got=0x%lx",
                  sites, (unsigned long)distinct[0]);
        return distinct[0];
    }
    char list[80] = {0};
    int  off = 0;
    for (int k = 0; k < nDistinct && off < (int)sizeof(list) - 14; ++k) {
        int r = std::snprintf(list + off, sizeof(list) - (std::size_t)off,
                              " 0x%lx", (unsigned long)distinct[k]);
        if (r < 0) break;
        off += r;
    }
    Log::Error("PKG0_FINDER: GOT AMBIGUOUS — %d site(s) derive %d different GOT "
               "base(s)%s:%s — refusing to guess, not injecting",
               sites, nDistinct, overflow ? "+" : "", list);
    return 0;
}

// Scan the r-x span for the cache-access idiom and return its disp32 (X), or 0
// when the answer is not UNIQUE. (0 is never a valid GOT-relative cache offset
// here, so it doubles as the "no answer" sentinel.)
//
// FAILING CLOSED matters more here than anywhere else in the finder: X builds
// cache_global, which we then dereference and ultimately write depots through.
// "Several answers" therefore has to mean "do nothing" — never "take the first
// one", which is what this used to do. First-by-address has no relation
// whatsoever to which one is correct; it is a coin toss whose result was being
// used to pick a write target. Same rule the two critical hooks now follow
// (Patterns::FindUniqueInSteamclient): ambiguous == unresolved.
//
// The idiom is 14 bytes:
//   8D /r mod=10 disp32        lea  r1,[GOT + X]
//   8B /r mod=00               mov  r2,[r1]
//   8B /r mod=10 58 0C 00 00   mov  r3,[r2 + 0xc58]
// The mod fields and the rm exclusions (no SIB, no disp32-only) are what pin
// those 6+2+6 lengths, so the 0xc58 anchor lands exactly at +10.
//
// Beyond the shape we require the three to be CHAINED — reg(lea)==rm(mov1) and
// reg(mov1)==rm(mov2), i.e. each instruction's destination is the next one's
// base. That is what makes them one expression instead of three neighbours.
// Without it three unrelated instructions whose forms happen to line up in
// front of a 0xc58 displacement are accepted, e.g.
//     8D B0 xx xx xx xx   lea esi,[eax+disp32]
//     8B 39               mov edi,[ecx]          <- reads ecx, not esi
//     8B 82 58 0C 00 00   mov eax,[edx+0xc58]    <- reads edx, not edi
// and every such accident is a candidate X that would now make the scan
// AMBIGUOUS and take the finder down with it. Tightening the match and failing
// closed only work as a pair; either alone is worse than what was here.
//
// What we deliberately do NOT require is a fixed base register for the lea.
// There is no "the GOT register": the compiler uses whatever it has free, and
// measured on bc54101b29 the two real sites use DIFFERENT ones —
//     lea eax,[esi+0x3b7d4]  @ rva 0xfdd2dd
//     lea eax,[eax+0x3b7d4]  @ rva 0x18964e5
// so pinning it would have discarded a good site.
//
// Measured on bc54101b29 with tools/experiment_cache_idiom.py: 2 sites, both
// chained, both disp32=0x3b7d4, both resolving to 0x2f85b20 in .bss. This build
// is UNIQUE, so the change is a no-op on it — it closes the hole for a future
// build, it does not fix a live failure.
constexpr int kMaxDistinctDisp = 4;   // only so the log can list them

int32_t FindCacheGlobalDisp(ScRange rx) {
    if (!rx.base || rx.size < 14) return 0;
    const uint8_t* p   = reinterpret_cast<const uint8_t*>(rx.base);
    const std::size_t n = rx.size - 14;

    int32_t distinct[kMaxDistinctDisp] = {0};
    int  nDistinct = 0;
    int  sites     = 0;
    bool overflow  = false;

    for (std::size_t i = 0; i <= n; ++i) {
        // lea r1, [base + disp32] : 8d MODRM(mod=10, base!=SIB) disp32
        if (p[i] != 0x8d) continue;
        uint8_t m1 = p[i + 1];
        if ((m1 & 0xc0) != 0x80 || (m1 & 0x07) == 0x04) continue;
        // mov r2, [r1] : 8b MODRM(mod=00, no disp / no SIB)
        if (p[i + 6] != 0x8b) continue;
        uint8_t m2 = p[i + 7];
        if ((m2 & 0xc0) != 0x00 || (m2 & 0x07) == 0x04 || (m2 & 0x07) == 0x05) continue;
        // mov r3, [r2 + 0xc58] : 8b MODRM(mod=10) 58 0c 00 00
        if (p[i + 8] != 0x8b) continue;
        uint8_t m3 = p[i + 9];
        if ((m3 & 0xc0) != 0x80 || (m3 & 0x07) == 0x04) continue;
        if (*reinterpret_cast<const uint32_t*>(p + i + 10) != 0x00000c58u) continue;
        // chained: each instruction's destination is the next one's base
        if (((m1 >> 3) & 0x07) != (m2 & 0x07)) continue;   // reg(lea)  == rm(mov1)
        if (((m2 >> 3) & 0x07) != (m3 & 0x07)) continue;   // reg(mov1) == rm(mov2)

        int32_t disp = *reinterpret_cast<const int32_t*>(p + i + 2);
        ++sites;
        bool seen = false;
        for (int k = 0; k < nDistinct; ++k) {
            if (distinct[k] == disp) { seen = true; break; }
        }
        if (seen) continue;
        if (nDistinct < kMaxDistinctDisp) distinct[nDistinct++] = disp;
        else                              overflow = true;
    }

    if (sites == 0) {
        Log::Error("PKG0_FINDER: cache-access idiom NOT_FOUND (anchor 0x%x) — "
                   "CPackageInfoCache layout changed? Not injecting",
                   (unsigned)kCacheRootIdxOff);
        return 0;
    }
    if (nDistinct == 1) {   // overflow implies nDistinct == kMaxDistinctDisp
        Log::Info("PKG0_FINDER: cache-access idiom UNIQUE — %d site(s), disp=0x%x",
                  sites, (unsigned)distinct[0]);
        return distinct[0];
    }
    char list[80] = {0};
    int  off = 0;
    for (int k = 0; k < nDistinct && off < (int)sizeof(list) - 12; ++k) {
        int r = std::snprintf(list + off, sizeof(list) - (std::size_t)off,
                              " 0x%x", (unsigned)distinct[k]);
        if (r < 0) break;
        off += r;
    }
    Log::Error("PKG0_FINDER: cache-access idiom AMBIGUOUS — %d site(s), %d distinct "
               "disp32%s:%s — refusing to guess, not injecting",
               sites, nDistinct, overflow ? "+" : "", list);
    return 0;
}

// =============================================================================
// Readability gate (parsed once; long-lived regions don't move)
// =============================================================================
struct Range { uintptr_t start, end; };
std::vector<Range> g_readable;
std::atomic<bool>  g_rangesLoaded{false};

void LoadReadableRanges() {
    std::ifstream maps("/proc/self/maps");
    if (!maps.is_open()) return;
    std::string line;
    while (std::getline(maps, line)) {
        auto dash = line.find('-');
        auto sp   = line.find(' ');
        if (dash == std::string::npos || sp == std::string::npos) continue;
        if (line.size() < sp + 5 || line[sp + 1] != 'r') continue;
        uintptr_t s = std::strtoul(line.substr(0, dash).c_str(), nullptr, 16);
        uintptr_t e = std::strtoul(line.substr(dash + 1, sp - dash - 1).c_str(),
                                    nullptr, 16);
        if (e > s) g_readable.push_back({s, e});
    }
    g_rangesLoaded.store(true, std::memory_order_release);
}

bool IsReadable(const void* p, std::size_t n) {
    // Always re-read on each top-level attempt via ResetReadable(); within a
    // walk the snapshot is stable enough.
    if (!g_rangesLoaded.load(std::memory_order_acquire)) LoadReadableRanges();
    uintptr_t a = reinterpret_cast<uintptr_t>(p);
    if (a == 0 || a + n < a) return false;
    for (const auto& r : g_readable) {
        if (a >= r.start && a + n <= r.end) return true;
    }
    return false;
}

void ResetReadable() {
    g_readable.clear();
    g_rangesLoaded.store(false, std::memory_order_release);
}

// =============================================================================
// Cache walk
// =============================================================================
// Returns package 0's PackageInfo*, or nullptr (with a diagnostic log line at
// the failure point). cacheGlobal is GOT + X (the runtime address of the slot
// holding the cache pointer).
void* FindPackage0(uintptr_t cacheGlobal) {
    if (!IsReadable(reinterpret_cast<void*>(cacheGlobal), sizeof(void*))) {
        Log::Debug("PKG0_FINDER: cache-global slot 0x%lx not readable",
                   (unsigned long)cacheGlobal);
        return nullptr;
    }
    auto* cache = *reinterpret_cast<uint8_t**>(cacheGlobal);
    if (!cache || !IsReadable(cache, kCacheNodesOff + sizeof(void*))) {
        Log::Debug("PKG0_FINDER: cache object %p not readable/null", (void*)cache);
        return nullptr;
    }

    int32_t rootIdx = *reinterpret_cast<int32_t*>(cache + kCacheRootIdxOff);
    auto*   nodes   = *reinterpret_cast<uint8_t**>(cache + kCacheNodesOff);
    if (rootIdx < 0) {
        Log::Debug("PKG0_FINDER: tree empty (rootIdx=%d) — packages not loaded yet",
                   rootIdx);
        return nullptr;
    }
    if (!nodes || !IsReadable(nodes, kNodeSize)) {
        Log::Debug("PKG0_FINDER: node array %p not readable", (void*)nodes);
        return nullptr;
    }

    int32_t cur = rootIdx;
    for (int depth = 0; cur >= 0 && depth < kMaxTreeDepth; ++depth) {
        uint8_t* node = nodes + static_cast<std::size_t>(cur) * kNodeSize;
        if (!IsReadable(node, kNodeSize)) {
            Log::Debug("PKG0_FINDER: node idx %d at %p not readable", cur, (void*)node);
            return nullptr;
        }
        uint32_t key = *reinterpret_cast<uint32_t*>(node + kNodePkgIdOff);
        if (key == 0) {
            void* pkg = *reinterpret_cast<void**>(node + kNodePkgInfoOff);
            if (!pkg || !IsReadable(pkg, 0x50)) {
                Log::Debug("PKG0_FINDER: package-0 node found but PackageInfo %p bad",
                           pkg);
                return nullptr;
            }
            // Cross-check the object against ITSELF before handing it over to be
            // written into.
            //
            // Everything above this line is navigation: seven compiled-in
            // offsets, walked through a structure Steam may be mutating from
            // another thread while we read it. The only validation we had was
            // "is this address readable", which a stale or torn read passes
            // happily — freed-but-still-mapped memory reads fine.
            //
            // But TWO independent sources say "this is package 0": the tree
            // node's key field (+0x10, read above) and the object's own id
            // (+0x00, here). We were using one and ignoring the other — and not
            // for lack of access: PkgId() was already being called, purely to
            // print it in the HIT line, and only on the first hit. Free evidence
            // thrown away.
            //
            // This is the practical mitigation for KNOWN LIMIT [14]. Five of the
            // seven offsets cannot be validated statically at all (field offsets
            // inside a node are invisible in a stripped binary), so instead of
            // checking the map we check where it took us. A disagreement means
            // either a torn/stale read or that the offsets have gone wrong; both
            // mean the same thing — do not write here.
            //
            // Unlike the resolution scans, this returns nullptr rather than
            // giving up: those read code bytes that are final, so a failure is
            // permanent and piece 3 made them one-shot. THIS read is genuinely
            // transient — the same walk can be consistent on the next poll — so
            // retrying is the correct response, and the caller already does.
            const uint32_t selfId = Hooks::LoadPackage::PkgId(pkg);
            if (selfId != 0) {
                Log::Warn("PKG0_FINDER: node idx %d has key 0 but the PackageInfo "
                          "at %p reports PackageId=%u — inconsistent read, not "
                          "injecting this pass", cur, pkg, selfId);
                return nullptr;
            }
            return pkg;
        }
        cur = (0 < key)
            ? *reinterpret_cast<int32_t*>(node + kNodeLeftOff)
            : *reinterpret_cast<int32_t*>(node + kNodeRightOff);
    }
    Log::Debug("PKG0_FINDER: walked tree (rootIdx=%d) without hitting PackageId=0",
               rootIdx);
    return nullptr;
}

// =============================================================================
// Worker
// =============================================================================
void Run() {
    // ON BY DEFAULT — the finder IS the fix for the cached-PackageId=0
    // regression, so it must run without the user setting anything. Mirrors the
    // LUMA_NO_* hook gates: LUMA_NO_PKG0_FINDER disables it entirely.
    if (const char* off = std::getenv("LUMA_NO_PKG0_FINDER"); off && off[0] && off[0] != '0') {
        Log::Info("PKG0_FINDER: disabled via LUMA_NO_PKG0_FINDER");
        return;
    }
    // LUMA_PKG0_FINDER=diag → walk + log without injecting; anything else
    // (including unset) → inject.
    const char* mode = std::getenv("LUMA_PKG0_FINDER");
    const bool inject = !(mode && std::strcmp(mode, "diag") == 0);
    if (mode && *mode && std::strcmp(mode, "inject") != 0 && std::strcmp(mode, "diag") != 0) {
        Log::Warn("PKG0_FINDER: LUMA_PKG0_FINDER=%s unrecognised — treating as inject", mode);
    }
    Log::Info("PKG0_FINDER: mode=%s (on by default) — runtime-deriving cache addr, "
              "polling every %ds until found, then re-checking every %ds",
              inject ? "inject" : "diag", kPollSec, kReinjectSec);

    using namespace std::chrono_literals;

    uintptr_t got = 0;
    int32_t   disp = 0;
    uintptr_t cacheGlobal = 0;
    bool      foundOnce = false;

    for (;;) {
        ScRange rx = GetSteamclientRx();
        if (rx.base) {
            // Resolve the cache address ONCE — the first time steamclient.so is
            // visible — and never again.
            //
            // Both steps below read code bytes that are FINAL by then. The module
            // has a single executable PT_LOAD (verified with readelf -lW on
            // bc54101b29: one `R E` segment, 0x1ffe574 = ~32 MB), so seeing any
            // r-x mapping for it means the whole text segment is mapped; there is
            // no window in which we could see half of it. Nothing reorders those
            // bytes afterwards either: our own detours and a plugin's are written
            // at function ENTRIES, and neither anchor can start there — the idiom
            // needs the GOT already in a register, which takes the 10-byte PIC
            // preamble first, while a detour covers bytes 0..4. Same bytes, same
            // answer: a failure here is permanent.
            //
            // And retrying it is not free. Neither scan can exit early — proving
            // there is exactly ONE match means walking the whole span even when
            // the match is at byte 0. That is the price of never writing to a
            // guessed address (see FindCacheGlobalDisp, and the same reasoning in
            // Patterns::FindUniqueInSteamclient), and it is a price meant to be
            // paid once per launch, like the four hook scans — not ~32 MB every
            // kPollSec for the life of the process, which is what this loop used
            // to do, forever, to keep reaching the same conclusion.
            //
            // So: resolve, or give up and end the thread. Nothing is lost by
            // ending it — injection, the licence reconcile and the keys.txt wake
            // are all nested under `cacheGlobal` below, so none of them can run
            // without an address anyway, and nothing observes this thread's
            // liveness (it is absent from status.json). The four hooks are
            // unaffected; only package-0 injection is off for this session.
            if (!cacheGlobal) {
                // Neither call needs an else: each logs its own NOT_FOUND /
                // AMBIGUOUS cause, with the consequence, at Error severity.
                got = DeriveGotBase(rx);
                if (got) {
                    disp = FindCacheGlobalDisp(rx);
                }
                // Both outcomes below emit TWO lines, mirroring what every hook
                // does (depot_key_hook.cpp:181-206 is the reference): a prose
                // line saying what happened, then a machine-readable twin whose
                // last field is outcome=. maintenance.md's triage is "grep
                // outcome=", and until now the finder answered nothing to it —
                // the one subsystem that is the SOLE depot injector was the one
                // invisible to the procedure. Field order and severities are
                // taken from the hooks, not invented: name= method= …
                // outcome=, outcome always last; failure is prose Error +
                // structured Warn, success is both Info.
                //
                // The prefix is "Finder resolve:", not "Hook install:", because
                // this installs nothing — it resolves an address. Same reason
                // outcome=resolved rather than installed. method= names the
                // resolver that won, exactly as in the hooks; there is only one
                // today, so it is always `scan`. If the finder ever reads
                // finder.cache_global_disp from the RVA feed (published on every
                // build, read by nobody — RESEARCH §13.5.a), this becomes
                // method=rva|scan and lines up with the hooks' method=rva|pattern
                // without touching the format.
                if (!disp) {
                    // The cause was logged by whichever step knows it (GOT above,
                    // or FindCacheGlobalDisp's NOT_FOUND/AMBIGUOUS). One line for
                    // the consequence, then stop.
                    Log::Error("PKG0_FINDER: no cache address (cause on the previous "
                               "line) — finder ends; package-0 injection is off for "
                               "this session, hooks are unaffected");
                    Log::Warn("Finder resolve: name=PKG0Finder method=none outcome=miss");
                    return;
                }
                cacheGlobal = got + static_cast<uintptr_t>(static_cast<intptr_t>(disp));
                Log::Info("PKG0_FINDER: GOT=0x%lx disp=0x%x cache_global=0x%lx",
                          (unsigned long)got, (unsigned)disp,
                          (unsigned long)cacheGlobal);
                Log::Info("Finder resolve: name=PKG0Finder method=scan got=0x%lx "
                          "disp=0x%x cache_global=0x%lx outcome=resolved",
                          (unsigned long)got, (unsigned)disp,
                          (unsigned long)cacheGlobal);
            }

            if (cacheGlobal) {
                ResetReadable();  // refresh /proc/self/maps snapshot each attempt
                void* pkg = FindPackage0(cacheGlobal);
                if (pkg) {
                    if (!foundOnce) {
                        auto* vec = Hooks::LoadPackage::AppIdVec(pkg);
                        Log::Info("PKG0_FINDER: HIT pkg=%p PackageId=%u "
                                  "AppIdVec{mem=%p size=%u alloc=%d}",
                                  pkg, Hooks::LoadPackage::PkgId(pkg),
                                  static_cast<void*>(vec->m_pMemory),
                                  vec->m_Size, vec->m_nAllocationCount);
                        if (vec->m_pMemory && vec->m_Size > 0) {
                            uint32_t k = vec->m_Size < 4 ? vec->m_Size : 4;
                            Log::Info("PKG0_FINDER:   AppIdVec[0..%u) = {%u, %u, %u, %u}", k,
                                      k > 0 ? vec->m_pMemory[0] : 0,
                                      k > 1 ? vec->m_pMemory[1] : 0,
                                      k > 2 ? vec->m_pMemory[2] : 0,
                                      k > 3 ? vec->m_pMemory[3] : 0);
                        }
                        foundOnce = true;
                    }
                    // Re-inject every time we see the package. InjectDepots is
                    // idempotent (content-based dedup), so this is a no-op unless
                    // our depots went missing — exactly what we want if Steam
                    // rebuilt PackageId=0 after a re-login / licence refresh. The
                    // finder is the SOLE injector (the LoadPackage hook no longer
                    // injects), so only this one thread mutates AppIdVec — no
                    // cross-thread race, no lock needed.
                    if (inject) Hooks::LoadPackage::InjectDepots(pkg, "finder");

                    // no-restart experiment: if a game was just added (keys.txt
                    // changed), fire the license reconcile HERE — on this thread,
                    // AFTER the new depots are injected above — so Steam re-reads
                    // ownership/appinfo without a restart. No-op unless enabled.
                    if (inject && LicenseReconcile::Enabled() &&
                        LicenseReconcile::TakeKeysChanged()) {
                        LicenseReconcile::Reconcile();
                    }
                }
            }
        }
        // Never give up: poll fast until the package first appears, then watch
        // at a slower cadence so a rebuilt PackageId=0 gets re-injected. But wake
        // IMMEDIATELY if a game was just added (keys.txt changed) so its depots
        // are injected + reconciled at once instead of up to kReinjectSec later —
        // the #38 "install too soon -> 0 target depots" window. The idle cadence
        // (and its safety-reinject role) is unchanged; only wake-on-add is new.
        LicenseReconcile::WaitForKeysChangeOr(foundOnce ? kReinjectSec : kPollSec);
    }
}

} // namespace

void Start() {
    std::thread(Run).detach();
}

} // namespace Hooks::PackageZeroFinder
