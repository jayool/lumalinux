# lumalinux and the CachyOS / multi-distro port

> Companion to LumaDeck's `docs/porting-cachyos.md` (the master design doc).
> This file tracks only the **lumalinux-side** items. Bottom line: the hooking
> core does **not** need porting — the work here is small and mostly
> confirmation + doc fixes.

## Why the core doesn't move

- **`steamclient.so` is the same binary on the desktop and Deck channels** for
  current builds. SLSsteam's upstream SafeMode whitelist annotates a single hash
  as `#ubuntu12_32 & steamdeck_stable` for every build since Jan 2026 — Valve
  ships an identical `steamclient.so` in both. Only the *client package/manifest
  version number* differs between channels.
- lumalinux locates hooks by **runtime signature scan + RTTI**
  (`src/patterns.cpp`, `src/rtti.cpp`), which is distro-agnostic and, for an
  identical binary, matches unchanged.
- Field evidence (LumaDeck issue #31, CachyOS Handheld): the plugin reported
  **all hooks injected on first Steam boot** — the core loaded and hooked on
  CachyOS. The failure was downstream in LumaDeck's session/crash-loop layer, not
  in lumalinux.

So: no pattern re-derivation, no new hooks. SteamOS behavior is unchanged.

## lumalinux-side items

1. **Cold-check the desktop channel (confirmation, no production change).**
   Extend/parameterise `tools/fetch_steamclient.py` to fetch the
   **`steam_client_ubuntu12`** (generic desktop) `steamclient.so` for the build
   Headcrab currently pins, then run `tools/check_patterns.py` + the SafeMode
   hash gate against it. Expected result: patterns match and the hash equals a
   whitelisted Deck hash. This closes the "identical binary" claim for the exact
   pinned build.

2. **Fix the outdated comment in `tools/fetch_steamclient.py`.** It states the
   Deck and desktop clients "are DIFFERENT builds with different steamclient.so
   hashes." True historically (Dec 2025), but they converged to one hash from
   Jan 2026. Update the note so future maintenance doesn't assume divergence.
   `LUMA_STEAM_MANIFEST` already allows overriding the channel — document the
   desktop value.

3. **Keep `res/updates.yaml` in sync with the pinned build.** lumalinux carries
   its own SafeMode whitelist (independent of SLSsteam's). Because the binary is
   identical across channels, whitelisting the Deck build's hash also covers
   desktop automatically — the only requirement is that the whitelist tracks the
   build Headcrab actually pins to.

4. **`install.sh` Steam-root path (verify, likely no change).** It writes the
   LD_PRELOAD block into `~/.local/share/Steam/steam.sh`, while Headcrab uses
   `$HOME/.steam/steam`. On a stock Linux Steam install `~/.steam/steam` is a
   symlink to `~/.local/share/Steam`, so both target the same file — **verify
   the symlink holds on CachyOS** before assuming it. Everything else in
   `install.sh`/`src` already uses `$HOME`/XDG (no `/home/deck` hardcoding).

## Non-regression

SteamOS/Deck is the tested platform and stays the default. All items above are
either confirmation-only or additive (a new channel option, a doc fix, a
verify). None changes how lumalinux loads or hooks on a Deck. See the master
doc's "Non-regression invariants" section.
