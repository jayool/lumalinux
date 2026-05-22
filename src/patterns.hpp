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
//      and sends it via an inner function (~0x109caa0 in dev binary)
//   3. Returns 0/Fail or 1/OK
//
// Located in build 1778284286 (May 2025) at VMA 0x1769cc0. The pattern matches
// the function prologue:
//   push ebp / push edi / push esi / push ebx
//   call <GOT thunk>
//   add  ebx, IMM
//   sub  esp, 0x20
//   mov  esi, [esp+0x34]   ; arg0 (this pointer)
//   mov  edi, [esp+0x3c]   ; arg2 (one of: app_id, depot_id, out_buf)
//   mov  ebp, [esp+0x40]   ; arg3
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
// v0.2 — appinfo / depot list (LumaCore-equivalent on Linux)
// =============================================================================
//
// CUserAppManager::BuildDepotDependency
//
// On Windows, LumaCore hooks this function to patch ManifestGid/ManifestSize
// of depot entries AFTER Steam builds the depot list for an app. The Linux
// i386 prologue uses ESI as PIC base (NOT EBX), and the function takes 8 args
// via cdecl on the stack:
//
//   55                  push ebp
//   89 E5               mov  ebp, esp
//   57 56               push edi; push esi
//   E8 ?? ?? ?? ??      call <PIC thunk>
//   81 C6 ?? ?? ?? ??   add  esi, IMM        ; ★ PIC base = esi
//   53                  push ebx
//   81 EC 2C 02 00 00   sub  esp, 0x22C
//   8B 45 08            mov  eax, [ebp+0x8]  ; arg0 (this pointer)
//   89 85 ...           mov  [ebp-X], eax
//
// Verified UNIQUE in build ~May 2026 steamclient.so at VMA 0xf30e80.
//
// SIGNATURE (Linux i386 cdecl — all args on stack at [ebp+0x8..0x24]):
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


// KeyValues::ReadAsBinary — KV-tree deserializer used to parse appinfo.
// Linux i386 prologue (uses esi as PIC base). Verified unique at VMA 0x23ee820.
// Not currently hooked; kept here for future v0.3 use.
inline constexpr const char* kKeyValuesReadAsBinaryPattern =
    "55 89 E5 57 56 E8 ?? ?? ?? ?? 81 C6 ?? ?? ?? ?? 53 81 EC 8C 00 00 00 8B 45 14 89 45";


// =============================================================================
// Finders
// =============================================================================

// v0.1 — depot key resolution wrapper. Returns 0 on failure.
uintptr_t FindDepotKeyFunction();

// v0.2 — CUserAppManager::BuildDepotDependency. Returns 0 on failure.
uintptr_t FindBuildDepotDependencyFunction();

// Helper: locate steamclient.so's executable base in this process.
uintptr_t FindSteamclientBase();

} // namespace Patterns
