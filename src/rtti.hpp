#pragma once

#include <cstdint>

// RTTI-based function resolution (update-resilient locator, CloudRedirect's
// technique — RESEARCH §15). Instead of a byte-pattern prologue (which shifts on
// every Steam rebuild), resolve a virtual method by its C++ RTTI:
//
//   type-name string ("12CConfigStore") -> type_info -> vtable
//   ([offset_to_top=0, type_info*] header) -> a fixed vtable SLOT index.
//
// None of {class name, slot index} is an address, so none moves per build; an
// update is only needed if Valve reorders that class's virtuals (§15.4) — far
// rarer than a prologue shift. Verified for DepotKey (CConfigStore slot 6 =
// LoadDepotDecryptionKey) on multiple builds via tools/experiment_rtti_depotkey.py.
//
// Read-only: lumalinux resolves the ADDRESS this way, then installs its normal
// function detour there (it does NOT swap the vtable slot). So this can never
// corrupt the vtable; a miss just returns 0 and the caller falls back to the
// byte pattern.
namespace Rtti {

// Resolve `mangledName`'s vtable and return the absolute runtime address stored
// in virtual slot `slot`, or 0 on any failure (string not found, relocations
// never completed, vtable not found, slot outside steamclient). The caller is
// expected to keep a byte-pattern fallback until this is proven on-device.
uintptr_t ResolveVtableSlot(const char* mangledName, int slot);

// Resolve by DERIVING the slot instead of trusting a fixed index: find
// `mangledName`'s vtable via RTTI, then return the address of the UNIQUE slot
// whose target function begins with `sigPattern` (same "AA BB ?? .." syntax as
// the k*Pattern literals). This survives a vtable reorder — the accessor is
// found wherever Valve moved it, where a hardcoded slot would silently
// mis-resolve. Returns 0 on zero OR multiple matches (fail closed), or if the
// vtable can't be found. `maxSlots` caps the scan; if `outSlot` != nullptr it
// receives the derived index on success (for logging / drift detection).
uintptr_t ResolveVtableSlotBySignature(const char* mangledName,
                                       const char* sigPattern,
                                       int maxSlots, int* outSlot);

} // namespace Rtti
