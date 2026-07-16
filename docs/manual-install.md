# Manual install: driving `steamidra_lite.py` by hand

> If you use **LumaDeck**, you don't need any of this: the plugin invokes
> `steamidra_lite.py` for you with the right arguments. This documents the
> manual (desktop, no plugin) flow and the tool's full CLI, for driving
> lumalinux directly with Hubcap-style zips.

Prerequisites: SLSsteam installed and its Headcrab-patched `steam.sh` in place,
plus lumalinux deployed (see the [README Installation](../README.md#installation)).
Run every command with **Steam closed**.

## What one install does

`python3 tools/steamidra_lite.py <appid>.zip` performs the six pieces SteaMidra
Linux's `process_lua_full` does, plus a 7th lumalinux-specific ecosystem-interop
step. The conceptual "why" of each is in [`method.md`](method.md) §3; this is the
operational "what".

1. **Extracts `.manifest` files** into both `~/.local/share/Steam/depotcache/` and
   `~/.local/share/Steam/config/depotcache/` (Steam reads either; writing both
   avoids intermittent "missing manifest").

2. **Adds the AppID** (only the main one, not the depots) to
   `~/.config/SLSsteam/config.yaml` under `AdditionalApps:`.

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
   if it is in `keys.txt`. The step also flips a legacy global `DisableShaderCache`
   from `1` back to `0` if present. Skip the whole step with `--no-vdf` (the
   DepotKey hook covers most cases).

5. **AppToken** (optional, `--token APPID:HEX`, repeatable) for games whose PICS
   appinfo Valve won't return without a token. Most games don't need it.

6. **Writes or resets the `.acf`** (`appmanifest_<appid>.acf`):
   - **`.acf` exists** (re-run or prior attempt): patches the error-state fields
     back to clean (`UpdateResult` / `BytesToDownload` / `Bytes*` / `StagingSize`
     to 0, clears the Update-Required bit of `StateFlags`). Stale state here is
     what surfaces as "NO INTERNET CONNECTION", not a real network problem.
   - **`.acf` absent** (brand-new): writes a clean stub with `StateFlags=1`
     (Uninstalled) and every byte/error counter at 0, deliberately omitting
     `InstalledDepots` / `MountedDepots`, so Steam shows **Install**.

7. **Ecosystem interop** (best-effort, each piece logged, never aborts the run):
   copies the parsed `.lua` to `~/.local/share/Steam/config/stplug-in/<appid>.lua`
   so SteaMidra-style scanners and LumaDeck's library list find the game; drops a
   `.DepotDownloader/` marker in the game's `steamapps/common/<installdir>/` and a
   `~/.local/share/ACCELA/depots/<appid>.depot` tracker so the standalone **ACCELA**
   desktop app recognises it.

**Backups.** Files that already exist get a `.bak` next to the original before any
change: `config.yaml`, `config.vdf`, and (when pre-existing) the `.acf`, the
stplug-in `.lua`, and the `.depot` tracker. `keys.txt` is merged in place without
a `.bak`.

Restart Steam and press **Install** on the game; it downloads natively, with
progress shown in the Steam library.

## Pinning: auto-update vs frozen

By **default (no `--pin`)** the EXTENDED entries are written with `gid` and `size`
set to `0` (no pin), so with nothing pinning the depot the game **auto-updates**
like an owned title. Pass **`--pin`** to write the zip's exact `gid`/`size` into
`keys.txt` (and keep `setManifestid` uncommented in the stplug-in `.lua`) — but
note that `keys.txt`'s only consumer was the **BuildDep hook, which is disabled by
default**, so the zip `--pin` no longer freezes anything on its own. The supported
freeze is `--pin-installed`, which writes SLSsteam `config.yaml` `ManifestIds`
(see below); the zip `--pin` write only takes effect if you launch Steam with
`LUMA_FORCE_BUILDDEP=1`. The tradeoffs, and how to move a pinned game to a new
version, are in [`method.md`](method.md) §6.

## CLI reference

Main install (zip in, full deploy):

| Flag | Effect |
|---|---|
| `<appid>.zip` | Hubcap-style zip (`.lua` + `.manifest` files). The default input. |
| `--manifests-dir <dir>` | Legacy input: a loose `.lua` + manifests directory instead of a zip. |
| `--pin` | Write the zip's manifest gid into `keys.txt` (default is no-pin, which auto-updates). Note: this no longer freezes on its own — its only consumer, the BuildDep hook, is disabled by default; the supported freeze is `--pin-installed` via SLSsteam `ManifestIds`. |
| `--name <name>` | Canonical game name used for the `.acf` `name` + `installdir`, skipping the store-API fetch (avoids the appid-as-installdir fallback). LumaDeck passes it. |
| `--token APPID:HEX` | AppToken for a game that needs one (repeatable). |
| `--no-vdf` | Skip the `config.vdf` DecryptionKeys injection. |
| `--steam-root <dir>` / `--sls-config <file>` / `--luma-keys <file>` | Override the default Steam root, SLSsteam config, and `keys.txt` locations. |

Modes that operate on an **already-deployed** game (no zip, install nothing new):

| Mode | Effect |
|---|---|
| `--accela-mark <appid>` | Recreate the ACCELA `.DepotDownloader` marker + `.depot` tracker once the game's files exist. Reads the real `installdir` from the `.acf` and recovers the depot/manifest from the stplug-in `.lua`. Idempotent. |
| `--pin-installed <appid>` | Freeze an already-installed game to its current manifest by writing its depot→gid map into SLSsteam `config.yaml` `ManifestIds` (touches none of `keys.txt` / stplug-in / depotcache). |
| `--unpin <appid>` | Un-freeze an installed game so it auto-updates again. |
| `--pin-status <appid>` | Report whether an installed game is pinned. |

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

`tools/vdf_inject_keys.py` is the same VDF logic as step 4 in a standalone script
(its own text parser, no `vdf` module) if you need to inject keys into `config.vdf`
without re-running the whole pipeline.
