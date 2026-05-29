#pragma once

namespace Hooks::Gmrc {

// Manifest-Request-Code injector. Hooks the GMRC getter
// (CContentServerDirectoryClient::BYieldingGetManifestRequestCode, found via
// Ghidra). For a manifest of one of our forced depots, fetches the request code
// from gmrc.wudrm.com and writes it to the function's out-param, returning
// success — so Steam downloads the manifest for unowned content as if Valve had
// authorized it. Other manifests fall through to the original.
bool Install();
void Uninstall();

} // namespace Hooks::Gmrc
