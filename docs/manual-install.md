# Manual install: driving `steamidra_lite.py` by hand

> If you use **LumaDeck**, you don't need any of this: the plugin invokes
> `steamidra_lite.py` for you with the right arguments. This documents the
> manual (desktop, no plugin) flow and the tool's full CLI, for driving
> lumalinux directly with Hubcap-style zips.

Prerequisites: the injection stack installed and reachable — SLSsteam + lumalinux
deployed and loaded through the **wrapper** at `~/.local/share/SLSsteam/path/steam`
(the model `setup.sh` installs, or LumaDeck's Quick Install). This replaces the old
Headcrab-patched `steam.sh` model: `steam.sh` is left vanilla and injection comes
from the wrapper (see the [README Installation](../README.md#installation) and
[`nosotros.md`](nosotros.md) §2.1). Note `steamidra_lite.py`
below is about **installing a game** into that stack; it is independent of how the
`.so`s are injected. Run every command with **Steam closed**.

## What one install does

`python3 tools/steamidra_lite.py <appid>.zip` performs the six pieces SteaMidra
Linux's `process_lua_full` does, plus a 7th lumalinux-specific ecosystem-interop
step. The conceptual "why" of each is in [`nosotros.md`](nosotros.md) §2.9 (add/remove)
and §2.4 (keys and manifests); this is the operational "what".

1. **Extracts `.manifest` files** into `~/.local/share/Steam/depotcache/` only.
   (The `config/depotcache/` copy SteaMidra writes was dropped in 2026-09: tested,
   Steam does not read it.) In the default no-pin mode the run also deletes older
   manifests of this game's content depots from `depotcache/`, keeping only the
   zip's, so a manifest left by an earlier pinned install can't hold the game back
   and Steam can follow Valve's current one. Shared depots and other games'
   manifests are left alone.

2. **Adds the AppID** (only the main one, not the depots) to
   `~/.config/SLSsteam/config.yaml` under `AdditionalApps:`. (With
   `--dlc-of-owned`: only the DLC AppIDs; the owned base game stays out.)

3. **Writes `~/.config/lumalinux/keys.txt`.** Every keyed depot, whether a content
   depot or a shared one (VC Redist, etc.), is written in the **EXTENDED** format
   `depot;parent_app;manifest_gid;size;key`, and the lumalinux DepotKey hook
   serves all of them. A `-- SHARED DEPOTS` header in the Hubcap `.lua` is still
   parsed, but only to print an informational line and to exclude shared depots
   from the no-pin stale-manifest prune; it no longer changes the key format.
   The **AppID entry** is separate: if the `.lua` ships a key for the app id
   (`addappid(APP_ID, 1, "real_key")`) it is written LEGACY `app_id;<real_key>`
   and the DepotKey hook serves it; if the `.lua` only has `addappid(APP_ID)`, it
   is written **presence-only** `app_id;` (empty key field) so KeyStore lists the
   id (the finder injects it into `AppIdVec`) while the DepotKey hook passes
   through to Steam's original. Presence-only is also what the **ShaderDepot** hook
   keys on to skip the game's shader pre-cache cleanly.

4. **Injects the DecryptionKeys into `~/.local/share/Steam/config/config.vdf`**
   under `InstallConfigStore > Software > Valve > Steam > depots`. The tool edits
   the file as text (no `vdf` Python module needed). It no-ops only if `config.vdf`
   or its `depots` block is absent. The main AppID is filtered out of the VDF even
   if it is in `keys.txt`. Skip the whole step with `--no-vdf` (the DepotKey hook
   covers most cases). The tool deliberately does **not** touch
   `DisableShaderCache` — that is the user's own Steam "Shader Pre-Caching"
   setting, and the per-game ShaderDepot hook (§13.9) handles keyless games
   without any global flag.

5. **AppToken** (optional, `--token APPID:HEX`, repeatable) for games whose PICS
   appinfo Valve won't return without a token. Written to the same SLSsteam
   `config.yaml`, under `AppTokens:`. Most games don't need it.

6. **Resets the `.acf` error state** (`appmanifest_<appid>.acf`), only if one
   already exists: patches the error-state fields back to clean (`UpdateResult` /
   `BytesToDownload` / `Bytes*` / `StagingSize` to 0, clears the Update-Required
   bit of `StateFlags`). Stale state here is what surfaces as "NO INTERNET
   CONNECTION", not a real network problem. `ScheduledAutoUpdate` and
   `FullValidateAfterNextUpdate` are left alone — they are work Steam scheduled
   for itself, not error residue. Skipped entirely with `--dlc-of-owned` (the base
   game is the account's own and Steam manages it).

   **If there is no `.acf`, nothing is written.** Steam creates it when you click
   Install, in whichever library you pick. We used to seed a stub here; it was
   measured to buy nothing and to cost a real bug when the game went to a second
   library (LumaDeck issue #41).

7. **Ecosystem interop** (best-effort, each piece logged, never aborts the run):
   copies the parsed `.lua` to `~/.local/share/Steam/config/stplug-in/<appid>.lua`
   so SteaMidra-style scanners and LumaDeck's library list find the game.

   The ACCELA/ASSella markers (`.DepotDownloader/` in the game folder, and
   `~/.local/share/ACCELA/depots/<appid>.depot`) are no longer written at all
   (removed 2026-10-06): they made games the account owns look like ACCELA
   installs, and running both tools on one Deck is unsupported.

**Backups.** Files that already exist get a `.bak` next to the original before any
change: `config.yaml`, `config.vdf`, and (when pre-existing) the stplug-in `.lua`.
The `.acf` gets one only when the error-state patch actually changes something —
an already-clean manifest is left alone, so a run no longer litters `.acf.bak`
files. `keys.txt` is merged in place without a `.bak`.

Start Steam again and press **Install** on the game; it downloads natively, with
progress shown in the Steam library.

The **no-restart** behaviour (the license reconcile, nosotros.md §2.2) does not apply
to this flow, and this doc used to claim it did. The reconcile lives inside the
injected `.so`: the `keys.txt` inotify watcher arms it and the package-0 finder
fires it, both on threads inside a **running** Steam. With Steam closed — as every
command above requires — there is nothing loaded to arm, so starting Steam is what
picks the new game up, exactly as before v0.16.16. What the reconcile removes is
the restart when a game is added while Steam is **up**: LumaDeck's Add Game, and
equally any `keys.txt` write from a manual run with Steam running. (If the
reconcile pattern is broken on your build it no-ops and even that path falls back
to a restart — `LUMA_NO_RECONCILE` forces that old behaviour on purpose.)

## Pinning: auto-update vs frozen

The live pin is SLSsteam `config.yaml` `ManifestIds` (depot → manifest gid).

By **default (no `--pin`)** the EXTENDED entries are written with `gid` and `size`
set to `0`, any `ManifestIds` entries this game's content depots had are removed,
stale manifests are pruned from `depotcache/` (step 1), and the `setManifestid`
lines in the stplug-in `.lua` are commented out — so the game **auto-updates**
like an owned title.

**`--pin`** freezes the game at the zip's version: it writes the zip's gids into
`ManifestIds` (shared redistributables, depot 228980, are never pinned), keeps
`setManifestid` uncommented in the stplug-in `.lua`, and also writes the zip's
`gid`/`size` into `keys.txt` — that last part is inert unless Steam is launched
with `LUMA_FORCE_BUILDDEP=1`, since the BuildDep hook that reads it is disabled by
default. Every `ManifestIds` write merges with what is there, so other games' pins
are kept.

To freeze an already-installed game at its *installed* version use
`--pin-installed`; to move a pin to specific gids (a new build whose manifests are
already in `depotcache/`), `--set-pin`; to go back to auto-update, `--unpin`. The
tradeoffs are in [`nosotros.md`](nosotros.md) §2.5.

## CLI reference

Main install (zip in, full deploy):

| Flag | Effect |
|---|---|
| `<appid>.zip` | Hubcap-style zip (`.lua` + `.manifest` files). The default input. |
| `--manifests-dir <dir>` | Legacy input: a loose `.lua` + manifests directory instead of a zip. |
| `--pin` | Freeze at the zip's version: writes the zip's gids into SLSsteam `ManifestIds` (and into `keys.txt`, inert while BuildDep is off). Default (no `--pin`) is auto-update: it clears this game's `ManifestIds` entries and prunes its older manifests from `depotcache/`. |
| `--dlc-of-owned` | DLC for a game the account already owns. Parses a keyless `.lua`, adds only the DLC AppIDs to `AdditionalApps` (the base game stays out) and leaves the game's `.acf` alone. The `.lua` must carry only DLC depots; LumaDeck filters it before calling. Background: RESEARCH §21. |
| `--name <name>` | **Accepted and ignored.** It fed the `installdir` of the `.acf` stub, which we no longer write (Steam writes the manifest on Install). |
| `--token APPID:HEX` | AppToken for a game that needs one (repeatable). |
| `--no-vdf` | Skip the `config.vdf` DecryptionKeys injection. |
| `--steam-root <dir>` / `--sls-config <file>` / `--luma-keys <file>` | Override the default Steam root, SLSsteam config, and `keys.txt` locations. |

Modes that operate on an **already-deployed** game (no zip, install nothing new):

| Mode | Effect |
|---|---|
| `--pin-installed <appid>` | Freeze an already-installed game to its current manifest by writing its depot→gid map into SLSsteam `config.yaml` `ManifestIds` (touches none of `keys.txt` / stplug-in / depotcache). |
| `--set-pin <appid> <depot>:<gid> [<depot>:<gid> …]` | Merge those gids into SLSsteam `ManifestIds` (how LumaDeck moves a pin to a new build once its manifests are in `depotcache/`). Refuses gid `0`; refuses, writing nothing, if any depot has no key in `keys.txt`; never pins shared redistributables. |
| `--unpin <appid>` | Un-freeze a game so it auto-updates again: removes its content depots (per `keys.txt`) from `ManifestIds`. |
| `--pin-status <appid>` | Print JSON `{"appid", "pinned", "depots"}` from SLSsteam `ManifestIds`, restricted to the app's content depots in `keys.txt` (for LumaDeck). |

## keys.txt formats

`keys.txt` lives at `~/.config/lumalinux/keys.txt`. Three line shapes:

- **EXTENDED** (all keyed depots, content and shared):
  `depot;parent_app;manifest_gid;size;key`. The DepotKey hook serves the key.
  (Manifest pinning is **not** driven from here: it's owned by SLSsteam
  `config.yaml` `ManifestIds`; the `gid`/`size` fields fed the BuildDep hook,
  which is disabled by default.)
- **LEGACY** (the app id, when the `.lua` ships a real key for it): `app_id;<key>`.
  The DepotKey hook serves it.
- **presence-only** (the app id, no key): `app_id;` (empty key field). Listed so
  the finder injects the id; the DepotKey hook passes through; the ShaderDepot hook
  uses it to skip the shader pre-cache.

**`retired_keys.txt`**, next to `keys.txt`, uses the same line format. LumaDeck
moves an owned game's DLC lines there when it uninstalls them: Steam still needs
those keys to delete the depot's files, so the DepotKey hook serves them, but they
carry no licence (nothing in it is injected by the finder or used for manifests).
`steamidra_lite.py` does not write it.

`tools/vdf_inject_keys.py` is the same VDF logic as step 4 in a standalone script
(its own text parser, no `vdf` module) if you need to inject keys into `config.vdf`
without re-running the whole pipeline.
