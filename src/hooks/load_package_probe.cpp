#include "load_package_probe.hpp"
#include "../lmhook.hpp"
#include "../log.hpp"
#include "../patterns.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace {

using ProbeFnPtr = bool (*)(void* a0, void* a1, int32_t a2, void* a3);

// Cache of currently-readable address ranges, parsed once from /proc/self/maps
// at first dereference. Stale entries are not a safety problem: an address
// reported readable here that has since been unmapped will SIGSEGV on read —
// but in practice the regions we care about (heap, .data, mmap'd libraries) are
// long-lived. A new mapping that arrives later is a "miss" (we conservatively
// skip the dump), not a crash.
struct Range { uintptr_t start, end; };
std::vector<Range>* g_readableRanges = nullptr;
std::once_flag g_rangesInitOnce;

void InitReadableRanges() {
    g_readableRanges = new std::vector<Range>();
    std::ifstream maps("/proc/self/maps");
    if (!maps.is_open()) return;
    std::string line;
    while (std::getline(maps, line)) {
        auto dash = line.find('-');
        auto sp   = line.find(' ');
        if (dash == std::string::npos || sp == std::string::npos) continue;
        if (line.size() < sp + 5) continue;
        if (line[sp + 1] != 'r') continue;
        uintptr_t s = std::strtoul(line.substr(0, dash).c_str(),       nullptr, 16);
        uintptr_t e = std::strtoul(line.substr(dash + 1, sp - dash - 1).c_str(),
                                   nullptr, 16);
        if (e > s) g_readableRanges->push_back({s, e});
    }
}

bool IsAddrReadable(const void* p, size_t len) {
    std::call_once(g_rangesInitOnce, InitReadableRanges);
    if (!g_readableRanges) return false;
    uintptr_t a = reinterpret_cast<uintptr_t>(p);
    if (a == 0 || a + len < a) return false;
    for (const auto& r : *g_readableRanges) {
        if (a >= r.start && a + len <= r.end) return true;
    }
    return false;
}

// Hardcoded candidate file offsets (in the 7c4ac73e... steamclient.so) and
// their RVAs. RVA = file_offset - 0xe33000 (the LOAD 2 segment's file→virt
// delta, derived from the binary's program headers).
struct Candidate {
    uintptr_t   rva;
    const char* note;
};
constexpr Candidate kCandidates[] = {
    {0x16dd440, "file 0x2510440 — v0.10.4 candidate (stack 0x22C; crashed when injecting)"},
    {0x0b96540, "file 0x19c9540 — alt stack 0x4BC"},
    {0x1fa95d0, "file 0x2ddc5d0 — alt stack 0x124"},
};
constexpr int kNumCandidates =
    static_cast<int>(sizeof(kCandidates) / sizeof(kCandidates[0]));

template<int Idx>
struct ProbeState {
    static inline ProbeFnPtr g_orig = nullptr;
    static inline uintptr_t  g_rva  = 0;
};

template<int Idx>
bool ProbeFn(void* a0, void* a1, int32_t a2, void* a3) {
    auto orig = ProbeState<Idx>::g_orig;
    bool result = orig ? orig(a0, a1, a2, a3) : false;

    uintptr_t rva = ProbeState<Idx>::g_rva;
    if (!a0) {
        Log::Info("PROBE[%d] rva=0x%lx arg0=NULL", Idx, (unsigned long)rva);
    } else if (IsAddrReadable(a0, 0x50)) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(a0);
        uint32_t f0  = *reinterpret_cast<const uint32_t*>(p + 0x00);
        uint8_t  fB  = *reinterpret_cast<const uint8_t* >(p + 0x0B);
        uint32_t f38 = *reinterpret_cast<const uint32_t*>(p + 0x38);
        uint32_t f44 = *reinterpret_cast<const uint32_t*>(p + 0x44);
        Log::Info("PROBE[%d] rva=0x%lx arg0=%p "
                  "[+0]=0x%08x [+0xb]=0x%02x [+0x38]=0x%08x [+0x44]=0x%08x",
                  Idx, (unsigned long)rva, a0, f0, fB, f38, f44);
    } else {
        Log::Info("PROBE[%d] rva=0x%lx arg0=%p (unreadable)",
                  Idx, (unsigned long)rva, a0);
    }
    return result;
}

template<int Idx>
bool InstallOne(uintptr_t base) {
    uintptr_t target = base + kCandidates[Idx].rva;
    void* tramp = nullptr;
    if (!LmHook::Install(target, reinterpret_cast<void*>(&ProbeFn<Idx>), &tramp)) {
        Log::Warn("LoadPackageProbe[%d]: install FAILED at 0x%lx rva=0x%lx (%s)",
                  Idx, (unsigned long)target,
                  (unsigned long)kCandidates[Idx].rva, kCandidates[Idx].note);
        return false;
    }
    ProbeState<Idx>::g_orig = reinterpret_cast<ProbeFnPtr>(tramp);
    ProbeState<Idx>::g_rva  = kCandidates[Idx].rva;
    Log::Info("LoadPackageProbe[%d]: INSTALLED at 0x%lx rva=0x%lx (%s)",
              Idx, (unsigned long)target,
              (unsigned long)kCandidates[Idx].rva, kCandidates[Idx].note);
    return true;
}

} // namespace

namespace Hooks::LoadPackageProbe {

bool Install() {
    const char* env = std::getenv("LUMA_LOADPKG_PROBE");
    if (!env || env[0] == '\0' || env[0] == '0') {
        return true;  // OFF by default; not an error
    }

    uintptr_t base = Patterns::FindSteamclientBase();
    if (!base) {
        Log::Warn("LoadPackageProbe: cannot resolve steamclient.so base — skipping");
        return false;
    }

    Log::Info("LoadPackageProbe: LUMA_LOADPKG_PROBE=%s — installing %d read-only probes",
              env, kNumCandidates);

    int installed = 0;
    if (InstallOne<0>(base)) ++installed;
    if (InstallOne<1>(base)) ++installed;
    if (InstallOne<2>(base)) ++installed;

    Log::Info("LoadPackageProbe: %d/%d probes active", installed, kNumCandidates);
    return installed > 0;
}

void Uninstall() {
    // Probes are an opt-in debug tool. We don't expose a clean uninstall path;
    // the process exits when Steam closes.
}

} // namespace Hooks::LoadPackageProbe
