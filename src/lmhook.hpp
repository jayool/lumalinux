#pragma once

// Hook installer using libmem (LM_HookCode) plus a get_pc_thunk fixup for the
// trampoline — the same proven approach SLSsteam uses to hook hot, PIC-prologue
// functions (e.g. CProtoBufMsgBase::InitFromPacket) without corrupting the GOT
// register or racing concurrent callers.
//
// LM_HookCode handles the entry patch + prologue relocation; LmHook then
// rewrites any relocated `call __i686.get_pc_thunk.REG` in the trampoline into
// `mov REG, <original_return_address>` so REG (the GOT base) stays correct.

#include <cstdint>

namespace LmHook {

// Hook `target`, redirecting to `hookFn`. On success, *outTrampoline is a
// callable pointer to the original (with the get_pc_thunk fixup applied).
bool Install(uintptr_t target, void* hookFn, void** outTrampoline);

} // namespace LmHook
