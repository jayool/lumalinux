#include "shader_depot_hook.hpp"
#include "../patterns.hpp"
#include "../rva_feed.hpp"
#include "../key_store.hpp"
#include "../gmrc_store.hpp"
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
    if (id == 0) return id;

    // (1) OUR keyless games (presence-only in keys.txt): the shader depot's
    // manifest is encrypted with a key we don't have, so the pre-cache can NEVER
    // succeed. Skip cleanly — the original v0.14 behaviour.
    if (KeyStore::IsPresenceOnly(id)) {
        Log::Info("ShaderDepot: depot %u is keyless (presence-only) "
                  "-> returning 0 so Steam skips its shader pre-cache cleanly", id);
        return 0;
    }

    // (2) Not a lumalinux-managed depot: the user's genuinely-owned games. Never
    // touch them — their shader pre-cache goes down Steam's normal owned path.
    if (!KeyStore::HasDepot(id)) return id;

    // (3) A KEYED, lumalinux-managed shader depot — the common case for modern
    // games (Silksong, Brotato, Formula Legends...). Its manifest is never in the
    // Hubcap zip, so the shader job is about to ask GMRC for a request code. If NO
    // code provider is reachable right now, that request would be denied and Steam
    // would surface the cosmetic "No internet connection" popup. Avoid it: probe
    // the providers first.
    //   - a provider is up   -> let the job run; the shader manifest is fetched
    //                           and shaders pre-cache normally (no popup, no loss).
    //   - all providers down -> skip the shader pre-cache THIS ONCE (the game
    //                           still installs from its content depots; shaders
    //                           pre-cache the next install/update when a provider
    //                           is back up).
    // This makes the skip conditional on code availability instead of on the key,
    // so a keyed game never triggers the "No connection" popup. See RESEARCH §13.11.
    if (Gmrc::ProvidersReachable()) {
        Log::Info("ShaderDepot: depot %u keyed, a code provider is reachable "
                  "-> letting the shader pre-cache run", id);
        return id;
    }
    Log::Warn("ShaderDepot: depot %u keyed but NO code provider is reachable "
              "-> skipping shader pre-cache to avoid the 'No connection' popup "
              "(shaders will pre-cache when a provider is back up)", id);
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
