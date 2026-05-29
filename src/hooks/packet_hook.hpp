#pragma once

namespace Hooks::Packet {

// Install the InitFromPacket hook (chained outer over SLSsteam's existing hook).
//
// SLSsteam hooks InitFromPacket via libmem too, but its fixPICThunkCall only
// handles get_pc_thunk calls, not the rel32 of our outer jmp. So if WE install
// first, SLSsteam later corrupts our trampoline when chaining over us → crash.
// To avoid that, this Install polls until SLSsteam has placed its E9 detour
// (up to ~30s) and only then installs ourselves on top, becoming the outer
// hook. Our LmHook's RelocateChainedJmp then fixes the rel32 of the relocated
// SLSsteam jmp in our trampoline correctly.
//
// Once installed, intercepts CMsgClientGetAppOwnershipTicketResponse (eMsg 858)
// and flips eresult to OK for apps with at least one injectable depot in
// KeyStore — i.e. the apps we're forcing Steam to treat as owned.
//
// If SLSsteam's detour never appears (timeout), the hook is NOT installed.
// Better no 858 fake (install fails with Access Denied) than a crash.
bool Install();

void Uninstall();

} // namespace Hooks::Packet
