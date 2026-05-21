#pragma once

#include <cstdint>
#include <string>

namespace Patterns {

// Byte pattern for LoadDepotDecryptionKey-equivalent function in steamclient.so
//
// Origin: static analysis of an older steamclient.so (BuildID a2a96cd00...)
// performed during Hito 1. The function:
//   - Constructs CMsgClientGetDepotDecryptionKey (EMsg 5438)
//   - Waits for response (EMsg 5439)
//   - Returns the depot decryption key
//
// Signature (deduced from caller examination):
//   EResult LoadDepotDecryptionKey(void* this_, uint32_t app_id,
//                                  uint32_t depot_id, void* out_key_buffer);
//
// ★ MUST BE VERIFIED ★ against the actual steamclient.so on your Steam Deck.
// If this pattern doesn't match or matches multiple locations, extend it with
// additional bytes from the function prologue. Run Ghidra search:
//   Search → Memory → For Strings: replace ? with .* (regex mode)
//
// Wildcards (??) are for:
//   - The relative offset of the GOT-thunk call (E8 ?? ?? ?? ??)
//   - The GOT base offset added to ebx (81 C3 ?? ?? ?? ??)
// These vary by build, but the surrounding instruction encoding is stable.
inline constexpr const char* kDepotKeyFnPattern =
    "55 57 BF 03 00 00 00 56 53 E8 ?? ?? ?? ?? 81 C3 ?? ?? ?? ?? 83 EC 6C";

// Searches steamclient.so for the depot key function.
// Returns the absolute address of the function start, or 0 on failure.
uintptr_t FindDepotKeyFunction();

// Helper to locate steamclient.so's base address in this process.
// Returns 0 if not found.
uintptr_t FindSteamclientBase();

} // namespace Patterns
