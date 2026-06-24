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
// We call the original and, ONLY for a keyless game added via lumalinux (its
// app-id/shader depot is presence-only in keys.txt, KeyStore::IsPresenceOnly),
// return 0 instead — so Steam takes its native skip path for that one game.
// Games that ship a real shader key, and the user's genuinely-owned games, pass
// straight through and their shader pre-cache runs normally. This replaces the
// blunt global DisableShaderCache (RESEARCH §13.8) with a per-game skip. See
// RESEARCH §13.9 for the disassembly that located this seam.
//
// SLSsteam hooks the message dispatch, not shader functions, so this is
// collision-free. A pattern miss is non-fatal: the hook simply doesn't install
// and Steam behaves as it did before (the global toggle remains available as a
// fallback).
bool Install();
void Uninstall();

} // namespace Hooks::ShaderDepot
