#include "cr_stats_fix.hpp"
#include "log.hpp"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <link.h>
#include <mutex>
#include <sys/uio.h>
#include <unistd.h>
#include <unordered_set>

// See cr_stats_fix.hpp for the why. This file is the how:
//   * an i386 assembly stub that IS `StatsHandlers::HandleGetUserStats` as far
//     as the dynamic linker is concerned (exact mangled name, default
//     visibility despite -fvisibility=hidden — asm symbols aren't subject to it);
//   * three small C helpers the stub calls: resolve the real function, post-
//     process a result, and bail out loudly if forwarding is impossible.
//
// The stub knows NOTHING about CloudRedirect's types. It forwards the three
// stack slots it received (hidden result pointer, appId, fields&) to the real
// function and, on return, hands the result pointer to cr_stats_fix_post, which
// only ever looks at the first 12 bytes — a libstdc++ std::vector<uint8_t>
// {begin, end, cap} — and only after proving they are readable and consistent.

namespace {

constexpr const char* kSymbol =
    "_ZN13StatsHandlers18HandleGetUserStatsEjRKSt6vectorIN2PB5FieldESaIS2_EE";
constexpr const char* kVersionSymbol = "CR_GetVersion";

// The crc-only "up to date" body for an EMPTY store: protobuf field 2
// (crc_stats), varint wire type, value 0. Tag = (2 << 3) | 0 = 0x10.
constexpr uint8_t kEmptyBody[2] = {0x10, 0x00};

std::atomic<void*> g_real{nullptr};   // CloudRedirect's HandleGetUserStats
std::atomic<bool>  g_active{false};   // all self-checks passed
std::mutex g_seenMutex;
std::unordered_set<uint32_t> g_seen;  // apps already logged (once per app)

// libstdc++ std::vector<uint8_t> — ABI-stable since forever, and the first
// member of CloudRedirect's PB::Writer, itself the first member of RpcResult.
struct VecRaw { uint8_t* begin; uint8_t* end; uint8_t* cap; };

// Can [p, p+n) be read? process_vm_readv on our own pid copies through the
// kernel and fails with EFAULT on unmapped / unreadable memory instead of
// faulting us. Any failure (ENOSYS, EPERM, short read) counts as "no".
bool Readable(const void* p, size_t n) {
    if (!p || n == 0 || n > 64) return false;
    uint8_t scratch[64];
    iovec local{scratch, n};
    iovec remote{const_cast<void*>(p), n};
    const ssize_t got = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
    return got == static_cast<ssize_t>(n);
}

} // namespace

extern "C" {

// Our stub's own address. NOT taken through the interposable name: in PIC code
// `&<default-visibility symbol>` goes through the GOT and yields whatever the
// process BOUND — which, if cloud_redirect.so preceded us, is CloudRedirect's
// copy, not ours. `cr_stats_fix_stub` is a hidden alias of the stub (same
// address, resolved locally), so this is always genuinely ours.
__attribute__((visibility("hidden")))
void cr_stats_fix_stub();

// Stub entry: the real function, resolved lazily the first time (the stub may
// fire before InstallHooks() if Steam ever asked for stats that early). RTLD_NEXT
// is relative to the DSO containing THIS function, i.e. liblumalinux.so, so it
// finds cloud_redirect.so's definition (which follows us in LD_PRELOAD) and never
// our own stub.
__attribute__((visibility("hidden")))
void* cr_stats_fix_resolve() {
    void* p = g_real.load(std::memory_order_acquire);
    if (p) return p;
    dlerror();
    p = dlsym(RTLD_NEXT, kSymbol);
    if (p) g_real.store(p, std::memory_order_release);
    return p;
}

// Stub exit: the real function has filled *sret. Clear the body ONLY if it is
// the 2-byte crc-0 "up to date" answer of an empty store, so CloudRedirect's
// TryHandleGetUserStats sees Size()==0 and takes its own passthrough branch.
__attribute__((visibility("hidden")))
void cr_stats_fix_post(void* sret, uint32_t appId) {
    if (!g_active.load(std::memory_order_acquire)) return;
    if (!Readable(sret, sizeof(VecRaw))) return;
    auto* v = static_cast<VecRaw*>(sret);
    const uint8_t* b = v->begin;
    const uint8_t* e = v->end;
    const uint8_t* c = v->cap;
    if (reinterpret_cast<uintptr_t>(b) < 4096) return;      // null / near-null
    if (e < b || c < e) return;                             // not a live vector
    if (static_cast<size_t>(e - b) != sizeof(kEmptyBody)) return;
    if (static_cast<size_t>(c - b) > 64) return;            // a 2-byte body never has a big buffer
    if (!Readable(b, sizeof(kEmptyBody))) return;
    if (std::memcmp(b, kEmptyBody, sizeof(kEmptyBody)) != 0) return;

    v->end = v->begin;   // == body.clear(): no reallocation, no ownership change

    bool first = false;
    {
        std::lock_guard<std::mutex> lock(g_seenMutex);
        first = g_seen.insert(appId).second;
    }
    if (first)
        Log::Info("CR-stats: app=%u — CloudRedirect's store is empty (crc 0) and would "
                  "have answered \"up to date\" with no schema; cleared so the request "
                  "passes through to SLSsteam/Steam (logged once per app)", appId);
}

// Stub dead end: entered, but nothing after us in the lookup chain defines the
// symbol. Cannot happen while cloud_redirect.so is loaded (it only references
// the symbol because it defines it), and returning would hand the caller an
// unconstructed result — so fail loudly instead of corrupting Steam silently.
__attribute__((visibility("hidden"), noreturn))
void cr_stats_fix_unreachable() {
    Log::Error("CR-stats: HandleGetUserStats stub entered but no real definition "
               "follows lumalinux in the lookup chain — aborting rather than "
               "returning garbage");
    std::abort();
}

} // extern "C"

// The stub. i386 cdecl with an sret result: on entry the stack is
//   [esp+0] return address   [esp+4] hidden result ptr   [esp+8] appId
//   [esp+12] const std::vector<PB::Field>&
// and the callee pops the hidden pointer (`ret $4`) with it also in %eax —
// exactly what the real function does (verified: `ret $4` epilogues, see
// Init). Stack alignment: GCC's i386 ABI wants %esp ≡ 0 (mod 16) at every
// call; entry is ≡ 12, `push %ebp` makes it ≡ 8, and the sub/pushes below are
// sized so each call site lands on ≡ 0. Only caller-saved registers are used.
asm(R"(
    .text
    .globl _ZN13StatsHandlers18HandleGetUserStatsEjRKSt6vectorIN2PB5FieldESaIS2_EE
    .type  _ZN13StatsHandlers18HandleGetUserStatsEjRKSt6vectorIN2PB5FieldESaIS2_EE, @function
    .globl cr_stats_fix_stub
    .hidden cr_stats_fix_stub
    .type  cr_stats_fix_stub, @function
_ZN13StatsHandlers18HandleGetUserStatsEjRKSt6vectorIN2PB5FieldESaIS2_EE:
cr_stats_fix_stub:
    pushl %ebp
    movl  %esp, %ebp
    subl  $8, %esp
    call  cr_stats_fix_resolve@PLT
    addl  $8, %esp
    testl %eax, %eax
    jz    1f
    subl  $12, %esp
    pushl 16(%ebp)
    pushl 12(%ebp)
    pushl 8(%ebp)
    call  *%eax
    addl  $20, %esp
    pushl 12(%ebp)
    pushl 8(%ebp)
    call  cr_stats_fix_post@PLT
    addl  $8, %esp
    movl  8(%ebp), %eax
    popl  %ebp
    ret   $4
1:
    subl  $8, %esp
    call  cr_stats_fix_unreachable@PLT
    .size _ZN13StatsHandlers18HandleGetUserStatsEjRKSt6vectorIN2PB5FieldESaIS2_EE, .-_ZN13StatsHandlers18HandleGetUserStatsEjRKSt6vectorIN2PB5FieldESaIS2_EE
    .section .note.GNU-stack,"",@progbits
)");

namespace CrStatsFix {

Result Init() {
    if (std::getenv("LUMA_NO_CR_STATS_FIX")) {
        Log::Info("CR-stats: disabled via LUMA_NO_CR_STATS_FIX (stub forwards untouched)");
        return Result::Disabled;
    }

    void* ours = reinterpret_cast<void*>(&cr_stats_fix_stub);

    void* real = cr_stats_fix_resolve();
    if (!real) {
        // Nothing AFTER us defines it. Either CloudRedirect isn't loaded, or it
        // is loaded but ahead of us in the lookup order (LD_PRELOAD reordered):
        // then its PLT binds to itself, our stub is never called, and the fix
        // is inert — report that as a failure, not as "not loaded".
        void* any = dlsym(RTLD_DEFAULT, kSymbol);
        if (any && any != ours) {
            Dl_info ai{};
            const char* where = (dladdr(any, &ai) && ai.dli_fname) ? ai.dli_fname : "?";
            Log::Warn("CR-stats: FAILED — HandleGetUserStats is bound from %s, which precedes "
                      "lumalinux in the lookup order; interposition inactive (is "
                      "cloud_redirect.so before liblumalinux.so in LD_PRELOAD?)", where);
            return Result::Failed;
        }
        Log::Info("CR-stats: cloud_redirect.so not loaded (or it no longer defines "
                  "HandleGetUserStats) — nothing to interpose");
        return Result::Disabled;
    }

    // Check 1b: the definition we forward to really is CloudRedirect's.
    Dl_info info{};
    ElfW(Sym)* sym = nullptr;
    if (!dladdr1(real, &info, reinterpret_cast<void**>(&sym), RTLD_DL_SYMENT) ||
        !info.dli_fname) {
        Log::Warn("CR-stats: FAILED — dladdr1 cannot describe %p", real);
        return Result::Failed;
    }
    if (!std::strstr(info.dli_fname, "cloud_redirect")) {
        Log::Warn("CR-stats: FAILED — HandleGetUserStats resolved into %s, not cloud_redirect.so",
                  info.dli_fname);
        return Result::Failed;
    }

    // Check 3: the process binds OUR stub (we precede cloud_redirect.so in
    // LD_PRELOAD). If not, CloudRedirect's PLT already points at itself and
    // nothing we do matters — say so instead of pretending.
    void* bound = dlsym(RTLD_DEFAULT, kSymbol);
    if (bound != ours) {
        Dl_info bi{};
        const char* where = (bound && dladdr(bound, &bi) && bi.dli_fname) ? bi.dli_fname : "?";
        Log::Warn("CR-stats: FAILED — process binds HandleGetUserStats from %s, not from "
                  "lumalinux (is cloud_redirect.so before liblumalinux.so in LD_PRELOAD?)",
                  where);
        return Result::Failed;
    }

    // Check 2: the real function still returns via a callee-popped hidden
    // pointer. Its .dynsym st_size bounds the scan; an i386 sret function ends
    // in `ret $4` (c2 04 00) — a plain-value one ends in `ret` (c3).
    const size_t size = sym ? sym->st_size : 0;
    if (size == 0 || size > (1u << 20)) {
        Log::Warn("CR-stats: FAILED — HandleGetUserStats has no usable symbol size (%zu)", size);
        return Result::Failed;
    }
    bool hasRet4 = false;
    {
        const auto* code = static_cast<const uint8_t*>(real);
        for (size_t i = 0; i + 3 <= size; ++i) {
            if (code[i] == 0xC2 && code[i + 1] == 0x04 && code[i + 2] == 0x00) {
                hasRet4 = true;
                break;
            }
        }
    }
    if (!hasRet4) {
        Log::Warn("CR-stats: FAILED — HandleGetUserStats (%zu bytes) has no `ret $4` epilogue; "
                  "its return convention changed, not arming", size);
        return Result::Failed;
    }

    // Probe the readability check itself once, on memory we own: if
    // process_vm_readv is unavailable here, every post-process would silently
    // decline, which is safe but worth knowing.
    if (!Readable(&g_active, 1)) {
        Log::Warn("CR-stats: FAILED — process_vm_readv self-probe failed (errno %d); "
                  "cannot validate results safely, not arming", errno);
        return Result::Failed;
    }

    const char* ver = "?";
    if (auto fn = reinterpret_cast<const char* (*)()>(dlsym(RTLD_DEFAULT, kVersionSymbol))) {
        if (const char* s = fn()) ver = s;
    }

    g_active.store(true, std::memory_order_release);
    Log::Info("CR-stats: armed — CloudRedirect %s, HandleGetUserStats @ %p (%zu bytes, "
              "sret ok), bound from lumalinux; empty-store answers will pass through",
              ver, real, size);
    return Result::Applied;
}

} // namespace CrStatsFix
