#pragma once

#include <cstdint>
#include <string>

namespace Patterns {

// =============================================================================
// v0.1 — depot key resolution
// =============================================================================
//
// Byte pattern for the depot key wrapper function in steamclient.so.
//
// This is the function that LumaCore calls LoadDepotDecryptionKey on Windows.
// It does:
//   1. Cache check via inner helper (returns 1/OK if cache hit)
//   2. RPC fallback: constructs CMsgClientGetDepotDecryptionKey (EMsg 5438)
//      and sends it via an inner function
//   3. Returns 0/Fail or 1/OK
//
// SIGNATURE (verified empirically on Steam Deck):
//   EResult LoadDepotDecryptionKey(
//       void*     this_,            // arg0
//       uint32_t  app_id,           // arg1
//       uint32_t  depot_id,         // arg2
//       void*     out_key_buffer);  // arg3
inline constexpr const char* kDepotKeyFnPattern =
    "55 57 56 53 E8 ?? ?? ?? ?? 81 C3 ?? ?? ?? ?? 83 EC 20 8B 74 24 34 8B 7C 24 3C 8B 6C 24 40";


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
// v0.4 — KeyValues::ReadAsBinary (appinfo KV-tree injection point)
// =============================================================================
//
// Steam's KV-tree binary deserializer. Used during appinfo parsing — our entry
// point for injecting depots BEFORE Steam builds pDepotInfo (which is what
// LumaCore-style works on but is too late on Linux because Valve filters
// unowned depots out of appinfo before the BuildDepotDependency stage).
//
// SIGNATURE (Linux i386, member function, all args on stack):
//   bool KeyValues::ReadAsBinary(
//       KeyValues*  this_root,    // [ebp+0x08]
//       void*       buf,          // [ebp+0x0C]
//       int         depth,        // [ebp+0x10]
//       bool        textMode,     // [ebp+0x14]
//       void*       symTable);    // [ebp+0x18]
//
// Verified UNIQUE at file offset 0x02477310 in build BuildID ea3e0c7660...
// (~Steam Deck May 2026 ubuntu12_32/steamclient.so).
//
// NOTE: differs from earlier linux32 variant only in `sub esp, 0x9C` (vs 0x8C);
// stack frame grew between builds. All else identical.
inline constexpr const char* kKeyValuesReadAsBinaryPattern =
    "55 89 E5 57 56 E8 ?? ?? ?? ?? 81 C6 ?? ?? ?? ?? 53 81 EC 9C 00 00 00 8B 45 14 89 45";


// =============================================================================
// v0.5 — CProtoBufMsgBase::InitFromPacket (network response interception)
// =============================================================================
//
// Hooked to rewrite denied Valve responses for unowned content:
//   eMsg 858  CMsgClientGetAppOwnershipTicketResponse  -> eresult=OK
//   eMsg 5439 CMsgClientGetDepotDecryptionKeyResponse   -> inject key + eresult=OK
//
// InitFromPacket prologue (Linux i386). Uses EDI as PIC/GOT base. Only 4 bytes
// precede the get_pc_thunk call, so subhook can't relocate it safely — we hook
// it with a hand-built trampoline (see packet_hook.cpp). Matched directly by
// prologue; the `mov eax,[edi+0x8b0]` tail makes it unique.
//
//   55             push ebp
//   89 E5          mov ebp, esp
//   57             push edi
//   E8 ?? ?? ?? ?? call __i686.get_pc_thunk.di
//   81 C7 ?? ?? ?? ?? add edi, <GOT>
//   56             push esi
//   53             push ebx
//   83 EC 2C       sub esp, 0x2C
//   8B 75 08       mov esi, [ebp+8]        ; pMsg (this)
//   8B 87 B0 08 00 00  mov eax, [edi+0x8B0]
inline constexpr const char* kInitFromPacketPattern =
    "55 89 E5 57 E8 ?? ?? ?? ?? 81 C7 ?? ?? ?? ?? 56 53 83 EC 2C 8B 75 08 8B 87 B0 08 00 00";


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
// PackageInfo layout (Linux i386, validated from struct used by LumaCore):
//   +0x00  uint32_t       PackageId
//   +0x30  CUtlVector<AppId_t>  AppIdVec      (16-byte CUtlVector header)
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
// Finders
// =============================================================================

uintptr_t FindDepotKeyFunction();
uintptr_t FindBuildDepotDependencyFunction();
uintptr_t FindKeyValuesReadAsBinaryFunction();
uintptr_t FindInitFromPacketFunction();
uintptr_t FindLoadPackageFunction();
uintptr_t FindGmrcFunction();

uintptr_t FindSteamclientBase();

} // namespace Patterns
