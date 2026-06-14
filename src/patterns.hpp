#pragma once

#include <cstdint>
#include <string>

namespace Patterns {

// =============================================================================
// v1.0 — depot key resolution (KeyValues accessor, LumaCore-style)
// =============================================================================
//
// We hook the INNER KeyValues accessor, exactly the function LumaCore hooks on
// Windows — NOT the outer dispatcher. Steam's depot-key flow is:
//
//   outer dispatcher (this_, app_id, depot_id, out_key)
//     └─ cache lookup → builds the KeyName
//          "Software\Valve\Steam\Depots\<depot>\DecryptionKey"
//          and virtual-calls THIS accessor with (pObject, foo, KeyName, Key,
//          KeySize) to read the key from Steam's KeyValues store.
//
// Hooking the outer dispatcher (v0.1) and short-circuiting it corrupts Steam's
// heap when we serve an owned depot (verified: `free(): invalid pointer`).
// Hooking THIS accessor instead just answers the KeyValues query: the cache
// gets a clean hit and the dispatcher proceeds through its normal path — no
// short-circuit, no corruption, works for owned and unowned depots alike. This
// is precisely LumaCore's DepotKeys.cpp approach.
//
// SIGNATURE (cdecl, i386 — 5 stack args, matches LumaCore):
//   int32_t LoadDepotDecryptionKey(
//       void*     pObject,    // arg0
//       uint32_t  foo,        // arg1 (KeyValues value-type selector; 1 for keys)
//       char*     KeyName,    // arg2 "Software\Valve\Steam\Depots\<depot>\DecryptionKey"
//       char*     Key,        // arg3 output buffer (32-byte AES key on success)
//       uint32_t  KeySize);   // arg4 size of Key buffer — VALIDATE before writing
//   returns the number of key bytes written on success (32), 0 on miss.
//
// Prologue (PIC get_pc_thunk; sub esp,0x24; then the 5-arg load sequence
// mov eax,[esp+0x44]; mov ebp,[esp+0x38]; mov edi,[esp+0x3c]; mov esi,[esp+0x40];
// mov [esp+0x10],eax; mov eax,[esp+0x48]; mov [esp+0x14],eax). Verified UNIQUE.
inline constexpr const char* kDepotKeyFnPattern =
    "55 57 56 53 E8 ?? ?? ?? ?? 81 C3 ?? ?? ?? ?? 83 EC 24 8B 44 24 44 8B 6C 24 38 8B 7C 24 3C 8B 74 24 40 89 44 24 10 8B 44 24 48 89 44 24 14";


// =============================================================================
// v0.2/v0.3 — BuildDepotDependency (diagnostic / injection mode)
// =============================================================================
//
// CUserAppManager::BuildDepotDependency. Linux i386 prologue uses ESI as PIC
// base. Function takes 8 args via cdecl on the stack. Verified UNIQUE in build
// 81b357094db... at VMA 0xf30e80.
//
// SIGNATURE (Linux i386 cdecl):
//   bool BuildDepotDependency(
//       void*       this_,                              // [ebp+0x08]
//       uint32_t    AppId,                              // [ebp+0x0C]
//       void*       pUserConfig,                        // [ebp+0x10]
//       CUtlVector<DepotEntry>* pDepotInfo,             // [ebp+0x14]
//       CUtlVector<DepotEntry>* pSharedDepotInfo,       // [ebp+0x18]
//       void*       pSteamApp,                          // [ebp+0x1C]
//       uint32_t*   pBuildId,                           // [ebp+0x20]
//       bool*       pbBetaFallback);                    // [ebp+0x24]
inline constexpr const char* kBuildDepotDependencyPattern =
    "55 89 E5 57 56 E8 ?? ?? ?? ?? 81 C6 ?? ?? ?? ?? 53 81 EC 2C 02 00 00 8B 45 08 89 85";


// =============================================================================
// v0.10.6 — LoadPackage (rolled back to v0.10.3 wrapper as safety baseline)
// =============================================================================
//
// CPackageInfoCache::LoadPackage(PackageInfo* pInfo, uint8_t sha1[20],
//                                 int32_t change_number, void* p4)
//
// Steam parses PICS package responses through this function. When PackageId==0
// (the implicit "free apps everyone owns" package) we inject our forced appids
// into pInfo->AppIdVec so Steam later fetches the appinfo for those apps as if
// they were owned. This is the trick LumaCore uses on Windows.
//
// History:
//   v0.5    — original LoadPackage (get_pc_thunk.di, stack 0x11C)
//   v0.10.3 — anchor-string hit landed on a wrapper/getter (stack 0x28). Hook
//             installed but never fired PackageId==0 because the wrapper is
//             not the function Steam actually calls during PICS parsing.
//             Steam OPENS, downloads broken ("0 mounted depots").
//   v0.10.4 — picked a candidate by prologue+behaviour scan (stack 0x22C, file
//             0x2510440). Looked right offline. CRASHED Steam at startup:
//             the function gets called early during PICS init with structs
//             whose [+0]==0 and [+0x38] is NOT an AppIdVec.m_pMemory — our
//             hook payload dereferenced garbage and SIGSEGV'd the worker.
//             Behaviour invariants (PackageId==0 check, writes to [reg+0x38],
//             stack ≥ 0x100) are NOT specific enough to single out
//             LoadPackage among unrelated package-shaped functions.
//   v0.10.6 — ROLLED BACK to the v0.10.3 wrapper pattern as a known-safe
//             baseline (Steam opens). Discovery of the real LoadPackage is
//             deferred to the read-only probe system in
//             src/hooks/load_package_probe.cpp, opt-in via the env var
//             LUMA_LOADPKG_PROBE=1. Once discovery output identifies the
//             right function from a live Steam session, a future bump will
//             re-target this pattern.
//
// PackageInfo layout (Linux i386, validated from LumaCore Structs.h):
//   +0x00  uint32_t                PackageId
//   +0x38  CUtlVector<AppId_t>     AppIdVec   (m_pMemory@+0x38, m_Size@+0x44)
inline constexpr const char* kLoadPackagePattern =
    "55 89 E5 57 56 E8 ?? ?? ?? ?? 81 C6 ?? ?? ?? ?? 53 83 EC 28 8B 7D 0C 57 89 F3 "
    "E8 ?? ?? ?? ?? 83 C4 10 85 C0";


// =============================================================================
// v0.8 — BYieldingGetManifestRequestCode (the GMRC getter)
// =============================================================================
//
// Located via Ghidra: the function (FUN_012d3bd0, RVA 0x12c3bd0) that builds the
// ContentServerDirectory.GetManifestRequestCode#1 request, waits for the
// response, and on success writes the 8-byte manifest request code to its last
// argument and returns 1. For unowned content the server denies it (returns 0).
//
// Signature (cdecl, i386):
//   int32 GetManifestRequestCode(
//       void*     this_,        // [ebp+0x08]
//       uint32_t  app_id,       // [ebp+0x0C]
//       uint32_t  depot_id,     // [ebp+0x10]
//       uint32_t  manifest_lo,  // [ebp+0x14]   manifest_id low 32
//       uint32_t  manifest_hi,  // [ebp+0x18]   manifest_id high 32
//       char*     branch,       // [ebp+0x1C]
//       uint64_t* out_code);    // [ebp+0x20]   <- request code written here
//
// Prologue (PIC, get_pc_thunk.ax):
//   E8 ?? ?? ?? ??      call __i686.get_pc_thunk.ax
//   05 ?? ?? ?? ??      add eax, <GOT>
//   55 89 E5            push ebp; mov ebp, esp
//   57 56 53            push edi; push esi; push ebx
//   81 EC 10 01 00 00   sub esp, 0x110
//   8B 7D 08            mov edi, [ebp+8]
//   8B 4D 20            mov ecx, [ebp+0x20]
inline constexpr const char* kGmrcFunctionPattern =
    "E8 ?? ?? ?? ?? 05 ?? ?? ?? ?? 55 89 E5 57 56 53 81 EC 10 01 00 00 8B 7D 08 8B 4D 20";


// =============================================================================
// Finders
// =============================================================================

uintptr_t FindDepotKeyFunction();
uintptr_t FindBuildDepotDependencyFunction();
uintptr_t FindLoadPackageFunction();
uintptr_t FindGmrcFunction();

uintptr_t FindSteamclientBase();

} // namespace Patterns

