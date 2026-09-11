#include "shader_depot_hook.hpp"
#include "../patterns.hpp"
#include "../rva_feed.hpp"
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
// Since v0.20.0 the skip covers EVERY lumalinux-managed game, keyed or not.
// The shader manifest is never in a Hubcap zip, so with a key the job still has
// to ask Valve for a manifest request code, and Valve only grants that for apps
// whose depot <appid> is public (games with a Steam Workshop — the workshop and
// shader depots share the id) and denies it for the rest. There is no public
// request-code provider left to fill the gap (all died 2026-09-09), and the
// denied path is not free: Steam shows "No internet connection", stalls the
// install for 30 s, and only then goes on without shaders. Returning 0 up front
// gives the same end state with no popup and no stall (verified 2026-09-10 on
// Valheim / Lethal Company in the SteamOS codespace). The cost is the precompiled shaders
// of the Workshop-carrying games (Brotato, RimWorld...), which compile at
// runtime like on any game without a cache. Owned games are never touched.
//
// See RESEARCH §13.9 for the full disassembly and why this is preferred over
// both the fragile manifest-fabrication path ("path B") and the global
// DisableShaderCache toggle (what moon writes; it kills owned games' shaders too).

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
    if (id == 0) return id;

    // Not a lumalinux-managed depot: the user's genuinely-owned games. Never
    // touch them — their shader pre-cache goes down Steam's normal owned path.
    if (!KeyStore::HasDepot(id) && !KeyStore::IsPresenceOnly(id)) return id;

    // Ours (the shader depot id == the app id, registered from the .lua with or
    // without a key): skip the pre-cache via Steam's own "invalid shader depot"
    // path. Keyless -> could never decrypt; keyed -> the manifest request code
    // is denied to us for anything without a Workshop and there is no provider
    // to fetch it from (see the file header). The game still installs from its
    // content depots, which never go through here.
    Log::Info("ShaderDepot: depot %u is lumalinux-managed (%s) -> returning 0 so "
              "Steam skips its shader pre-cache cleanly",
              id, KeyStore::IsPresenceOnly(id) ? "keyless" : "keyed");
    return 0;
}

} // namespace

namespace Hooks::ShaderDepot {

bool Install() {
    // RVA feed first (prologue-independent), else the unique-match byte pattern.
    const char* method = "rva";
    uintptr_t target = RvaFeed::Resolve("ShaderDepot");
    if (!target) { method = "pattern"; target = Patterns::FindShaderCacheDepotFunction(); }
    if (!target) {
        Log::Warn("ShaderDepot hook: GetShaderCacheDepot not found — "
                  "per-game shader skip not installed (non-fatal; keyless shader "
                  "pre-cache will behave as before — fall back to the global "
                  "DisableShaderCache if it loops). See RESEARCH §13.9.");
        Log::Warn("Hook install: name=ShaderDepot method=%s outcome=miss", method);
        return false;
    }
    void* tramp = nullptr;
    if (!LmHook::Install(target, reinterpret_cast<void*>(&HookFn), &tramp)) {
        Log::Error("ShaderDepot hook: LmHook::Install failed (target=0x%lx)",
                   (unsigned long)target);
        Log::Warn("Hook install: name=ShaderDepot method=%s target=0x%lx outcome=hook_install_failed",
                  method, (unsigned long)target);
        return false;
    }
    g_origFn = reinterpret_cast<ShaderDepotFn>(tramp);

    Log::Info("ShaderDepot hook: INSTALLED (per-game shader skip, target=0x%lx, "
              "trampoline=%p)", (unsigned long)target, (void*)g_origFn);
    uintptr_t base = Patterns::FindSteamclientBase();
    Log::Info("Hook install: name=ShaderDepot method=%s target=0x%lx rva=0x%lx outcome=installed",
              method, (unsigned long)target, (unsigned long)(base ? target - base : 0));
    return true;
}

void Uninstall() { g_origFn = nullptr; }

} // namespace Hooks::ShaderDepot
