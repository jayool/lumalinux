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
// v0.10.4 — LoadPackage (PackageId=0 appid injection)
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
//   v0.10.4 — real LoadPackage re-identified by prologue+behaviour scan against
//             hash 7c4ac73ec1ae96b3338e06bdd69a1c79c7ab2f6932ca2c81b5b683f215ad1800
//             (Steam client 2026.06.10): file offset 0x2510440. Confirmed by
//             stack size 0x22C, presence of PackageId==0 comparison, 7 writes
//             to [pInfo+0x38] (AppIdVec.m_pMemory), and call from ReadFromDisk.
//
// Linux i386 prologue (v0.10.4):
//   55                  push ebp
//   89 E5               mov ebp, esp
//   57                  push edi
//   56                  push esi
//   53                  push ebx
//   E8 ?? ?? ?? ??      call __i686.get_pc_thunk.bx
//   81 C3 ?? ?? ?? ??   add ebx, <GOT>
//   81 EC 2C 02 00 00   sub esp, 0x22C
//   8B 45 08            mov eax, [ebp+0x08]  ; pInfo
//   8B 7D 0C            mov edi, [ebp+0x0C]  ; sha1
//   89 85 D4 FD FF FF   mov [ebp-0x22C], eax ; spill pInfo
//   65 8B 35 14 00 00 00  mov esi, gs:[0x14] ; stack canary
//   89 75 E4            mov [ebp-0x1C], esi
//   31 F6               xor esi, esi
//   85 C0               test eax, eax        ; if (pInfo == NULL) return
//
// PackageInfo layout (Linux i386, validated from LumaCore Structs.h):
//   +0x00  uint32_t                PackageId
//   +0x38  CUtlVector<AppId_t>     AppIdVec   (m_pMemory@+0x38, m_Size@+0x44)
//
// Pattern is 49 bytes, wildcarded only over the 4-byte get_pc_thunk call offset
// and the 4-byte GOT delta. Verified UNIQUE (1 match) in the 2026.06.10 client.
inline constexpr const char* kLoadPackagePattern =
    "55 89 E5 57 56 53 E8 ?? ?? ?? ?? 81 C3 ?? ?? ?? ?? 81 EC 2C 02 00 00 "
    "8B 45 08 8B 7D 0C 89 85 D4 FD FF FF 65 8B 35 14 00 00 00 89 75 E4 31 F6 85 C0";


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

