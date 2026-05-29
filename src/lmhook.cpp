#include "lmhook.hpp"
#include "log.hpp"

#include "libmem/libmem.h"

#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::vector<std::string> SplitOn(const char* s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (const char* p = s; *p; ++p) {
        if (*p == sep) { out.push_back(cur); cur.clear(); }
        else cur.push_back(*p);
    }
    out.push_back(cur);
    return out;
}

// Port of SLSsteam's MemHlp::fixPICThunkCall. After LM_HookCode relocates the
// prologue into the trampoline, any `call __i686.get_pc_thunk.REG` it copied
// returns the WRONG address (trampoline location, not the original), corrupting
// REG (the GOT base). We find that call in the trampoline and replace it with
// `mov REG, <address it would have returned to in the original function>`.
bool FixPicThunk(uintptr_t fn, uintptr_t tramp) {
    constexpr unsigned kMaxBytes = 0x5;  // minimum detour size; thunk lives within
    lm_inst_t inst;

    for (unsigned off = 0; off <= kMaxBytes; ) {
        lm_address_t start = static_cast<lm_address_t>(tramp + off);
        if (!LM_Disassemble(start, &inst)) {
            Log::Debug("LmHook: disasm failed at 0x%lx", (unsigned long)(tramp + off));
            return false;
        }
        off += inst.size;

        if (std::strcmp(inst.mnemonic, "call") != 0) continue;

        // Where this call would land in the ORIGINAL function.
        lm_address_t followAddr =
            static_cast<lm_address_t>(fn + off + *reinterpret_cast<int32_t*>(start + 1));

        bool isThunk = true;
        char newInstr[256] = {0};

        for (unsigned i = 0; i < 2; ++i) {  // thunk = `mov REG,[esp]` ; `ret`
            if (!LM_Disassemble(followAddr, &inst)) return false;
            followAddr += inst.size;

            if (i == 0) {
                if (std::strcmp(inst.mnemonic, "mov") != 0) { isThunk = false; break; }
                auto parts = SplitOn(inst.op_str, ',');  // "edi, dword ptr [esp]"
                lm_address_t retAddr = static_cast<lm_address_t>(fn + off);
                std::snprintf(newInstr, sizeof(newInstr), "%s %s, %p",
                              inst.mnemonic, parts.at(0).c_str(),
                              reinterpret_cast<void*>(retAddr));
            } else {  // i == 1
                if (std::strcmp(inst.mnemonic, "ret") != 0) { isThunk = false; break; }
            }
        }
        if (!isThunk) continue;

        if (!LM_Assemble(newInstr, &inst)) {
            Log::Error("LmHook: failed to assemble '%s'", newInstr);
            return false;
        }

        lm_prot_t oldProt;
        LM_ProtMemory(start, inst.size, LM_PROT_XRW, &oldProt);
        LM_WriteMemory(start, inst.bytes, inst.size);
        LM_ProtMemory(start, inst.size, oldProt, nullptr);
        Log::Info("LmHook: rewrote PIC thunk in trampoline -> '%s'", newInstr);
        return true;
    }
    return false;
}

// If the target function's first byte is 0xE9, its prologue is already a
// 5-byte `jmp rel32` placed by another hooker (e.g. SLSsteam's inline
// detour). libmem copies those 5 bytes verbatim into our trampoline but
// does NOT adjust the rel32 — so the trampoline's jmp would land at the
// wrong address. Re-compute the rel32 so the trampoline still jumps to
// the same absolute target.
//
//   T + 5 + new_rel32 == O + 5 + old_rel32   ⇒   new_rel32 = old_rel32 + (O - T)
//
// Only touches offset 0 of the trampoline. The jmp-back libmem appends at
// the END of the relocated prologue is left alone (libmem computes it
// from scratch and gets that one right).
//
// Verified in /tmp/relocator-test/test_relocator.cpp (arithmetic + live
// execution) before being integrated here.
bool RelocateChainedJmp(uintptr_t fn, uintptr_t tramp) {
    // Defensive: trampoline holds the ORIGINAL bytes of fn (libmem copied
    // them verbatim before overwriting the prologue with our hook jmp). If
    // those originals don't start with 0xE9, this isn't a chained scenario
    // and we must NOT touch the rel32 — doing so corrupts whatever real
    // instruction libmem relocated (e.g. 55 89 e5 ... -> the 0x55 stays
    // but the next 4 bytes would be mangled). Checking the trampoline is
    // safer than checking fn because fn might have been mutated by us.
    if (*reinterpret_cast<const volatile uint8_t*>(tramp) != 0xE9) return false;

    int32_t old_rel32 = *reinterpret_cast<const volatile int32_t*>(tramp + 1);
    int32_t new_rel32 = old_rel32 + static_cast<int32_t>(
        static_cast<intptr_t>(fn) - static_cast<intptr_t>(tramp));

    lm_address_t writeAddr = static_cast<lm_address_t>(tramp + 1);
    lm_prot_t oldProt;
    LM_ProtMemory(writeAddr, 4, LM_PROT_XRW, &oldProt);
    LM_WriteMemory(writeAddr, reinterpret_cast<lm_byte_t*>(&new_rel32), 4);
    LM_ProtMemory(writeAddr, 4, oldProt, nullptr);

    Log::Info("LmHook: relocated chained jmp at trampoline 0x%lx: "
              "delta=%+ld, target=0x%lx",
              (unsigned long)tramp,
              (long)(static_cast<intptr_t>(fn) - static_cast<intptr_t>(tramp)),
              (unsigned long)(tramp + 5 + static_cast<uintptr_t>(static_cast<intptr_t>(new_rel32))));
    return true;
}

} // namespace

namespace LmHook {

bool Install(uintptr_t target, void* hookFn, void** outTrampoline) {
    // CRITICAL: capture the ORIGINAL first byte BEFORE LM_HookCode runs.
    // After LM_HookCode overwrites the prologue with our own jmp, *target
    // becomes 0xE9 trivially — so checking it later would always trigger
    // the chained-jmp branch, even when the function was originally NOT
    // pre-hooked. The trampoline preserves the original bytes too
    // (libmem copies them verbatim), so tramp[0] is the same source of
    // truth; either check works. We use the pre-LM_HookCode read here
    // for clarity of intent.
    bool already_hooked = (*reinterpret_cast<volatile uint8_t*>(target) == 0xE9);

    lm_address_t tramp = LM_ADDRESS_BAD;
    lm_size_t size = LM_HookCode(static_cast<lm_address_t>(target),
                                 reinterpret_cast<lm_address_t>(hookFn),
                                 &tramp);
    if (size == 0 || tramp == LM_ADDRESS_BAD) {
        Log::Error("LmHook: LM_HookCode failed for 0x%lx", (unsigned long)target);
        return false;
    }

    // Branch on prologue kind: a chained jmp prologue (already hooked by
    // a third party) needs rel32 relocation; the original PIC prologue
    // needs the get_pc_thunk fixup. The two are mutually exclusive.
    if (already_hooked) {
        RelocateChainedJmp(target, static_cast<uintptr_t>(tramp));
    } else {
        FixPicThunk(target, static_cast<uintptr_t>(tramp));
    }

    *outTrampoline = reinterpret_cast<void*>(tramp);
    Log::Info("LmHook: hooked 0x%lx (size=%zu tramp=0x%lx)",
              (unsigned long)target, (size_t)size, (unsigned long)tramp);
    return true;
}

} // namespace LmHook
