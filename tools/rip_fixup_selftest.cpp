// SPDX-License-Identifier: GPL-3.0-only
// rip_fixup_selftest.cpp — offline proof for src/rip_fixup_core.hpp against the
// real libmem (x86-64). Hooks a function whose first instruction is
// `mov rax,[rip+G]` (the shape of the 64-bit client's GetShaderCacheDepot),
// calls the original through the trampoline before and after the fix-up.
// Two scenarios: [far] the fail-closed refusal, [near] the rebased trampoline.
//
//   g++ -O1 -std=c++20 -masm=intel -I src -I lib/x86_64/include \
//       tools/rip_fixup_selftest.cpp src/compat_x86_64.cpp lib/x86_64/liblibmem.a \
//       -lpthread -o /tmp/rf && /tmp/rf
// (src/compat_x86_64.cpp: the prebuilt libmem wants a libstdc++ symbol the
// runtime SDK's GCC 10 does not export — the same shim the library links.)
//
// Measured 2026-10-20 with libmem 5.1.5: LM_HookCode copies the 7 bytes verbatim
// (disp32 unchanged) and the unfixed trampoline SIGSEGVs; with FixRelocated the
// pass-through hook returns the original's values. Exit 0 = pass.
#include "libmem/libmem.h"
#include "rip_fixup_core.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>

struct Mgr { char pad[0x7c]; int v; };
static Mgr g_mgr{{}, 0};
extern "C" { Mgr* g_ptr = &g_mgr; }         // the global the first instruction reads
extern "C" __attribute__((noinline)) unsigned target(void* appinfo);
asm(R"(
.text
.globl target
.type target,@function
target:
    mov rax, qword ptr [rip + g_ptr]
    mov eax, dword ptr [rax + 0x7c]
    test eax, eax
    jne 1f
    xor eax, eax
    ret
1:  add eax, 1000
    ret
.size target, .-target
)");
using Fn = unsigned (*)(void*);
static Fn g_orig = nullptr;
static int g_calls = 0;
extern "C" unsigned hook(void* a) { ++g_calls; return g_orig(a); }

// Scenario B: the function AND its global live in an mmap'd page, like
// steamclient.so and liblumalinux.so do in the real process; libmem's
// trampoline is mmap'd too, so it lands within ±2 GB and the fix-up applies.
// The hook is a pass-through thunk in the same page (`jmp [rip+slot]`), so the
// 5-byte entry jmp reaches it like it reaches liblumalinux.so from steamclient.
#include <sys/mman.h>
static int ScenarioNear() {
    unsigned char* page = (unsigned char*)mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) { std::puts("FAIL: mmap"); return 1; }
    // target @0: mov rax,[rip+25] ; mov eax,[rax+0x7c] ; test ; jne +3 ; xor eax,eax ; ret ; add eax,1000 ; ret
    static const unsigned char code[] = {
        0x48, 0x8B, 0x05, 25, 0, 0, 0,   0x8B, 0x40, 0x7C,   0x85, 0xC0,   0x75, 0x03,
        0x31, 0xC0,   0xC3,   0x05, 0xE8, 0x03, 0x00, 0x00,   0xC3 };
    std::memcpy(page, code, sizeof(code));
    Mgr* mgr = &g_mgr;
    std::memcpy(page + 32, &mgr, 8);                       // the slot `mov rax,[rip+25]` reads
    // hook @64: jmp qword ptr [rip+10] -> slot2 @80 (the trampoline, filled below)
    static const unsigned char thunk[] = { 0xFF, 0x25, 10, 0, 0, 0 };
    std::memcpy(page + 64, thunk, sizeof(thunk));
    Fn tgt = (Fn)page;
    g_mgr.v = 7;
    if (tgt(nullptr) != 1007) { std::puts("FAIL: near target before hook"); return 1; }
    lm_address_t tramp = LM_ADDRESS_BAD;
    lm_size_t n = LM_HookCode((lm_address_t)page, (lm_address_t)(page + 64), &tramp);
    if (!n || tramp == LM_ADDRESS_BAD) { std::puts("FAIL: LM_HookCode (near)"); return 1; }
    std::printf("[near] relocated %zu bytes, trampoline %+ld bytes away\n", (size_t)n,
                (long)((intptr_t)tramp - (intptr_t)page));
    const auto v = RipFixupCore::FixRelocated(
        (uintptr_t)page, (uintptr_t)tramp, (size_t)n,
        [](uintptr_t at, RipFixupCore::Inst* out) {
            static lm_inst_t li;
            if (!LM_Disassemble((lm_address_t)at, &li)) return false;
            out->address = at; out->size = (unsigned)li.size;
            out->mnemonic = li.mnemonic; out->op_str = li.op_str; return true;
        },
        [](uintptr_t at, int32_t* x) { std::memcpy(x, (const void*)at, 4); },
        [](uintptr_t at, int32_t x) {
            lm_prot_t old; LM_ProtMemory((lm_address_t)at, 4, LM_PROT_XRW, &old);
            LM_WriteMemory((lm_address_t)at, (lm_byte_t*)&x, 4);
            LM_ProtMemory((lm_address_t)at, 4, old, nullptr);
        },
        [](const char* why) { std::printf("[near] fix-up refused: %s\n", why); });
    if (v != RipFixupCore::Verdict::Fixed) {
        std::printf("[near] verdict=%d — trampoline not within 2 GB this run; cannot prove the fixed path\n", (int)v);
        LM_UnhookCode((lm_address_t)page, tramp, n);
        return 3;
    }
    uintptr_t t = (uintptr_t)tramp; std::memcpy(page + 80, &t, 8);   // thunk -> trampoline
    g_mgr.v = 0; const unsigned r0 = tgt(nullptr);
    g_mgr.v = 7; const unsigned r7 = tgt(nullptr);
    std::printf("[near] through hook+fixed trampoline: %u (want 0), %u (want 1007)\n", r0, r7);
    const bool ok = r0 == 0 && r7 == 1007;
    std::puts(ok ? "[near] PASS" : "[near] FAIL");
    return ok ? 0 : 2;
}

int main() {
    // Scenario A: PIE binary at 0x55…, trampoline mmap'd at 0x7f…: >2 GB apart,
    // the fix-up must refuse (fail-closed) — a raw copy here SIGSEGVs.
    g_mgr.v = 7;
    if (target(nullptr) != 1007) { std::puts("FAIL: target before hook"); return 1; }
    lm_address_t tramp = LM_ADDRESS_BAD;
    lm_size_t n = LM_HookCode((lm_address_t)&target, (lm_address_t)&hook, &tramp);
    if (!n || tramp == LM_ADDRESS_BAD) { std::puts("FAIL: LM_HookCode"); return 1; }
    std::printf("[far] relocated %zu bytes, trampoline %+ld bytes away\n", (size_t)n,
                (long)((intptr_t)tramp - (intptr_t)&target));
    const auto v = RipFixupCore::FixRelocated(
        (uintptr_t)&target, (uintptr_t)tramp, (size_t)n,
        [](uintptr_t at, RipFixupCore::Inst* out) {
            static lm_inst_t li;
            if (!LM_Disassemble((lm_address_t)at, &li)) return false;
            out->address = at; out->size = (unsigned)li.size;
            out->mnemonic = li.mnemonic; out->op_str = li.op_str; return true;
        },
        [](uintptr_t at, int32_t* x) { std::memcpy(x, (const void*)at, 4); },
        [](uintptr_t at, int32_t x) {
            lm_prot_t old; LM_ProtMemory((lm_address_t)at, 4, LM_PROT_XRW, &old);
            LM_WriteMemory((lm_address_t)at, (lm_byte_t*)&x, 4);
            LM_ProtMemory((lm_address_t)at, 4, old, nullptr);
        },
        [](const char* why) { std::printf("[far] fix-up refused: %s\n", why); });
    LM_UnhookCode((lm_address_t)&target, tramp, n);
    if (v == RipFixupCore::Verdict::Unfixable) {
        std::puts("[far] PASS (refused, hook undone)");
    } else if (v == RipFixupCore::Verdict::Fixed) {
        std::puts("[far] trampoline happened to land within 2 GB — fixed instead");
    } else { std::puts("[far] FAIL: nothing happened"); return 1; }
    (void)hook; (void)g_calls; (void)g_orig;
    return ScenarioNear();
}
