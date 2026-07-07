#pragma once

// Neutralise SLSsteam's update-block so LumaDeck-managed games auto-update again.
//
// SLSsteam's 20260705 release added an IClientAppManager::GetAppStateInfo hook
// (Apps::getAppStateInfo) that, for any game where shouldDisableUpdates(appId) is
// true (isAddedAppId || !isSubscribed), clears the APPSTATE_UPDATE_* bits of the
// app-state it hands back to Steam. That kills Steam's auto-updater for every
// AdditionalApps game — i.e. everything LumaDeck manages. It is deliberate on
// AceSLS's side, has no config toggle, and every config workaround has a worse
// side effect (UseWhitelist breaks unlock/DLC globally; PlayNotOwnedGames
// blanket-activates the whole library). See docs/slssteam-analysis.md.
//
// The clear compiles (SLSsteam is built -flto -O3) to a SINGLE combined-mask
// instruction: `and dword [reg+disp], 0xFFFFF8E5` (0xFFFFF8E5 = ~0x71A =
// ~(REQUIRED|QUEUED|OPTIONAL|RUNNING|PAUSED|STARTED)). We flip that imm32 to
// 0xFFFFFFFF in memory, turning the clear into a no-op (AND with all-ones keeps
// the value). The update-required bit then survives and Steam auto-updates
// normally. Nothing else in SLSsteam is touched: the game stays in AdditionalApps,
// ownership / DLC / tokens behave exactly as before — the ONLY removed behaviour
// is the update-flag clearing. Verified end-to-end on a live codespace (Mina the
// Hollower, in AdditionalApps, auto-downloaded its full update with this applied).
namespace SlsUpdateUnblock {

// Locate + patch the update-clear in the loaded SLSsteam.so. Safe to call once at
// startup. Returns true only if a patch was applied. No-op (returns false) when
// SLSsteam isn't loaded, the anchor isn't found exactly once, or memory can't be
// made writable — in every failure case the block simply stays and auto-update
// falls back to manual, a safe degradation, never a crash.
bool Apply();

} // namespace SlsUpdateUnblock
