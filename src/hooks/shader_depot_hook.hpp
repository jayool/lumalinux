#pragma once

namespace Hooks::ShaderDepot {

// Per-game shader pre-cache skip ("path C", v0.14).
//
// Hooks CShaderCacheManager::GetShaderCacheDepot(appinfo), whose only caller is
// CGetShaderDepotManifestJob. The original returns the app's shader-cache depot
// id (== app id) from PICS appinfo, or 0 if the app has none. When it returns
// 0, Steam logs "skipping because shader depot ID is invalid" and ends the
// shader job CLEANLY — no error, no "Invalid content configuration", no install
// pause, no 5-minute retry loop.
//
// We call the original and decide per game (RESEARCH §13.10, §13.11):
//   1. Keyless game (presence-only in keys.txt): the shader manifest can never
//      decrypt without a key -> return 0 so Steam skips cleanly. (v0.14)
//   2. The user's genuinely-owned games (not in keys.txt): pass straight through;
//      their shader pre-cache runs down Steam's normal owned path.
//   3. Keyed lumalinux-managed game (Silksong, Brotato...): its shader manifest is
//      never in the Hubcap zip, so the job will ask GMRC for a request code. We
//      probe the code providers (Gmrc::ProvidersReachable) FIRST — if one is up,
//      let the job run (shaders pre-cache normally); if all are down, return 0 to
//      skip THIS once, so Steam never issues a request it can't satisfy and the
//      cosmetic "No internet connection" popup never appears. Shaders pre-cache on
//      a later install/update when a provider is back up. (v0.16)
// This replaces the blunt global DisableShaderCache (RESEARCH §13.8) with a
// per-game, code-availability-gated skip. See RESEARCH §13.9 for the disassembly
// that located this seam.
//
// SLSsteam hooks the message dispatch, not shader functions, so this is
// collision-free. A pattern miss is non-fatal: the hook simply doesn't install
// and Steam behaves as it did before (the global toggle remains available as a
// fallback).
bool Install();
void Uninstall();

} // namespace Hooks::ShaderDepot
