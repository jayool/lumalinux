#pragma once

#include <cstdint>
#include <cstddef>

#include "../steam_types.hpp"   // CUtlVector<T>

namespace Hooks::LoadPackage {

// Hook CPackageInfoCache::LoadPackage. When the package being loaded has
// PackageId == 0 (the implicit "free apps everyone owns" package), it logs
// what it sees; it no longer injects. Installed only with LUMA_LOADPKG_DEBUG:
// the injection into package 0 is done by the package-0 finder
// (package_zero_finder.cpp), which walks the cache instead of waiting for a
// LoadPackage call that a cached package 0 never makes (RESEARCH §13).
//
// This was the Linux equivalent of LumaCore's PackagePatch on Windows. The
// injection helper below is what the finder calls.
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

constexpr std::size_t kOffPackageId = 0x00;
#ifdef LUMA_X86_64
// steamrt64 client (measured on 947e5a19…, docs/steam-rt3-x64.md §1.2): the two
// pointers after the SHA-1 are 8 bytes each, so AppIdVec starts at +0x40
// (CUtlVector<uint32_t> is 24 bytes there: m_pMemory +0, alloc +8, grow +0xc,
// m_Size +0x10); DepotIdVec at +0x58.
constexpr std::size_t kOffAppIdVec   = 0x40;
constexpr std::size_t kOffDepotIdVec = 0x58;
#else
constexpr std::size_t kOffAppIdVec   = 0x38;
constexpr std::size_t kOffDepotIdVec = 0x48;
#endif

inline uint32_t PkgId(void* pInfo) {
    return *reinterpret_cast<uint32_t*>(
        static_cast<uint8_t*>(pInfo) + kOffPackageId);
}
inline CUtlVector<uint32_t>* AppIdVec(void* pInfo) {
    return reinterpret_cast<CUtlVector<uint32_t>*>(
        static_cast<uint8_t*>(pInfo) + kOffAppIdVec);
}
inline CUtlVector<uint32_t>* DepotIdVec(void* pInfo) {
    return reinterpret_cast<CUtlVector<uint32_t>*>(
        static_cast<uint8_t*>(pInfo) + kOffDepotIdVec);
}

// Where lumalinux's depot ids go inside PackageId=0: the package's own depot
// list, DepotIdVec, on both clients (where at0m's download.lua puts Assella's
// depots). Until 2026-10-31 they went into AppIdVec, measured since v0.8 to
// pass the 32-bit client's per-depot licence filter where the app id alone did
// not. The 64-bit client rebuilds its licence table only from CUser::RunFrame's
// check (docs/steam-rt3-x64.md §1.3), and that check refuses while any id in
// AppIdVec has no settled appinfo record: a depot id never gets one (gdb on
// ed794dda: one check, the depot id unsettled, no rebuild, "0 target"). In
// DepotIdVec there is no record to settle: with the depots there the check
// passed at once, the depot mounted and the game ran (run 44); without the
// check's rebuild it did not (run 43). The 32-bit client, whose reconcile has
// no such gate, is moved for the same reason: the beta is the next stable, and
// AppIdVec entries make Steam create app records for depot ids and ask Valve
// for them.
inline CUtlVector<uint32_t>* DepotTargetVec(void* pInfo) { return DepotIdVec(pInfo); }
constexpr const char* kDepotTargetName = "DepotIdVec";

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

// The other direction: take out of AppIdVec the ids lumalinux injected that
// are no longer in keys.txt (the game was uninstalled, or an owned game's DLC
// keys were retired). Only ids this process put there are ever removed —
// Steam's own package-0 entries are never touched. Without this the client
// kept believing it owned the DLC until its next start (measured 2026-10-07
// 07:20: a re-tick after an uninstall re-downloaded them). The finder calls
// it before InjectDepots on every pass; the keys.txt watcher already arms
// the reconcile for the removal, so Steam re-reads ownership right after.
// Returns true if anything was removed.
bool RetireDepots(void* pInfo, const char* source);

} // namespace Hooks::LoadPackage
