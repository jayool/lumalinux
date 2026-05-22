#pragma once

namespace Hooks::KeyValues {

// Install a diagnostic hook on KeyValues::ReadAsBinary in steamclient.so.
// In v0.4 the hook is pure passthrough + logging:
//   - counts total calls and top-level (depth=0) calls
//   - logs only top-level calls with their args
//   - lets us identify which call corresponds to appinfo parsing for a given
//     AppId so v0.5 can target it for KV-tree mutation
//
// Returns true on success.
bool Install();
void Uninstall();

} // namespace Hooks::KeyValues
