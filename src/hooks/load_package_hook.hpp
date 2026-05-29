#pragma once

namespace Hooks::LoadPackage {

// Hook CPackageInfoCache::LoadPackage. When the package being loaded has
// PackageId == 0 (the implicit "free apps everyone owns" package), we append
// our forced appids (KeyStore::GetForcedAppIds()) into pInfo->AppIdVec so
// Steam fetches their appinfo as if they were owned.
//
// This is the Linux equivalent of LumaCore's PackagePatch on Windows. Hooking
// here is critical: by the time Steam reaches BuildDepotDependency, the appids
// must already be in PackageId=0 — otherwise Steam never asks for their
// appinfo, BuildDep finds no matching app and we can't even PATCH depot
// entries (let alone inject new ones).
bool Install();
void Uninstall();

} // namespace Hooks::LoadPackage
