#pragma once

#include <cstdint>
#include <cstddef>

#include "../steam_types.hpp"   // CUtlVector<T>

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

// =============================================================================
// PackageInfo layout (Linux i386, derived from LumaCore's Structs.h).
// =============================================================================
// Exposed in the header so package_zero_finder.cpp can read the same layout
// without duplicating offsets. Single source of truth.
//
// PackageInfo (Linux i386):
//   +0x00  uint32_t  PackageId
//   +0x04  int32_t   ChangeNumber
//   +0x08  uint64_t  PICS_token        (4-byte aligned on i386 SysV)
//   +0x10  int32_t   BillingType       (enum class : int)
//   +0x14  int32_t   LicenseType
//   +0x18  int32_t   Status
//   +0x1C  uint8_t   SHA_1_Hash[20]
//   +0x30  void*     pPackageInfoNodeBegin
//   +0x34  void*     pExtendNodeBegin
//   +0x38  CUtlVector<uint32_t>  AppIdVec     (16 bytes)
//   +0x48  CUtlVector<uint32_t>  DepotIdVec   (16 bytes)
//
// CUtlVector<T> lives in steam_types.hpp (shared with the BuildDep hook).

constexpr std::size_t kOffPackageId  = 0x00;
constexpr std::size_t kOffAppIdVec   = 0x38;
constexpr std::size_t kOffDepotIdVec = 0x48;

inline uint32_t PkgId(void* pInfo) {
    return *reinterpret_cast<uint32_t*>(
        static_cast<uint8_t*>(pInfo) + kOffPackageId);
}
inline CUtlVector<uint32_t>* AppIdVec(void* pInfo) {
    return reinterpret_cast<CUtlVector<uint32_t>*>(
        static_cast<uint8_t*>(pInfo) + kOffAppIdVec);
}
// Experiment only (LUMA_DEPOT_IDVEC): the package's separate depot-eligibility
// vector. moon populates this with depot ids; lumalinux historically used
// AppIdVec for everything. See InjectDepots.
inline CUtlVector<uint32_t>* DepotIdVec(void* pInfo) {
    return reinterpret_cast<CUtlVector<uint32_t>*>(
        static_cast<uint8_t*>(pInfo) + kOffDepotIdVec);
}

// =============================================================================
// Reusable depot injection
// =============================================================================
// Performs the actual KeyStore depot → AppIdVec injection, with the same
// sanity checks the LoadPackage hook has always had (size cap, AppId range
// validation, idempotent dedup, realloc-with-doubling growth).
//
// `source` is a short label for the log line — distinguishes injections done
// from the hook itself ("hook") from those done by the package-0 finder
// worker ("finder") so we can tell them apart in postmortem.
//
// Returns true if any depot was actually added; false if there was nothing to
// add, sanity checks failed, or realloc failed. NEVER writes anything on a
// false return.
//
// Content-based idempotency: it filters out depot ids already present in
// AppIdVec, so calling it twice (e.g. hook + finder both fire) never doubles
// entries — the vector's own contents are the source of truth, no external
// flag needed.
bool InjectDepots(void* pInfo, const char* source);

} // namespace Hooks::LoadPackage
