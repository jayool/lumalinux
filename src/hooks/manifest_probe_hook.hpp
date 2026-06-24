#pragma once

namespace Hooks::ManifestProbe {

// DIAGNOSTIC-ONLY hook (v0.13.10) on
// CDepotDownloadMgr::BYldRequestDepotManifest. Logs every manifest request for
// one of our depots (and any depot whose id == app id, i.e. the shader
// pre-cache depot), including the value the original returns, then ALWAYS defers
// to the original. It never changes behaviour — its only purpose is to map how
// the keyless app-id / shader depot flows through this layer so we can design a
// per-game shader-skip. See RESEARCH §13.8.
bool Install();
void Uninstall();

} // namespace Hooks::ManifestProbe
