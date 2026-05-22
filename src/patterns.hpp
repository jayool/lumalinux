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
// Finders
// =============================================================================

uintptr_t FindDepotKeyFunction();
uintptr_t FindBuildDepotDependencyFunction();
uintptr_t FindKeyValuesReadAsBinaryFunction();

uintptr_t FindSteamclientBase();

} // namespace Patterns
