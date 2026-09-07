// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace ProcFilter {

// True in the processes that actually host steamclient.so.
//
// lumalinux is loaded into EVERY child of the Steam launcher via LD_PRELOAD
// (steamerrorreporter, steam-runtime-launcher-service, game processes under
// Proton, …). Only `steam` and `steamwebhelper` ever map steamclient.so, so
// everywhere else we must return immediately with zero side effects.
//
// Fail-open: if /proc is unreadable we allow, because a false negative here
// silently disables lumalinux while a false positive only costs a worker that
// times out. Override with LUMA_PROCESS_ANY=1 for setups where a different
// process is the steamclient.so host (custom gamescope-session, debug, …).
//
// The result is cached: this is called from the constructor path AND from the
// dlopen interposer (src/libcurl_pin.cpp), which can fire before any of our
// own initialisation has run, so it must be self-contained and cheap.
bool IsSteamHost();

} // namespace ProcFilter
