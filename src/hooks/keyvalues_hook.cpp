#include "keyvalues_hook.hpp"
#include "../patterns.hpp"
#include "../log.hpp"

#include <subhook.h>

#include <atomic>
#include <cstdint>

namespace {

// ── Function signature ──────────────────────────────────────────────────────
// Linux i386 cdecl — member function (this is first stack arg).
//
//   bool KeyValues::ReadAsBinary(
//       KeyValues*  this_root,   // [ebp+0x08]
//       void*       buf,         // [ebp+0x0C]
//       int         depth,       // [ebp+0x10]
//       bool        textMode,    // [ebp+0x14]  (passed as int)
//       void*       symTable);   // [ebp+0x18]

using ReadAsBinaryFn = bool (*)(void* this_root, void* buf, int depth,
                                int textMode, void* symTable);

subhook_t      g_hook   = nullptr;
ReadAsBinaryFn g_origFn = nullptr;

std::atomic<uint64_t> g_callCount{0};
std::atomic<uint64_t> g_topLevelCount{0};


// Try to extract a printable name from the KeyValues object. Source SDK's
// CKeyValues stores the name as a string pointer (offset varies by build).
// We log the first few words of the object for inspection — we'll narrow
// down the layout from the dumps.
void DumpKVHeader(void* kv) {
    if (!kv) {
        Log::Debug("    kv: <null>");
        return;
    }
    const uint32_t* w = reinterpret_cast<const uint32_t*>(kv);
    Log::Debug("    kv@%p: [+0]=0x%08x [+4]=0x%08x [+8]=0x%08x [+12]=0x%08x [+16]=0x%08x [+20]=0x%08x",
               kv, w[0], w[1], w[2], w[3], w[4], w[5]);
}


bool HookFn(void* this_root, void* buf, int depth, int textMode, void* symTable) {
    if (!g_origFn) return false;

    uint64_t n = g_callCount.fetch_add(1) + 1;
    bool isTop = (depth == 0);
    if (isTop) {
        g_topLevelCount.fetch_add(1);
        Log::Debug("ReadAsBinary[#%llu top=%llu]: this=%p buf=%p depth=%d textMode=%d symTable=%p",
                   (unsigned long long)n,
                   (unsigned long long)(g_topLevelCount.load()),
                   this_root, buf, depth, textMode, symTable);
        DumpKVHeader(this_root);
    }

    return g_origFn(this_root, buf, depth, textMode, symTable);
}

} // namespace

namespace Hooks::KeyValues {

bool Install() {
    uintptr_t target = Patterns::FindKeyValuesReadAsBinaryFunction();
    if (!target) {
        Log::Error("KeyValues hook: target not found");
        return false;
    }

    g_hook = subhook_new(reinterpret_cast<void*>(target),
                         reinterpret_cast<void*>(&HookFn),
                         static_cast<subhook_flags_t>(0));
    if (!g_hook) {
        Log::Error("KeyValues hook: subhook_new failed");
        return false;
    }

    if (subhook_install(g_hook) != 0) {
        Log::Error("KeyValues hook: subhook_install failed (target=0x%lx)",
                   (unsigned long)target);
        subhook_free(g_hook);
        g_hook = nullptr;
        return false;
    }

    g_origFn = reinterpret_cast<ReadAsBinaryFn>(subhook_get_trampoline(g_hook));
    if (!g_origFn) {
        Log::Error("KeyValues hook: subhook_get_trampoline returned null");
        subhook_remove(g_hook);
        subhook_free(g_hook);
        g_hook = nullptr;
        return false;
    }

    Log::Info("KeyValues hook: INSTALLED (target=0x%lx, trampoline=%p) [diagnostic mode]",
              (unsigned long)target, (void*)g_origFn);
    return true;
}

void Uninstall() {
    if (!g_hook) return;
    subhook_remove(g_hook);
    subhook_free(g_hook);
    g_hook = nullptr;
    g_origFn = nullptr;
    Log::Info("KeyValues hook: uninstalled (total_calls=%llu, top_level=%llu)",
              (unsigned long long)g_callCount.load(),
              (unsigned long long)g_topLevelCount.load());
}

} // namespace Hooks::KeyValues
