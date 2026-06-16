# lumalinux

A 32-bit shared object loaded into `steamclient.so` on Steam Deck / Linux that
enables depot resolution, manifest fetching, and decryption-key plumbing during
the native install flow. It runs **three function-level hooks** plus an **active
package-0 finder** (a worker thread that walks the live package cache instead of
waiting for a hook event — see [RESEARCH §13](docs/RESEARCH.md)). A fourth,
diagnostic-only LoadPackage hook is opt-in via `LUMA_LOADPKG_DEBUG=1`.
Designed to **coexist with SLSsteam without modifying or forking it**.

> ⚠️ **Educational / research use only.** Use it with your own Steam account
> and content. Do not redistribute Valve binaries. The author does not host or
> distribute any third-party content; this repo is only the hooking code and
> the RE workflow that made it possible.

## What it does

Each piece lives in `steamclient.so` and takes one specific responsibility:

| Piece | Function / target | Role |
|---|---|---|
| **package-0 finder** | walks `CPackageInfoCache` directly | Adds depot ids into `PackageId=0`'s `AppIdVec` so Steam's per-depot licence filter keeps the content depots instead of dropping them. Active worker (own thread, runs by default), so it works even when Steam has `PackageId=0` cached and never re-calls `LoadPackage` — see [RESEARCH §13](docs/RESEARCH.md). |
| **DepotKey** hook | `LoadDepotDecryptionKey` | Serves depot AES keys from a local `keys.txt`. |
| **BuildDep** hook | `CUserAppManager::BuildDepotDependency` | Patches each surfaced depot's `ManifestGid` to pin the right manifest (patch only — never injects new entries). |
| **GMRC** hook | `…BYieldingGetManifestRequestCode` | Fetches the **manifest request code** from `gmrc.wudrm.com` and returns it through the hook. |
| *LoadPackage* hook (opt-in) | `CPackageInfoCache::LoadPackage` | Diagnostic only since v0.13.1. Installed only when `LUMA_LOADPKG_DEBUG=1` is set; logs every `PackageId + AppIdVec` triple. Does not inject — the finder does. |

Division of labour with SLSsteam:

- **SLSsteam** → ownership / licensing layer. Fakes ownership, supplies PICS
  access tokens, handles family-share/offline.
- **lumalinux** → the three install-path hooks plus the package-0 finder above.

The two are deliberately orthogonal: lumalinux does not touch what SLSsteam
already handles, and SLSsteam does not touch any function lumalinux hooks.

## Installation

Two supported paths.

### Recommended (gamemode, with LumaDeck)

Set up in desktop mode, then switch to gamemode.

1. **ACCELA + SLSsteam** via enter-the-wired:

   ```bash
   curl -fsSL https://raw.githubusercontent.com/ciscosweater/enter-the-wired/main/enter-the-wired | bash
   ```

   Installs ACCELA and SLSsteam. Creates `~/.config/SLSsteam/config.yaml`
   with `DisableCloud: yes` by default.

2. **(Optional) CloudRedirect** — for cloud-save sync via third-party providers:

   Edit `~/.config/SLSsteam/config.yaml` and change the `DisableCloud` line
   from `yes` to `no`. Then run:

   ```bash
   curl -fsSL https://headcrab.pages.dev | bash
   ```

   Open the CloudRedirect app once to sign into your cloud provider.

3. **lumalinux**:

   ```bash
   curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/install.sh | bash
   ```

   Detects CloudRedirect if present and prepends `liblumalinux.so` to the
   existing `LD_PRELOAD` so both load.

4. **(Optional) Decky Loader + LumaDeck** — for the gamemode UI:
   - Decky Loader: <https://decky.xyz/>
   - LumaDeck: download from <https://github.com/jayool/LumaDeck> and install
     through Decky as a custom plugin.

5. **.NET 9 runtime** — required by ACCELA for some features:

   ```bash
   curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 9.0 --runtime dotnet
   ```

### Minimal (desktop, manual depot ZIPs)

For users who'd rather drop pre-made depot ZIPs instead of using the
LumaDeck UI.

1. **ACCELA + SLSsteam** (same as above):

   ```bash
   curl -fsSL https://raw.githubusercontent.com/ciscosweater/enter-the-wired/main/enter-the-wired | bash
   ```

2. **lumalinux**:

   ```bash
   curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/install.sh | bash
   ```

3. **Add games** with `steamidra_lite` for each depot ZIP:

   ```bash
   python3 tools/steamidra_lite.py <appid>.zip
   ```

   Restart Steam — the game shows up unlocked.

### After a Headcrab/SLSsteam update

Headcrab regenerates `~/.local/share/Steam/steam.sh` whenever its updater
runs. That erases lumalinux's patch. Re-run the lumalinux installer to
reapply:

```bash
curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/install.sh | bash
```

### Tested platforms

- **Recommended path**: SteamOS gamemode (Steam Deck)
- **Minimal path**: SteamOS desktop
- Other Arch-based distros (Bazzite, CachyOS, etc.) should work but are
  untested

## Usage

Once lumalinux is loaded, you configure a game by pointing it at a Hubcap-style
zip (`.lua` + `.manifest` files you legitimately have). Run with Steam
**closed**:

```sh
python3 tools/steamidra_lite.py <appid>.zip
```

That single command does the full deploy (depotcache manifests, `keys.txt`,
SLSsteam config entry, `config.vdf` keys, `.acf` stub, the `stplug-in` lua,
and the ACCELA markers). The detailed step-by-step is in
[Configuring a game](#configuring-a-game-advanced) below.

If you use **LumaDeck**, the QAM plugin calls this script under the hood when
you tap "Download Manifest". You don't need to invoke it manually.

## Troubleshooting

Log: `~/.cache/lumalinux/lumalinux.log`.

### Env vars (set before launching Steam)

- `LUMA_NO_NOTIFY` — silence the startup toast.
- `LUMA_LOG_LEVEL` — log verbosity: `error` / `warn` / `info` / `debug` (or
  `0`–`3`). Default `info`. The per-call `[DEBUG]` traces (e.g. BuildDep on
  every depot query) are suppressed by default; set `LUMA_LOG_LEVEL=debug` when
  actively debugging.
- `LUMA_NO_DEPOTKEY` / `LUMA_NO_BUILDDEP` / `LUMA_NO_GMRC` — disable an
  individual install-path hook.
- `LUMA_NO_PKG0_FINDER=1` — disable the package-0 finder. The finder is **on by
  default** (since v0.13.0) and is the **sole** depot injector: it walks the
  package cache directly, which is what makes installs work when Steam has
  `PackageId=0` already cached and never calls `LoadPackage`. See
  [`docs/RESEARCH.md` §13](docs/RESEARCH.md).
- `LUMA_PKG0_FINDER=diag` — run the finder in log-only mode (walks + logs, does
  not inject). Default (unset) is inject.
- `LUMA_LOADPKG_DEBUG=1` — **install and enable** the diagnostic LoadPackage
  hook. Off by default since v0.13.1 (the hook no longer injects, so it serves
  no purpose in normal operation; installing it unconditionally was just an
  extra detour + a misleading "LoadPackage FAILED" toast when its byte pattern
  drifted). When on, it logs every `PackageId + AppIdVec` triple — useful for
  diagnosing the kind of regression that produced §13.
- `LUMA_LOADPKG_IDX=N` — only relevant with `LUMA_LOADPKG_DEBUG=1`; pick a
  different `LoadPackage` candidate when the pattern matches multiple sites.

### Install hangs at "0 target depots" / "Fully Installed" with 0 bytes

The depot ids aren't reaching `PackageId=0`. In a healthy run the log shows the
finder finding and seeding the package after login:

```
PKG0_FINDER: GOT=0x… disp=0x… cache_global=0x…
PKG0_FINDER: HIT pkg=… PackageId=0 AppIdVec{…}
LoadPackage[finder]: APPENDED N depot id(s) to PackageId=0
```

If you never see `PKG0_FINDER: HIT`, the finder couldn't locate the package
cache — usually because a Steam update moved one of the anchors it derives
addresses from (the GMRC prologue tail or the cache-access idiom; see
[RESEARCH §13](docs/RESEARCH.md)). Possible warnings to check for:

- `cache-access idiom not found in r-x — class layout changed?` → the cache's
  internal class layout (the `0xc58` root-offset anchor) moved.
- `GOT not derived yet (GMRC prologue tail not located)` → the GMRC prologue
  tail anchor moved.

In either case, the package-0 path needs a runtime-derivation update; open an
issue with that log line.

### "N/3 hooks — XXX FAILED" toast

A byte pattern stopped matching. Almost always a Steam client update.

- **DepotKey / BuildDep / GMRC FAILED** → installs WILL break. Re-derive the
  patterns; see [`docs/maintenance.md`](docs/maintenance.md).
- **LoadPackage FAILED** (only visible with `LUMA_LOADPKG_DEBUG=1`) → harmless;
  the diagnostic hook didn't install but the finder still works and installs
  succeed. You can ignore it unless you needed the diagnostic.

### After a SLSsteam update, Steam loads with no toast at all

The Headcrab Updater regenerated `~/.local/share/Steam/steam.sh` and the
lumalinux block went with it. Re-run the lumalinux installer:

```bash
curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/install.sh | bash
```

The deployed `.so` and `keys.txt` survive — only the launcher block needs
re-adding.

## Configuring a game (advanced)

> If you use LumaDeck, skip this section — the plugin invokes
> `steamidra_lite.py` for you with the right arguments. This section
> describes what happens under the hood and how to drive it manually.

`python3 tools/steamidra_lite.py <appid>.zip` does the **six** pieces SteaMidra
Linux's `process_lua_full` does (see `sff/ui.py`), plus a **7th
lumalinux-specific ecosystem-interop step**:

1. **Extracts `.manifest` files** into both `~/.local/share/Steam/depotcache/`
   AND `~/.local/share/Steam/config/depotcache/` (Steam reads either; syncing
   both avoids intermittent "missing manifest").

2. **Adds the AppID** (only the main one — not the depots) to
   `~/.config/SLSsteam/config.yaml` under `AdditionalApps:`. Replicates
   `sff/app_injector/sls.py:add_ids` exactly.

3. **Writes `~/.config/lumalinux/keys.txt`** with the right format per depot:
   - **Content depots** → EXTENDED: `depot;parent_app;manifest_gid;size;key`
     (lumalinux injects these in `pDepotInfo` via the BuildDep hook).
   - **Shared depots** (e.g. VC Redist that Steam already has in
     `pSharedDepotInfo`) → LEGACY: `depot;key` (no injection — just serve the
     key when Steam asks). Detected from the `-- SHARED DEPOTS` header in the
     Hubcap `.lua`.
   - **AppID entry**. If the Hubcap `.lua` ships a key for the app id itself
     (`addappid(APP_ID, 1, "real_key")`), it's written as LEGACY
     `app_id;<real_key>` and our DepotKey hook serves it. If the `.lua` has no
     such key (`addappid(APP_ID)` only), the entry is written as **presence-only**
     `app_id;` (empty key field) — KeyStore loads it with `has_key=false`, so
     `GetAllDepotIds()` lists the id (the finder injects it into `AppIdVec`) but
     the DepotKey hook passes through to Steam's original for it. Matches
     LumaCore's `DepotKeySet[id] = ""` semantics.

4. **Injects the DecryptionKeys into `~/.local/share/Steam/config/config.vdf`**
   under `InstallConfigStore > Software > Valve > Steam > depots`. Optional
   (`--no-vdf` skips it; the lumalinux DepotKey hook covers most cases).
   Requires `pip install --user vdf` for the VDF library; without it the
   step is skipped with a warning.

5. **AppToken** (optional, `--token APPID:HEX`) — only needed for games whose
   PICS appinfo Valve doesn't return without a token. Most games don't need it.

6. **Writes or resets the `.acf`** (`appmanifest_<appid>.acf`). Two cases:
   - **`.acf` exists** (you've installed before, or this is a re-run after a
     prior install attempt): patches the error-state fields back to clean
     (`UpdateResult` / `BytesToDownload` / `Bytes*` / `StagingSize` → 0, and
     clears the Update-Required bit of `StateFlags`). Replicates
     `sff/lua/writer.py:_patch_acf_error_state`, whose own comment reads
     *"this is what causes 'NO INTERNET CONNECTION'"*. Stale state in the
     `.acf` is what causes that message, not a real network problem.
   - **`.acf` doesn't exist** (brand-new install): writes a clean stub with
     `StateFlags=1` (Uninstalled) and every error/byte counter at 0,
     deliberately omitting `InstalledDepots` / `MountedDepots`. Steam reads
     the stub, sees an owned-but-uninstalled app, and shows Install — the
     Linux equivalent of what LumaCore lets Steam write itself on Windows
     when the IPC ownership ticket succeeds. The stub is also there so that
     `UpdateResult` / `Bytes*` are already 0 when Steam starts downloading,
     so a transient hiccup mid-download doesn't surface as "No internet" in
     the UI. Note: this depends on the gmrc shader-cache fix (commit
     `b4c4968`) being in place, otherwise Steam's shader pre-cache phase
     produces a transient "No connection" placeholder when the `.acf` is
     Uninstalled.

Beyond those six, the script also performs a **7th, lumalinux-specific step**
(*not* part of SteaMidra's `process_lua_full`). It's best-effort — each piece
is logged and never aborts the run:

7. **Ecosystem interop.**
   - Copies the parsed `.lua` to
     `~/.local/share/Steam/config/stplug-in/<appid>.lua` so SteaMidra-style
     scanners and LumaDeck's library list find the game.
   - Drops an in-game `.DepotDownloader/` marker inside the game's
     `steamapps/common/<installdir>/` and a `~/.local/share/ACCELA/depots/<appid>.depot`
     update tracker, so the standalone **ACCELA / ASSella** desktop app treats
     the game as one of its own (its scanner only needs the marker folder to
     exist next to real game content + a matching `.acf`).

   The in-game marker is timing-sensitive: when you first run the script the
   game isn't downloaded yet, so `steamapps/common/<installdir>/` is empty and
   ACCELA won't list it until Steam has pulled the files. For a guaranteed
   post-download marking, re-run in **mark-only** mode once the game is
   installed:

   ```sh
   python3 tools/steamidra_lite.py --accela-mark <appid>
   ```

   `--accela-mark` installs nothing. It reads the real `installdir` from
   `appmanifest_<appid>.acf` (the folder Steam actually used) and recovers the
   depot/manifest from the stplug-in `.lua`, then (re)creates the
   `.DepotDownloader` marker + `.depot` tracker in the right place. Idempotent.
   LumaDeck calls this automatically on library refresh; you only need it by
   hand in the manual (desktop) flow.

`keys.txt` lives at `~/.config/lumalinux/keys.txt`. Backups (`.bak`) of every
file touched are written next to the originals before any change.

`tools/vdf_inject_keys.py` is the same VDF logic in a standalone script — use
it if you need to inject keys without re-running the full pipeline.

## How it works (deep dive)

### Why LD_PRELOAD (not LD_AUDIT)

lumalinux is 32-bit and hooks 32-bit `steamclient.so`. Loading it via
`LD_AUDIT` places it in a separate linker namespace and corrupts the heap
(observed: `realloc(): invalid pointer` on startup). Loading via `LD_PRELOAD`
keeps it in the normal namespace and works. The library exports `la_objopen`
/ `la_preinit` as a fallback, but **`LD_PRELOAD` is the supported path**.

The injection point is `~/.local/share/Steam/steam.sh`, the user-local
launcher wrapper that Headcrab installs and maintains as part of the
SLSsteam setup. `install.sh` inserts the `LD_PRELOAD` export there with a
preserve pattern so any pre-existing `LD_PRELOAD` (e.g. CloudRedirect's)
survives. `/usr/bin/steam` is **not** touched, so the system file stays
vanilla and survives `pacman -Syu steam`. See
[RESEARCH §5](docs/RESEARCH.md) for the exact anchor and patch format.

### Related docs

- [`docs/method.md`](docs/method.md) — **the method**: how an unowned-game
  install actually works end to end (the six gates, where the bytes come from,
  the phase-by-phase native flow, and a side-by-side of lumalinux vs
  LumaCore/SteaMidra vs SteamTools/OpenSteamTool)
- [`docs/RESEARCH.md`](docs/RESEARCH.md) — every hook's signature, RE
  workflow, GMRC endpoint notes, dead-ends, the full LumaCore parity table
- [`docs/cloudredirect.md`](docs/cloudredirect.md) — running side by side
  with CloudRedirect's flatpak; `LD_PRELOAD` ordering
- [`docs/maintenance.md`](docs/maintenance.md) — fixing things after a Steam
  client or SteamOS update

## Build infrastructure

`tools/fetch_libmem.sh` downloads the official pre-built static lib + headers
from `rdbo/libmem`'s GitHub release (version + SHA256 pinned in the script).
The fetched files are gitignored, not vendored — bump the version in the
script when upgrading.

CI (`.github/workflows/build.yml`) builds it on every push to `main`, invoking
the same `fetch_libmem.sh` step before cmake, and uploads the `.so` plus the
helper Python tools as an artifact.

## Credits / notes

- Hook design references **LumaCore** (Windows), reimplemented for Linux.
- Coexists with **SLSsteam** (ownership / licensing layer); does not fork or
  modify it. The SafeMode hash-whitelist update flow (`src/update.cpp`,
  `src/curl.cpp`, `src/sha256.cpp`, `src/globals.cpp`, `res/version.txt`,
  `res/updates.yaml`) was ported from SLSsteam (AGPL-3.0) with minimal
  adaptations — see the AGPL-3.0 header in each of those files. The full
  AGPL-3.0 text is in [`LICENSE-AGPL`](LICENSE-AGPL); the rest of lumalinux
  remains under GPL-3.0 ([`LICENSE`](LICENSE)). Per AGPL-3.0 §13 the
  network-interaction clause applies to the combined binary; in practice
  inert for lumalinux because it is not a network service.
- Coexists with **CloudRedirect** (cloud-save RPC layer); see
  [`docs/cloudredirect.md`](docs/cloudredirect.md) for the `LD_PRELOAD`
  ordering note.
- Manifest request codes via **gmrc.wudrm.com** (the endpoint SteaMidra uses;
  the `manifest.steam.run` endpoint LumaCore originally used is dead).
- Research / educational. Use with your own Steam account and content. Do not
  redistribute Valve binaries.
