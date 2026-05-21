#pragma once

#include <cstdint>
#include <string>

namespace Patterns {

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
// Notes:
//   - This combination of save-set, exact stack frame size (0x20), and the
//     three arg loads from offsets 0x34/0x3c/0x40 was unique in the binary
//     analyzed during dev. Verified single match.
//   - The middle arg (arg1 at [esp+0x38]) is NOT loaded into a register at
//     the top — it's accessed later by displacement. The function takes 4
//     args total.
//
// SIGNATURE — best deduction (NOT empirically confirmed):
//   EResult LoadDepotDecryptionKey(
//       void*     this_,            // arg0 — some object pointer
//       uint32_t  arg1,             // arg1 — likely app_id (TO VERIFY via Frida)
//       uint32_t  arg2,             // arg2 — likely depot_id (TO VERIFY)
//       void*     out_key_buffer);  // arg3 — output buffer for the AES key
//
// ★ The actual arg order may differ from this assumption. First runtime test
// will reveal if app_id/depot_id are swapped. Frida verification needed.
inline constexpr const char* kDepotKeyFnPattern =
    "55 57 56 53 E8 ?? ?? ?? ?? 81 C3 ?? ?? ?? ?? 83 EC 20 8B 74 24 34 8B 7C 24 3C 8B 6C 24 40";

// Searches steamclient.so for the depot key function.
// Returns the absolute address of the function start, or 0 on failure.
uintptr_t FindDepotKeyFunction();

// Helper to locate steamclient.so's base address in this process.
// Returns 0 if not found.
uintptr_t FindSteamclientBase();

} // namespace Patterns
