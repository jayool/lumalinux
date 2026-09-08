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


// Resolve by deriving the slot FROM THE METHOD NAME, reading no byte of the
// target function. This is `download.lua`'s technique (SLSsteam's
// VFTableInfo_t::init + Decompiler::parseInterfaceMapBase); see
// docs/slssteam-plugins-analysis.md §7.5.a for the analysis and the on-binary
// verification.
//
//   1. `mapMangledName` is Steam's INTERFACE MAP class (e.g.
//      "21IClientConfigStoreMap") — the dispatch layer whose every virtual
//      references its own method name as a string literal.
//   2. Find that name's string, then the slot of the map vtable that REFERENCES
//      it (i386 PIC: `lea reg,[got + (S - GOT)]`).
//   3. Apply that index to the concrete class `implMangledName`
//      (e.g. "12CConfigStore").
//
// Independent of everything the byte pattern depends on: no prologue, no code
// layout, no published address. It only breaks if Valve renames the method or
// reorders the *Map interface — and even a reorder is absorbed, because the
// index is re-derived on every launch rather than assumed.
//
// Fail-closed. Returns 0 if: the string is absent; no map slot references it
// closely; the name is a prefix of a longer one and no exact NUL-terminated
// match exists; either vtable is unlocatable; or the resolved address falls
// outside steamclient.so. Overloads (the same name in several slots, each with
// its OWN string) resolve to the LOWEST index, which is what a bare name means
// in SLSsteam's table — the sibling slot is a DIFFERENT function, so this
// tie-break is load-bearing, not cosmetic. `outSlot` receives the derived index.
uintptr_t ResolveVtableSlotByName(const char* mapMangledName,
                                  const char* methodName,
                                  const char* implMangledName,
                                  int maxSlots, int* outSlot);

} // namespace Rtti
