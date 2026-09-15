#include "shader_depot_hook.hpp"
#include "../patterns.hpp"
#include "../rva_feed.hpp"
#include "../key_store.hpp"
#include "../gmrc_store.hpp"
#include "gmrc_hook.hpp"
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
// Three cases, decided per game (RESEARCH §13.10, §13.11):
//   keyless (presence-only in keys.txt) -> 0: could never decrypt.
//   not ours                           -> pass through, never touched.
//   keyed, ours                        -> the shader manifest is never in a
//     Hubcap zip, so the job has to ask for a manifest request code. Valve
//     grants it only for apps whose depot <appid> is public (Workshop games)
//     and denies the rest, and the denied path is not free: Steam shows "No
//     internet connection", stalls the install 30 s, then goes on without
//     shaders. So the job may run ONLY when the GMRC hook is installed AND a
//     provider is answering right now (Gmrc::ProvidersReachable); otherwise 0,
//     which is Steam's own clean skip — same end state, no popup, no stall
//     (verified 2026-09-10 on Valheim / Lethal Company in the SteamOS codespace).
//     v0.20.0 returned 0 unconditionally because no provider existed after
//     2026-09-09; with one again (see gmrc_store.hpp) the shaders of the
//     Workshop-carrying games (Brotato, RimWorld...) are back within reach.
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

    // Keyless (presence-only in keys.txt): the pre-cache can never decrypt.
    if (KeyStore::IsPresenceOnly(id)) {
        Log::Info("ShaderDepot: depot %u is keyless (presence-only) -> returning 0 "
                  "so Steam skips its shader pre-cache cleanly", id);
        return 0;
    }

    // Not a lumalinux-managed depot: the user's genuinely-owned games. Never
    // touch them — their shader pre-cache goes down Steam's normal owned path.
    if (!KeyStore::HasDepot(id)) return id;

    // Keyed, ours: the job is about to ask for the shader manifest's request
    // code. Let it only when the GMRC hook can answer (installed, provider up).
    if (!Hooks::Gmrc::Active()) {
        Log::Info("ShaderDepot: depot %u keyed but the GMRC hook is off -> "
                  "returning 0 so Steam skips its shader pre-cache cleanly", id);
        return 0;
    }
    if (Gmrc::ProvidersReachable()) {
        Log::Info("ShaderDepot: depot %u keyed, a code provider is reachable "
                  "-> letting the shader pre-cache run", id);
        return id;
    }
    Log::Warn("ShaderDepot: depot %u keyed but NO code provider is reachable "
              "-> skipping shader pre-cache to avoid the 'No connection' popup "
              "(shaders pre-cache on a later install/update when one is back)", id);
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
