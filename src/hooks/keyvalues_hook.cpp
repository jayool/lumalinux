#include "keyvalues_hook.hpp"
#include "../patterns.hpp"
#include "../log.hpp"

#include <subhook.h>

#include <atomic>
#include <cstdint>
#include <cstring>

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


void DumpHex(const char* label, const void* p, size_t n) {
    if (!p) { Log::Debug("    %s: <null>", label); return; }
    const uint8_t* b = static_cast<const uint8_t*>(p);
    // Print in chunks of 16 bytes
    char line[80];
    for (size_t off = 0; off < n; off += 16) {
        char* w = line;
        for (size_t i = 0; i < 16 && off + i < n; ++i) {
            w += std::snprintf(w, sizeof(line) - (w - line), "%02X ", b[off + i]);
        }
        // ASCII column
        *w++ = ' ';
        for (size_t i = 0; i < 16 && off + i < n; ++i) {
            uint8_t c = b[off + i];
            *w++ = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
        }
        *w = 0;
        Log::Debug("    %s+%02zx: %s", label, off, line);
    }
}


bool HookFn(void* this_root, void* buf, int depth, int textMode, void* symTable) {
    if (!g_origFn) return false;

    uint64_t n = g_callCount.fetch_add(1) + 1;
    bool isTop = (depth == 0);

    // Snapshot of buffer BEFORE original runs.
    uint8_t buf_snapshot[128];
    uint8_t data_snapshot[256];
    size_t  data_snapshot_len = 0;
    void*   data_ptr = nullptr;

    if (isTop && buf) {
        std::memcpy(buf_snapshot, buf, sizeof(buf_snapshot));
        // The first 4 bytes of buf are a pointer to the actual binary KV
        // stream Steam is reading. Deref it for the real data.
        data_ptr = *reinterpret_cast<void**>(buf);
        if (data_ptr) {
            std::memcpy(data_snapshot, data_ptr, sizeof(data_snapshot));
            data_snapshot_len = sizeof(data_snapshot);
        }
    }

    if (isTop) {
        g_topLevelCount.fetch_add(1);
        Log::Debug("ReadAsBinary[#%llu top=%llu]: this=%p buf=%p depth=%d textMode=%d symTable=%p data=%p",
                   (unsigned long long)n,
                   (unsigned long long)(g_topLevelCount.load()),
                   this_root, buf, depth, textMode, symTable, data_ptr);
        // Dump KV root (first 48 bytes)
        DumpHex("kv  ", this_root, 48);
        // Dump buffer descriptor (first 128 bytes)
        DumpHex("buf ", buf_snapshot, 128);
        // Dump actual binary KV stream (first 256 bytes) — what Steam is about to parse
        DumpHex("data", data_snapshot, data_snapshot_len);
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
