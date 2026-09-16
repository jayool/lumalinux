#pragma once

namespace Hooks::Gmrc {

// Manifest-Request-Code injector. Hooks the GMRC getter
// (CContentServerDirectoryClient::BYieldingGetManifestRequestCode, found via
// Ghidra). For a manifest of one of our forced depots, fetches the request code
// from the provider cascade (src/gmrc_store.hpp) with the depot id and gid
// Steam passed, writes it to the function's out-param and returns success — so
// Steam downloads the manifest for unowned content as if Valve had authorized
// it. Other manifests, and ours when no provider answers, fall through to the
// original. On by default since v0.21.0; LUMA_NO_GMRC disables it (main.cpp).
bool Install();
// True once the hook is installed (the ShaderDepot hook lets a keyed game's
// shader pre-cache run only if GMRC can fetch its manifest code).
bool Active();
void Uninstall();

} // namespace Hooks::Gmrc
