// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 jayool — https://github.com/jayool/lumalinux
#pragma once

// Scope SLSsteam's native-achievement "legit app" guard so it also fires for
// LumaDeck-managed (AdditionalApps) games, WITHOUT touching genuinely-owned ones.
//
// SLSsteam 20260710 added native achievement support: when a game asks for its
// stats and you don't own it, SLSsteam borrows the schema from a real owner it
// finds via the game's recent reviews. Both entry points guard with
//     if (CUser::isSubscribed(appId)) return;   // "don't touch legit apps"
// isSubscribed reads the *original* ownership (the trampoline, not SLSsteam's
// live fake), so for a pure-SLSsteam not-owned game it's false and the borrow
// runs. But LumaDeck installs games via NATIVE Steam download, which writes a
// real local licence (config.vdf DecryptionKeys, AppTokens, .acf) — that makes
// isSubscribed return *true*, so the guard skips the borrow and our games get no
// native achievements. (See luma-lab: docs/slssteam.md §2.7 and RESEARCH §17.)
//
// A blunt NOP of the guard is unsafe: it would run the borrow for GENUINELY
// owned games too, hijacking your real stats request with a reviewer's and
// clearing your unlocked achievements. So instead we repoint the guard's
// `call isSubscribed` to our own combined check
//     isSubscribed(appId) && !CConfig::isAddedAppId(appId)
// which returns true (skip the borrow) ONLY for games that are owned AND not in
// AdditionalApps — i.e. real purchases stay untouched, LumaDeck games get the
// borrow. The `test al,al; jne` that follows is left exactly as-is.
//
// SLSsteam 20261112 added a third place with the same guard: Achievements::sendMsg,
// which decides whether the new Steam UI's achievements page
// (Player.GetUserAchievements#1) is answered from the local stats store instead
// of by Valve — again only `if (!isSubscribed(appId))`, so again never for our
// games, whose page then reads "not in your library" with everything locked
// while the unlocks themselves are fine (Post-Game Summary, appcache/stats).
// That site gets the same repoint when the build has it; a build without it
// (or without the pattern) keeps the two sites above and logs why.
//
// All three callees (CUser::isSubscribed, CConfig::isAddedAppId, g_config) are
// resolved from SLSsteam.so's on-disk symbol table (the release .so is NOT
// stripped), so this survives -flto -O3 codegen shifts far better than a raw
// byte anchor. Fail-safe: if SLSsteam isn't loaded, a symbol is missing, or the
// `call isSubscribed; test al,al; jne` pattern isn't found exactly once per
// guard (or the two guards don't call the same isSubscribed), NOTHING is patched
// and native achievements simply fall back to off — never a crash.
namespace SlsAchievementUnblock {

// Locate + repoint the achievement guards in the loaded SLSsteam.so. Safe to
// call once at startup. Returns true only if BOTH schema-borrow guards were
// repointed; the optional achievements-page site is reported in the log.
bool Apply();

} // namespace SlsAchievementUnblock
