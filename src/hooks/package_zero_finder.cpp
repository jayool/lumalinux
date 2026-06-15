#include "package_zero_finder.hpp"
#include "load_package_hook.hpp"
#include "../patterns.hpp"
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
// Cache layout (CPackageInfoCache) — all offsets validated against the
// decompilation of the accessor at va 0xf7d4e0 (objdump) / 0xf8d4e0 (Ghidra).
// =============================================================================
//
// Steam reaches the cache via a global pointer at fixed offset 0x3a1bc from
// the GOT base (the per-function PIC base register). The slot holds a
// pointer to a pointer:
//
//   lea  eax, [GOTbase + 0x3a1bc]   ; eax = &cache_slot
//   mov  ecx, [eax]                 ; ecx = *cache_slot   = cache_obj
//   mov  edx, [ecx + 0xc58]         ; root index of search tree (-1 if empty)
//   mov  ecx, [ecx + 0xc6c]         ; pointer to node array
//
// Each node is 0x18 bytes:
//   +0x00  int32  left  (index in node array, -1 = none)
//   +0x04  int32  right
//   +0x10  uint32 packageId        ← search key
//   +0x14  PackageInfo*            ← what we want
//
// `LoadPackage`'s injection target lives off the PackageInfo*:
//   PackageInfo + 0x00  uint32  PackageId
//   PackageInfo + 0x38  CUtlVector<AppId_t> AppIdVec   (m_pMemory@+0x38, m_Size@+0x44)
constexpr uintptr_t kCacheGlobalOffset = 0x3a1bc;
constexpr std::size_t kCacheRootIdxOff  = 0xc58;
constexpr std::size_t kCacheNodesOff    = 0xc6c;
constexpr std::size_t kNodeLeftOff      = 0x00;
constexpr std::size_t kNodeRightOff     = 0x04;
constexpr std::size_t kNodePkgIdOff     = 0x10;
constexpr std::size_t kNodePkgInfoOff   = 0x14;
constexpr std::size_t kNodeSize         = 0x18;

// Walk cap: a balanced BST with N nodes has depth <= log2(N). 64 covers up
// to 2^64 nodes — overkill, but cheap insurance against a corrupted tree
// (cycle, depth > log2(N)) preventing an infinite loop.
constexpr int kMaxTreeDepth = 64;

// Worker: poll once per second up to this many times. Steam usually
// populates the package cache during steamclient.so's init; in practice
// the finder should hit on iteration 1 or 2. The 60 s ceiling is for the
// case where the user is on the OOBE / login screen and ReadFromDisk
// hasn't fired yet.
constexpr int kFinderTimeoutSec = 60;

// =============================================================================
// Readability gate
// =============================================================================
// Cached snapshot of readable VMA ranges from /proc/self/maps. Refreshed
// lazily on first use. If the cache is stale (a region we previously saw
// gets unmapped before we read it) the dereference still SIGSEGVs, but
// long-lived regions (heap, .data, mmap'd libs) are the common case and
// don't move. Cheap defence against the cache offsets being wrong on a
// future Steam build.

struct Range { uintptr_t start, end; };
std::vector<Range> g_readable;
std::atomic<bool>  g_rangesLoaded{false};

void LoadReadableRanges() {
    std::ifstream maps("/proc/self/maps");
    if (!maps.is_open()) return;
    std::string line;
    while (std::getline(maps, line)) {
        // Format: "<startHex>-<endHex> rwxp <off> <dev> <inode> <path>"
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
    if (!g_rangesLoaded.load(std::memory_order_acquire)) LoadReadableRanges();
    uintptr_t a = reinterpret_cast<uintptr_t>(p);
    if (a == 0 || a + n < a) return false;  // null or wrap
    for (const auto& r : g_readable) {
        if (a >= r.start && a + n <= r.end) return true;
    }
    return false;
}

// =============================================================================
// Cache walk
// =============================================================================

// Returns the PackageInfo* for packageId == 0, or nullptr if not present,
// not initialised, or any pointer along the way fails the readability check.
void* FindPackage0(uintptr_t scBase) {
    auto* cacheSlot = reinterpret_cast<void**>(scBase + kCacheGlobalOffset);
    if (!IsReadable(cacheSlot, sizeof(void*))) return nullptr;
    auto* cache = static_cast<uint8_t*>(*cacheSlot);
    if (!cache || !IsReadable(cache, kCacheNodesOff + sizeof(void*))) return nullptr;

    int32_t rootIdx = *reinterpret_cast<int32_t*>(cache + kCacheRootIdxOff);
    if (rootIdx < 0) return nullptr;  // tree empty

    auto* nodes = *reinterpret_cast<uint8_t**>(cache + kCacheNodesOff);
    if (!nodes || !IsReadable(nodes, kNodeSize)) return nullptr;

    int32_t cur = rootIdx;
    for (int depth = 0; cur >= 0 && depth < kMaxTreeDepth; ++depth) {
        uint8_t* node = nodes + static_cast<std::size_t>(cur) * kNodeSize;
        if (!IsReadable(node, kNodeSize)) return nullptr;

        uint32_t key = *reinterpret_cast<uint32_t*>(node + kNodePkgIdOff);
        if (key == 0) {
            void* pkg = *reinterpret_cast<void**>(node + kNodePkgInfoOff);
            if (!pkg || !IsReadable(pkg, 0x50)) return nullptr;
            return pkg;
        }
        // BST descent. We're looking for key 0; any non-zero key is > 0,
        // so we always go left. Kept as a conditional anyway for clarity
        // (and in case the tree uses a different ordering convention).
        cur = (0 < key)
            ? *reinterpret_cast<int32_t*>(node + kNodeLeftOff)
            : *reinterpret_cast<int32_t*>(node + kNodeRightOff);
    }
    return nullptr;
}

// =============================================================================
// Worker
// =============================================================================

void Run() {
    const char* mode = std::getenv("LUMA_PKG0_FINDER");
    if (!mode || !*mode) return;

    const bool inject = std::strcmp(mode, "inject") == 0;
    if (!inject && std::strcmp(mode, "diag") != 0) {
        Log::Warn("PKG0_FINDER: LUMA_PKG0_FINDER=%s — unrecognised mode, "
                  "expected 'diag' or 'inject'; treating as 'diag'", mode);
    }

    Log::Info("PKG0_FINDER: mode=%s — searching for PackageId=0 in cache "
              "(timeout %d s)", inject ? "inject" : "diag", kFinderTimeoutSec);

    using namespace std::chrono_literals;
    for (int i = 0; i < kFinderTimeoutSec; ++i) {
        uintptr_t base = Patterns::FindSteamclientBase();
        if (base) {
            void* pkg = FindPackage0(base);
            if (pkg) {
                auto* vec = Hooks::LoadPackage::AppIdVec(pkg);
                Log::Info("PKG0_FINDER: hit pkg=%p PackageId=%u "
                          "AppIdVec{mem=%p size=%u alloc=%d} (attempt %d)",
                          pkg, Hooks::LoadPackage::PkgId(pkg),
                          static_cast<void*>(vec->m_pMemory),
                          vec->m_Size, vec->m_nAllocationCount, i + 1);

                // Dump a few entries so we can see at a glance that the
                // layout is right (real appids are small distinct positives).
                if (vec->m_pMemory && vec->m_Size > 0) {
                    uint32_t n = vec->m_Size < 4 ? vec->m_Size : 4;
                    Log::Info("PKG0_FINDER:   AppIdVec[0..%u) = {%u, %u, %u, %u}",
                              n,
                              n > 0 ? vec->m_pMemory[0] : 0,
                              n > 1 ? vec->m_pMemory[1] : 0,
                              n > 2 ? vec->m_pMemory[2] : 0,
                              n > 3 ? vec->m_pMemory[3] : 0);
                }

                if (inject) {
                    Hooks::LoadPackage::InjectDepots(pkg, "finder");
                }
                return;
            }
        }
        std::this_thread::sleep_for(1s);
    }
    Log::Warn("PKG0_FINDER: PackageId=0 not found in cache after %d s",
              kFinderTimeoutSec);
}

} // namespace

void Start() {
    // Fire off the worker and return immediately. main.cpp is supposed to
    // call this AFTER InstallHooks() so the LoadPackage hook is already
    // in place — that way if the hook does fire (e.g. Steam later parses a
    // PICS update for PackageId=0), it injects; if not, we cover the gap
    // via this worker.
    std::thread(Run).detach();
}

} // namespace Hooks::PackageZeroFinder
