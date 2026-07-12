#include "sls_achievement_unblock.hpp"
#include "log.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <elf.h>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace SlsAchievementUnblock {
namespace {

// Mangled names in SLSsteam.so (verified present in the 20260711 release, which
// ships NOT stripped). If AceSLS renames/strips these, ResolveSymbols fails and
// Apply() no-ops — a safe degradation.
constexpr const char* kIsSubscribed   = "_ZN5CUser12isSubscribedEj";
constexpr const char* kIsAddedAppId   = "_ZN7CConfig12isAddedAppIdEj";
constexpr const char* kGConfig        = "g_config";
constexpr const char* kGetUserStats   =
    "_ZN12Achievements23sendAndRecvGetUserStatsEP7CAPIJobP16CProtoBufMsgBasejS3_j";
constexpr const char* kGetPlayerStats =
    "_ZN12Achievements25sendAndRecvGetPlayerStatsEP30CClientUnifiedServiceTransport"
    "PKcP28CPlayer_GetUserStats_RequestP29CPlayer_GetUserStats_Response";

// Resolved-at-init pointers the replacement guard calls. SLSsteam is x86-32 GCC:
// non-static methods are cdecl with `this` as an explicit first stack arg, so a
// plain cdecl free function pointer matches the ABI exactly.
using IsSubFn   = uint8_t (*)(void* cuser, uint32_t appid);   // CUser::isSubscribed
using IsAddedFn = uint8_t (*)(void* cconfig, uint32_t appid); // CConfig::isAddedAppId
IsSubFn   g_isSubscribed = nullptr;
IsAddedFn g_isAddedAppId = nullptr;
void*     g_configPtr    = nullptr;

// Opt-in (LUMA_SLS_ACH_TRACE) one-line-per-call trace, for verifying the guard
// on-device — notably the x86-32 GCC calling convention (`this` first on the
// stack). A correct convention logs the REAL appid (e.g. 1454400) and sane 0/1
// flags; a swapped/garbled convention would log a pointer-sized appid or crash.
bool g_trace = false;

// The scoped replacement for `isSubscribed(appId)` at the guard call site. The
// following `test al,al; jne <return>` returns (skips the schema borrow) only
// when this yields non-zero — i.e. ONLY for genuinely-owned games:
//   subscribed && !added  → 1 → skip borrow  (real purchase, untouched)
//   subscribed &&  added  → 0 → run borrow    (LumaDeck game → native cheevos)
//   !subscribed           → 0 → run borrow    (unchanged from stock)
// Receives the same (this, appid) the original call site pushed. Never called
// unless Apply() succeeded (the call is only repointed on success).
extern "C" uint8_t sls_ach_combined_guard(void* cuser, uint32_t appid) {
    // Step-by-step trace (opt-in via LUMA_SLS_ACH_TRACE) so that if a call
    // faults, the last surviving line pinpoints WHICH callee died: ENTER logs
    // the args as
    // the caller pushed them (a correct cdecl convention shows a real appid like
    // 1454400 and a sane cuser pointer); "sub ok" means isSubscribed returned;
    // "added ok" means isAddedAppId returned (i.e. g_configPtr was a valid this).
    if (g_trace) Log::Info("SLS-ach guard ENTER: cuser=%p appid=%u", cuser, appid);
    const uint8_t sub = g_isSubscribed(cuser, appid);
    if (g_trace) Log::Info("SLS-ach guard: isSubscribed ok, sub=%u", sub);
    const uint8_t added = sub ? g_isAddedAppId(g_configPtr, appid) : 0;
    if (g_trace) Log::Info("SLS-ach guard: isAddedAppId ok, added=%u", added);
    const uint8_t skip = static_cast<uint8_t>(sub && !added);
    if (g_trace)
        Log::Info("SLS-ach guard: appid=%u sub=%u added=%u -> skip=%u",
                  appid, sub, added, skip);
    return skip;
}

struct SoInfo { uintptr_t base = 0; std::string path; };

// SLSsteam.so load base (the offset-0 mapping start — its lowest PT_LOAD has
// vaddr 0, as every GCC .so does, so base + st_value == runtime address) and its
// on-disk path, both from /proc/self/maps.
bool FindSo(SoInfo& out) {
    std::ifstream maps("/proc/self/maps");
    if (!maps.is_open()) return false;
    std::string line;
    bool haveBase = false;
    while (std::getline(maps, line)) {
        if (line.find("SLSsteam.so") == std::string::npos) continue;
        if (out.path.empty()) {
            auto sl = line.find('/');
            if (sl != std::string::npos) {
                out.path = line.substr(sl);
                while (!out.path.empty() &&
                       (out.path.back() == '\n' || out.path.back() == '\r' ||
                        out.path.back() == ' '  || out.path.back() == '\t'))
                    out.path.pop_back();
            }
        }
        unsigned long start = 0, end = 0, off = ~0UL;
        char perms[8] = {0};
        if (std::sscanf(line.c_str(), "%lx-%lx %7s %lx", &start, &end, perms, &off) == 4) {
            if (off == 0 && !haveBase) { out.base = start; haveBase = true; }
        }
    }
    return haveBase && !out.path.empty();
}

struct Sym { uintptr_t value = 0; uint32_t size = 0; bool found = false; };

// Resolve every name in `names` from the ELF32 .symtab of the on-disk .so.
// Returns false unless ALL are found (fail-closed).
bool ResolveSymbols(const std::string& path, const std::vector<const char*>& names,
                    std::vector<Sym>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
    if (buf.size() < sizeof(Elf32_Ehdr)) return false;

    const auto* eh = reinterpret_cast<const Elf32_Ehdr*>(buf.data());
    if (std::memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0) return false;
    if (eh->e_ident[EI_CLASS] != ELFCLASS32) return false;
    if (eh->e_shentsize < sizeof(Elf32_Shdr) || eh->e_shoff == 0) return false;
    if ((size_t)eh->e_shoff + (size_t)eh->e_shnum * eh->e_shentsize > buf.size())
        return false;

    const auto* shs = reinterpret_cast<const Elf32_Shdr*>(buf.data() + eh->e_shoff);
    const Elf32_Shdr* symtab = nullptr;
    for (int i = 0; i < eh->e_shnum; ++i)
        if (shs[i].sh_type == SHT_SYMTAB) { symtab = &shs[i]; break; }
    if (!symtab || symtab->sh_entsize < sizeof(Elf32_Sym) ||
        symtab->sh_link >= eh->e_shnum)
        return false;

    const Elf32_Shdr& strtab = shs[symtab->sh_link];
    if ((size_t)symtab->sh_offset + symtab->sh_size > buf.size()) return false;
    if ((size_t)strtab.sh_offset + strtab.sh_size > buf.size()) return false;

    const char*  strs = reinterpret_cast<const char*>(buf.data() + strtab.sh_offset);
    const size_t nstr = strtab.sh_size;
    const size_t nsym = symtab->sh_size / symtab->sh_entsize;

    out.assign(names.size(), Sym{});
    for (size_t s = 0; s < nsym; ++s) {
        const auto* sym = reinterpret_cast<const Elf32_Sym*>(
            buf.data() + symtab->sh_offset + s * symtab->sh_entsize);
        if (sym->st_name >= nstr) continue;
        const char* nm = strs + sym->st_name;
        for (size_t k = 0; k < names.size(); ++k)
            if (!out[k].found && std::strcmp(nm, names[k]) == 0) {
                out[k].value = sym->st_value;
                out[k].size  = sym->st_size;
                out[k].found = true;
            }
    }
    for (const auto& s : out)
        if (!s.found) return false;
    return true;
}

// Within [fn, fn+size) find the single isSubscribed guard and return the address
// of its `call` (E8), or 0 on zero/multiple matches (fail-safe). The guard is:
//   E8 rel32          call CUser::isSubscribed
//   83 C4 imm8        add $imm,%esp   (cdecl caller cleanup — the byte I missed
//                                      the first time; imm left wild so a build
//                                      that cleans a different amount still hits)
//   84 C0             test %al,%al
//   75 rel8           jne <early return>
uintptr_t FindGuardCall(uintptr_t fn, uint32_t size) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(fn);
    uintptr_t hit = 0;
    int count = 0;
    for (uint32_t i = 0; i + 12 <= size; ++i)
        if (p[i] == 0xE8 &&
            p[i + 5] == 0x83 && p[i + 6] == 0xC4 &&        // add $imm8,%esp
            p[i + 8] == 0x84 && p[i + 9] == 0xC0 &&        // test %al,%al
            p[i + 10] == 0x75) {                           // jne rel8
            hit = fn + i;
            ++count;
        }
    return count == 1 ? hit : 0;
}

// Current absolute target of a `E8 rel32` call at `callSite`.
uintptr_t CallTarget(uintptr_t callSite) {
    int32_t rel;
    std::memcpy(&rel, reinterpret_cast<const void*>(callSite + 1), 4);
    return callSite + 5 + rel;
}

// Atomically repoint the 4-byte rel32 of a live `E8 rel32` call.
//
// The v0.16.2 OOBE was a TORN WRITE: the old code flipped the page to RW
// (dropping PROT_EXEC) and did a byte-wise memcpy of the rel32 while steamclient
// threads were executing the very function being patched. On-device coredump
// proved it — a SIGILL whose instruction pointer landed at a *wrong* offset
// inside liblumalinux.so (the redirect target's module) reached with a garbage
// return address: a Steam thread executed the `call` mid-write, read a
// half-updated rel32, and jumped into hyperspace. Game mode reproduced it
// reliably because it drives those functions concurrently at boot.
//
// This version removes both hazards:
//   1. The page keeps PROT_EXEC throughout, so no concurrent instruction fetch
//      can hit a non-executable page. (If a hardened kernel refuses W^X the
//      mprotect fails and we no-op — fail-safe, never a crash.)
//   2. The rel32 is written with ONE aligned dword store. x86 guarantees a
//      4-byte store is atomic as long as it does not cross a 64-byte cache line,
//      so a thread executing the call concurrently observes either the OLD
//      target (isSubscribed — fine) or the NEW one (combined_guard — fine),
//      never a torn mix. If the operand would straddle a cache line (rare) we
//      skip that site rather than risk a non-atomic store.
bool WriteRel32(uintptr_t addr, uint32_t value) {
    constexpr uintptr_t kCacheLine = 64;
    if ((addr & (kCacheLine - 1)) > kCacheLine - 4) {
        Log::Warn("SLS-ach: rel32 at 0x%lx straddles a cache line — skipping this "
                  "site (no torn write)", (unsigned long)addr);
        return false;
    }
    const long pageSz = sysconf(_SC_PAGESIZE);
    const uintptr_t pageMask = ~static_cast<uintptr_t>(pageSz - 1);
    uintptr_t first = addr & pageMask;
    uintptr_t last  = (addr + 4 - 1) & pageMask;
    void*  page = reinterpret_cast<void*>(first);
    size_t len  = (last - first) + static_cast<size_t>(pageSz);
    // Keep EXEC set — the page is never non-executable during the write.
    if (mprotect(page, len, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        Log::Warn("SLS-ach: mprotect(RWX) failed at 0x%lx", (unsigned long)addr);
        return false;
    }
    // Single atomic (within-cache-line) dword store — no torn read possible.
    __atomic_store_n(reinterpret_cast<uint32_t*>(addr), value, __ATOMIC_SEQ_CST);
    mprotect(page, len, PROT_READ | PROT_EXEC);  // best-effort restore to RX
    return true;
}

} // namespace

bool Apply() {
    if (const char* off = std::getenv("LUMA_NO_SLS_ACH_UNBLOCK");
        off && off[0] && off[0] != '0') {
        Log::Info("SLS-ach: disabled via LUMA_NO_SLS_ACH_UNBLOCK");
        return false;
    }

    g_trace = std::getenv("LUMA_SLS_ACH_TRACE") != nullptr;

    SoInfo so;
    if (!FindSo(so)) {
        Log::Info("SLS-ach: SLSsteam.so not mapped — nothing to patch");
        return false;
    }

    // Order matters: [0]=isSubscribed [1]=isAddedAppId [2]=g_config
    //                [3]=getUserStats [4]=getPlayerStats
    std::vector<const char*> names = {
        kIsSubscribed, kIsAddedAppId, kGConfig, kGetUserStats, kGetPlayerStats,
    };
    std::vector<Sym> syms;
    if (!ResolveSymbols(so.path, names, syms)) {
        Log::Warn("SLS-ach: could not resolve SLSsteam symbols (stripped or renamed?) "
                  "— native achievements stay off. %s", so.path.c_str());
        return false;
    }

    const uintptr_t combined = reinterpret_cast<uintptr_t>(&sls_ach_combined_guard);

    // Locate the guard call in both achievement entry points BEFORE patching.
    uintptr_t guardA = FindGuardCall(so.base + syms[3].value, syms[3].size); // GetUserStats
    uintptr_t guardB = FindGuardCall(so.base + syms[4].value, syms[4].size); // GetPlayerStats
    if (!guardA || !guardB) {
        Log::Warn("SLS-ach: guard pattern not found exactly once (UserStats=%s, "
                  "PlayerStats=%s) — SLSsteam codegen may have shifted; leaving off.",
                  guardA ? "ok" : "MISS", guardB ? "ok" : "MISS");
        return false;
    }

    // Cross-check: both guards must call the SAME target (isSubscribed@plt). This
    // confirms we found the isSubscribed guards and not some other call+test+jne.
    if (CallTarget(guardA) != CallTarget(guardB)) {
        Log::Warn("SLS-ach: the two guard calls target different addresses "
                  "(0x%lx vs 0x%lx) — refusing to patch.",
                  (unsigned long)CallTarget(guardA), (unsigned long)CallTarget(guardB));
        return false;
    }

    // Wire the callees combined_guard invokes (direct, bypassing the PLT).
    g_isSubscribed = reinterpret_cast<IsSubFn>(so.base + syms[0].value);
    g_isAddedAppId = reinterpret_cast<IsAddedFn>(so.base + syms[1].value);
    g_configPtr    = reinterpret_cast<void*>(so.base + syms[2].value);

    // The rel32 cross-.so distance FITS on-device (~-23MB, well within +/-2GB),
    // so the E8 rel32 repoint is sound. Kept as a one-line startup breadcrumb.
    const long long distA = static_cast<long long>(combined) -
                            static_cast<long long>(guardA + 5);
    const long long distB = static_cast<long long>(combined) -
                            static_cast<long long>(guardB + 5);
    const bool fitsA = distA >= -2147483648LL && distA <= 2147483647LL;
    const bool fitsB = distB >= -2147483648LL && distB <= 2147483647LL;
    Log::Info("SLS-ach: combined_guard=0x%lx guardA=0x%lx guardB=0x%lx | "
              "rel32 distA=%lld [%s] distB=%lld [%s]",
              (unsigned long)combined, (unsigned long)guardA, (unsigned long)guardB,
              distA, fitsA ? "FITS" : "OVERFLOW",
              distB, fitsB ? "FITS" : "OVERFLOW");

    if (!fitsA || !fitsB) {
        Log::Warn("SLS-ach: a rel32 is OUT OF RANGE — refusing to patch");
        return false;
    }

    // Repoint both `call isSubscribed` rel32s → sls_ach_combined_guard. The jne
    // that follows is untouched and now returns only for genuinely-owned games.
    //
    // Default-on (v0.16.5) after full on-device validation via LUMA_SLS_ACH_ARM
    // (v0.16.4): combined_guard runs crash-free through all four steps, native
    // achievements fire for AdditionalApps games (appid=1454400 sub=1 added=1 ->
    // skip=0) and genuinely-owned games are untouched (appid=22380/2371090 sub=1
    // added=0 -> skip=1). The cdecl `this`-on-stack convention and g_config `this`
    // are both confirmed correct at runtime. LUMA_NO_SLS_ACH_UNBLOCK=1 disables
    // this entirely (checked at the top of Apply); LUMA_SLS_ACH_TRACE=1 turns the
    // per-call guard trace back on (off by default to avoid log spam).
    const uint32_t relA = static_cast<uint32_t>(combined - (guardA + 5));
    const uint32_t relB = static_cast<uint32_t>(combined - (guardB + 5));
    if (!WriteRel32(guardA + 1, relA)) return false;
    if (!WriteRel32(guardB + 1, relB)) return false;

    Log::Info("SLS-ach: scoped the native-achievement guard (GetUserStats@0x%lx, "
              "GetPlayerStats@0x%lx) -> combined_guard 0x%lx. AdditionalApps games "
              "get native achievements; owned games untouched.",
              (unsigned long)guardA, (unsigned long)guardB, (unsigned long)combined);
    return true;
}

} // namespace SlsAchievementUnblock
