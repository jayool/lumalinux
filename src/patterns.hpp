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
// v0.5 — LoadPackage (PackageId=0 appid injection)
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
// Linux i386 prologue:
//   55                  push ebp
//   89 E5               mov ebp, esp
//   57                  push edi
//   E8 ?? ?? ?? ??      call __i686.get_pc_thunk.di
//   81 C7 ?? ?? ?? ??   add edi, <GOT>
//   56                  push esi
//   53                  push ebx
//   81 EC 1C 01 00 00   sub esp, 0x11C
//
// PackageInfo layout (Linux i386, validated from LumaCore's Structs.h):
//   +0x00  uint32_t              PackageId
//   +0x38  CUtlVector<AppId_t>   AppIdVec  (m_pMemory@+0x38, m_Size@+0x44)
//
// IMPORTANT — read before touching this pattern:
//
// This pattern matches 3 candidates in the steamclient.so binary; the runtime
// enumerates them and picks one by index via LUMA_LOADPKG_IDX (default 0). The
// first match is the real LoadPackage. This has been true across multiple Steam
// builds — including the 7c4ac73e... (2026.06.10) build that triggered the
// v0.10.3..v0.10.6 fiasco.
//
// In v0.10.3 a previous session interpreted "multi-match" as "pattern broken"
// and re-derived from the anchor strings "PackageID : %u, change number" and
// "No package info for packageID %u found" — but those strings live inside
// CPackageInfoCache::GetPackage(packageId) (a cache lookup), NOT inside
// LoadPackage. The "re-derivation" hooked that lookup function, the hook ran
// but never fired with PackageId==0 (because GetPackage isn't on the PICS load
// path), and downloads silently broke ("0 mounted depots"). v0.10.4 picked yet
// another candidate by behaviour filter and crashed Steam at startup.
//
// Steam did NOT refactor LoadPackage. The function is still at the same RVA
// (0x14b780 across observed builds) with the same prologue and the same
// PackageInfo layout. If a future Steam build legitimately moves it, the
// re-derivation MUST be validated against runtime ground truth (a log line
// "LoadPackage: PackageId=0 hit — vec mem=… size=…") before any pattern bump
// is published.
inline constexpr const char* kLoadPackagePattern =
    "55 89 E5 57 E8 ?? ?? ?? ?? 81 C7 ?? ?? ?? ?? 56 53 81 EC 1C 01 00 00";


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
// v0.14 — GetShaderCacheDepot (per-game shader pre-cache skip, "path C")
// =============================================================================
//
// CShaderCacheManager::GetShaderCacheDepot(appinfo). Given a game's appinfo it
// reads the PICS KeyValues `appinfo.game.shadercachedepot` and returns that
// depot id (0 if the app isn't a game or has no shader depot). Its SOLE caller
// is CGetShaderDepotManifestJob::BYieldingRunClientJob:
//
//     id = GetShaderCacheDepot(appinfo);
//     if (id == 0) -> log "skipping because shader depot ID is invalid",
//                     return cleanly (no error, no install pause, no retry loop)
//     else         -> "Getting shader depot manifests" -> download+decrypt
//                     (which FAILS for a keyless game and triggers the loop /
//                      "Invalid content configuration" suspend, see RESEARCH 13.8)
//
// So returning 0 here for a keyless game makes Steam take its OWN clean skip
// path, per-game, with no global DisableShaderCache and without breaking games
// that ship a real shader key (those pass through and their shaders work). See
// RESEARCH 13.9 for the full disassembly; this is the shipped form of "path C".
//
// SIGNATURE (cdecl, i386): returns the shader depot id (== app id by Steam
// convention), 0 if none.
//   uint32_t GetShaderCacheDepot(void* appinfo /* or this_ */);
//
// Prologue (PIC get_pc_thunk.bx):
//   57 56 53            push edi; push esi; push ebx
//   E8 ?? ?? ?? ??      call __i686.get_pc_thunk.bx        (rel32 wildcarded)
//   81 C3 ?? ?? ?? ??   add ebx, <GOT>                     (GOT imm32 wildcarded)
//   8B 83 ?? ?? ?? ??   mov eax, [ebx+0x2b758]   (shader-manager global —
//                       the disp32 is a GOT-relative offset that SHIFTS between
//                       Steam builds, like the package-0 cache global in §13.5;
//                       wildcarded so a rebuild that only moves the global does
//                       NOT break this hook. Verified still unique wildcarded.)
//   8B 40 44            mov eax, [eax+0x44]
//   85 C0               test eax, eax
//   75 0D               jne ...                  (0 if shader mgr absent)
//   5B / 31 C0          pop ebx / xor eax,eax
// The identity anchors that remain fixed are the 57 56 53 prologue and the
// distinctive 8B 40 44 85 C0 75 0D 5B 31 C0 tail (read [eax+0x44]; test;
// early-return 0). Verified UNIQUE in build 7c4ac73e both with and without the
// global disp32 wildcarded.
inline constexpr const char* kShaderCacheDepotPattern =
    "57 56 53 E8 ?? ?? ?? ?? 81 C3 ?? ?? ?? ?? 8B 83 ?? ?? ?? ?? 8B 40 44 85 C0 75 0D 5B 31 C0";


// =============================================================================
// Finders
// =============================================================================

uintptr_t FindDepotKeyFunction();
uintptr_t FindBuildDepotDependencyFunction();
uintptr_t FindLoadPackageFunction();
uintptr_t FindGmrcFunction();
uintptr_t FindShaderCacheDepotFunction();

uintptr_t FindSteamclientBase();

} // namespace Patterns

