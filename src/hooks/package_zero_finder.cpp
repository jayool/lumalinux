#include "package_zero_finder.hpp"
#include "load_package_hook.hpp"
#include "../patterns.hpp"
#include "../license_reconcile.hpp"
#include "../log.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
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
    for (std::size_t i = 0; i <= n; ++i) {
        if (p[i] != 0x05) continue;                          // add eax, imm32
        if (std::memcmp(p + i + 5, tail, sizeof(tail)) != 0) continue;
        int32_t imm = *reinterpret_cast<const int32_t*>(p + i + 1);
        return rx.base + i + static_cast<uintptr_t>(static_cast<intptr_t>(imm));
    }
    return 0;
}

// Scan the r-x span for the cache-access idiom and return its disp32 (X).
// Returns 0 if not found (0 is never a valid GOT-relative cache offset here).
int32_t FindCacheGlobalDisp(ScRange rx) {
    if (!rx.base || rx.size < 14) return 0;
    const uint8_t* p   = reinterpret_cast<const uint8_t*>(rx.base);
    const std::size_t n = rx.size - 14;

    int32_t found = 0;
    int     foundCount = 0;
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

        int32_t disp = *reinterpret_cast<const int32_t*>(p + i + 2);
        if (foundCount == 0) {
            found = disp;
            foundCount = 1;
        } else if (disp != found) {
            // Two different disps for the same idiom — ambiguous, log and keep
            // the first (the real cache is by far the most common; mismatches
            // would be rare unrelated code). Counted for the log line.
            ++foundCount;
        } else {
            ++foundCount;
        }
    }
    if (foundCount > 0) {
        Log::Info("PKG0_FINDER: cache-access idiom found %d time(s), disp=0x%x",
                  foundCount, (unsigned)found);
    }
    return found;
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
            // Derive GOT + cache offset once we can see steamclient.so. These
            // are stable for the process lifetime, so compute them once.
            if (!cacheGlobal) {
                got = DeriveGotBase(rx);
                if (got) {
                    disp = FindCacheGlobalDisp(rx);
                    if (disp) {
                        cacheGlobal = got + static_cast<uintptr_t>(static_cast<intptr_t>(disp));
                        Log::Info("PKG0_FINDER: GOT=0x%lx disp=0x%x cache_global=0x%lx",
                                  (unsigned long)got, (unsigned)disp,
                                  (unsigned long)cacheGlobal);
                    } else {
                        Log::Warn("PKG0_FINDER: cache-access idiom not found in r-x "
                                  "— class layout changed?");
                    }
                } else {
                    Log::Debug("PKG0_FINDER: GOT not derived yet (GMRC prologue tail "
                               "not located)");
                }
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
