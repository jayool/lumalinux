#pragma once

namespace Hooks::DepotDependency {

// Install a diagnostic hook on CUserAppManager::BuildDepotDependency in
// steamclient.so. The hook logs the depot list Steam produces so we can
// determine whether content depots (e.g. 2379781/2379782 for Balatro) appear
// in pDepotInfo for unowned apps, or whether we need to inject them at an
// earlier hook point.
//
// v0.2: diagnostic only — no mutation of the depot list yet.
//
// Returns true on success.
bool Install();
void Uninstall();

} // namespace Hooks::DepotDependency
