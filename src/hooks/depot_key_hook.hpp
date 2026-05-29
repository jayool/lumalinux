#pragma once

#include <cstdint>

namespace Hooks::DepotKey {

// Install the hook on the depot key resolution function in steamclient.so.
// Returns true on success.
bool Install();

// Remove the hook (best-effort).
void Uninstall();

// The depot id whose key we most recently SERVED from the KeyStore. The GMRC
// hook reads this to correlate a GetManifestRequestCode response back to the
// manifest it's for: Steam asks for a depot's key right before requesting its
// manifest, so this is the depot currently being processed. 0 if none yet.
uint32_t LastServedDepot();

} // namespace Hooks::DepotKey
