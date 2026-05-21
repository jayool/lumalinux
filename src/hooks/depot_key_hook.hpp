#pragma once

#include <cstdint>

namespace Hooks::DepotKey {

// Install the hook on the depot key resolution function in steamclient.so.
// Returns true on success.
bool Install();

// Remove the hook (best-effort).
void Uninstall();

} // namespace Hooks::DepotKey
