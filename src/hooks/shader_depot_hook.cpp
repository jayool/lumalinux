#include "shader_depot_hook.hpp"
#include "../patterns.hpp"
#include "../key_store.hpp"
#include "../lmhook.hpp"
#include "../log.hpp"

#include <cstdint>

namespace {

// ── Function signature ──────────────────────────────────────────────────────
//
//   uint32_t GetShaderCacheDepot(void* appinfo);   // cdecl, i386, 1 stack arg
//
// Returns the shader-cache depot id read from the game's PICS appinfo
// (`appinfo.game.shadercachedepot`, == the app id by Steam convention), or 0 if
// the app is not a game / has no shader depot. Its ONLY caller is
// CGetShaderDepotManifestJob::BYieldingRunClientJob:
//
//     id = GetShaderCacheDepot(appinfo);
//     if (id == 0) -> "skipping because shader depot ID is invalid", return clean
//     else         -> "Getting shader depot manifests" -> download + decrypt
//
// For a keyless game the download+decrypt can never succeed (we hold no shader
// key), which is what produces the "Invalid content configuration" install
// suspend / the recurring "Missing decryption key" loop (RESEARCH §13.8).
// Returning 0 for exactly those games routes them down Steam's own clean skip.
//
// See RESEARCH §13.9 for the full disassembly and why this is preferred over
// both the fragile manifest-fabrication path ("path B") and the global
// DisableShaderCache toggle.

using ShaderDepotFn = uint32_t (*)(void* /*appinfo*/);

ShaderDepotFn g_origFn = nullptr;

uint32_t HookFn(void* appinfo) {
    if (!g_origFn) {
        // Cannot happen post-install (the trampoline is set before the hook is
        // reachable). Returning 0 here would skip the shader pre-cache for
        // whatever app is being queried, so only do it as a last resort.
        Log::Error("ShaderDepot: no original fn pointer — should never happen");
        return 0;
    }

    const uint32_t id = g_origFn(appinfo);

    // Skip the shader pre-cache for a base/placeholder depot we manage. The
    // shader-cache depot id is the app id by Steam convention, and for a
    // lumalinux-added game that base depot holds no shader content we can serve,
    // while its manifest is only obtainable via GMRC — which fails when
    // gmrc.wudrm.com 502s and surfaces as the "No internet connection" popup
    // (e.g. Silksong's depot 1030300, confirmed on-device: the probe is this
    // shader job, NOT BuildDepotDependency). Returning 0 routes Steam down its
    // own clean "shader depot ID invalid -> skip" path.
    //
    // Managed base = present in our keystore but NOT a content depot
    // (parent_app_id == 0). This covers BOTH keyless (presence-only) bases AND
    // keyed ones: Hubcap gives some bases a real key (addappid(APP,1,key)), so
    // the old IsPresenceOnly-only check let those through — that was the bug.
    // Genuinely-owned games aren't in the store, so they pass through and their
    // shaders pre-cache normally.
    if (id != 0 && KeyStore::HasDepot(id) && !KeyStore::IsContentDepot(id)) {
        Log::Info("ShaderDepot: depot %u is a managed base placeholder -> returning "
                  "0 so Steam skips its shader pre-cache (no content; its manifest "
                  "code isn't reliably fetchable via GMRC)", id);
        return 0;
    }
    return id;
}

} // namespace

namespace Hooks::ShaderDepot {

bool Install() {
    uintptr_t target = Patterns::FindShaderCacheDepotFunction();
    if (!target) {
        Log::Warn("ShaderDepot hook: GetShaderCacheDepot pattern not found — "
                  "per-game shader skip not installed (non-fatal; keyless shader "
                  "pre-cache will behave as before — fall back to the global "
                  "DisableShaderCache if it loops). See RESEARCH §13.9.");
        Log::Warn("Hook install: name=ShaderDepot method=pattern outcome=pattern_miss");
        return false;
    }
    void* tramp = nullptr;
    if (!LmHook::Install(target, reinterpret_cast<void*>(&HookFn), &tramp)) {
        Log::Error("ShaderDepot hook: LmHook::Install failed (target=0x%lx)",
                   (unsigned long)target);
        Log::Warn("Hook install: name=ShaderDepot method=pattern target=0x%lx outcome=hook_install_failed",
                  (unsigned long)target);
        return false;
    }
    g_origFn = reinterpret_cast<ShaderDepotFn>(tramp);

    Log::Info("ShaderDepot hook: INSTALLED (per-game shader skip, target=0x%lx, "
              "trampoline=%p)", (unsigned long)target, (void*)g_origFn);
    uintptr_t base = Patterns::FindSteamclientBase();
    Log::Info("Hook install: name=ShaderDepot method=pattern target=0x%lx rva=0x%lx outcome=installed",
              (unsigned long)target, (unsigned long)(base ? target - base : 0));
    return true;
}

void Uninstall() { g_origFn = nullptr; }

} // namespace Hooks::ShaderDepot
