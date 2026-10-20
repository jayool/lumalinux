// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 jayool — https://github.com/jayool/lumalinux
#pragma once
// x86-64 trampoline fix-up: libmem's LM_HookCode copies the detoured prologue
// bytes into the trampoline VERBATIM. An instruction that addresses memory
// relative to rip (`mov rax,[rip+disp32]`, the first instruction of the 64-bit
// client's GetShaderCacheDepot) then reads from the wrong place — the
// displacement was computed for the original address, not the trampoline's.
// Measured 2026-10-20 (tools/rip_fixup_selftest.cpp): raw copy, SIGSEGV on the
// first call through the trampoline.
//
// This header is the pure logic, shared by src/lmhook.cpp and the offline
// selftest: walk the relocated bytes, and for every rip-relative instruction
// recompute disp32 so it points at the same absolute address from the new
// location. Fail-closed: a relocated instruction this cannot rebase (a rip
// operand plus an immediate, where the disp32 is not the last four bytes; or
// a relative jump/call whose target is an immediate) returns Unfixable, and
// the caller must UNHOOK rather than leave a trampoline that will fault.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <climits>

namespace RipFixupCore {

struct Inst {
    uintptr_t   address;
    unsigned    size;
    const char* mnemonic;
    const char* op_str;
};

enum class Verdict { None, Fixed, Unfixable };

inline bool HasRipOperand(const char* op) {
    return op && std::strstr(op, "rip") != nullptr;
}

// ", 0x…" / ", -0x…" / ", 12" after the memory operand: an immediate follows
// the displacement in the encoding, so the disp32 is NOT the last four bytes.
inline bool ImmediateAfterMemory(const char* op) {
    const char* rb = op ? std::strchr(op, ']') : nullptr;
    if (!rb) return false;
    const char* c = std::strchr(rb, ',');
    if (!c) return false;
    ++c;
    while (*c == ' ') ++c;
    return (*c >= '0' && *c <= '9') || *c == '-';
}

// A relative branch copied verbatim lands at the wrong place too. `jmp`/`call`
// with a memory operand ([rip+x]) is handled by the rip path; with an immediate
// target it is refused.
inline bool IsRelativeBranch(const char* mnemonic, const char* op) {
    if (!mnemonic) return false;
    const bool branch = mnemonic[0] == 'j' || std::strcmp(mnemonic, "call") == 0
                        || std::strncmp(mnemonic, "loop", 4) == 0;
    return branch && !(op && std::strchr(op, '['));
}

// New disp32 for an instruction of `size` bytes copied from fnInsn to
// trampInsn (disp32 at the last four bytes). False when it does not fit.
inline bool Rebase(uintptr_t fnInsn, uintptr_t trampInsn, unsigned size,
                   int32_t oldDisp, int32_t* newDisp) {
    const intptr_t target = static_cast<intptr_t>(fnInsn + size) + oldDisp;
    const intptr_t delta  = target - static_cast<intptr_t>(trampInsn + size);
    if (delta > INT32_MAX || delta < INT32_MIN) return false;
    *newDisp = static_cast<int32_t>(delta);
    return true;
}

// disasm(addr, Inst*) -> bool; read4(addr, int32_t*) ; write4(addr, int32_t)
// log(const char*) for the diagnostic line. Walks [tramp, tramp+n).
template <class DisasmFn, class Read4Fn, class Write4Fn, class LogFn>
Verdict FixRelocated(uintptr_t fn, uintptr_t tramp, std::size_t n,
                     DisasmFn disasm, Read4Fn read4, Write4Fn write4, LogFn log) {
    Verdict v = Verdict::None;
    std::size_t off = 0;
    while (off < n) {
        Inst in{};
        if (!disasm(tramp + off, &in) || in.size == 0) {
            log("disassembly failed inside the relocated prologue");
            return Verdict::Unfixable;
        }
        const bool rip = HasRipOperand(in.op_str);
        if (!rip && IsRelativeBranch(in.mnemonic, in.op_str)) {
            log("relocated relative branch cannot be rebased");
            return Verdict::Unfixable;
        }
        if (rip) {
            if (ImmediateAfterMemory(in.op_str) || in.size < 5) {
                log("rip-relative instruction with an immediate cannot be rebased");
                return Verdict::Unfixable;
            }
            const uintptr_t dispAt = tramp + off + in.size - 4;
            int32_t oldDisp = 0, newDisp = 0;
            read4(dispAt, &oldDisp);
            if (!Rebase(fn + off, tramp + off, in.size, oldDisp, &newDisp)) {
                log("rip-relative target is more than 2 GB from the trampoline");
                return Verdict::Unfixable;
            }
            write4(dispAt, newDisp);
            v = Verdict::Fixed;
        }
        off += in.size;
    }
    return v;
}

} // namespace RipFixupCore
