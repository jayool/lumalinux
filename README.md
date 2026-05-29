# lumalinux

A research project on Steam's Linux client internals. `lumalinux` is a 32-bit
shared object that installs four function-level hooks inside `steamclient.so`
on Steam Deck / Linux, exploring how Steam orchestrates depot resolution,
manifest fetching, and decryption-key plumbing during the native install flow.
Designed to **coexist with SLSsteam without modifying or forking it**.

For internals — every hook's signature, how it was found, the Steam
content-install flow, the GMRC endpoint notes, dead-ends, and the Ghidra
reverse-engineering workflow — see [`docs/RESEARCH.md`](docs/RESEARCH.md).

> ⚠️ **Educational / research use only.** Use it with your own Steam account
> and content. Do not redistribute Valve binaries. The author does not host or
> distribute any third-party content; this repo is only the hooking code and
> the RE workflow that made it possible.

## What it does

The hooks live in `steamclient.so` and target the install pipeline. Each one
takes one specific responsibility:

| Hook | Function | Role |
|---|---|---|
| **LoadPackage** | `CPackageInfoCache::LoadPackage` | Adds depot ids into `PackageId=0`'s `AppIdVec` so Steam's per-depot license filter keeps the content depots instead of dropping them. |
| **DepotKey** | `LoadDepotDecryptionKey` | Serves depot AES keys from a local `keys.txt`. |
| **BuildDep** | `CUserAppManager::BuildDepotDependency` | Patches each surfaced depot's `ManifestGid` / `ManifestSize` to pin the right manifest (patch only — never inject new entries). |
| **GMRC** | `CContentServerDirectory…BYieldingGetManifestRequestCode` | Fetches the **manifest request code** from `gmrc.wudrm.com` and returns it through the hook. |

Division of labour:

- **SLSsteam** (loaded via `LD_AUDIT`) → ownership / licensing layer.
- **lumalinux** (loaded via `LD_PRELOAD`) → the four hooks above.

The two are deliberately orthogonal: lumalinux does not touch what SLSsteam
already handles, and SLSsteam does not touch any function lumalinux hooks.

## Why LD_PRELOAD (not LD_AUDIT)

lumalinux is 32-bit and hooks 32-bit `steamclient.so`. Loading it via `LD_AUDIT`
places it in a separate linker namespace and corrupts the heap (observed:
`realloc(): invalid pointer` on startup). Loading via `LD_PRELOAD` keeps it in
the normal namespace and works. The library exports `la_objopen` / `la_preinit`
as a fallback, but **`LD_PRELOAD` is the supported path**.

## Build

32-bit Linux shared object. Needs a multilib toolchain + cmake.

```sh
./tools/fetch_libmem.sh         # one-time: drops the pinned libmem release
                                # tarball into lib/ + include/libmem/
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j
# -> build/liblumalinux.so  (ELF 32-bit i386)
```

`tools/fetch_libmem.sh` downloads the official pre-built static lib + headers
from `rdbo/libmem`'s GitHub release (version + SHA256 pinned in the script).
The fetched files are gitignored, not vendored — bump the version in the
script when upgrading.

CI (`.github/workflows/build.yml`) builds it on every push to `main`, invoking
the same `fetch_libmem.sh` step before cmake, and uploads the `.so` plus the
helper Python tools as an artifact.

## Install / load

Prerequisites: **SLSsteam already installed** (it injects via `LD_AUDIT` in
the Steam launcher chain). lumalinux loads via **`LD_PRELOAD`** (see "Why
LD_PRELOAD").

**1. Deploy the `.so`** (Desktop Mode, Konsole):
```sh
mkdir -p ~/.local/share/lumalinux ~/.config/lumalinux
cp build/liblumalinux.so ~/.local/share/lumalinux/liblumalinux.so
chmod +x ~/.local/share/lumalinux/liblumalinux.so
file ~/.local/share/lumalinux/liblumalinux.so   # must say: ELF 32-bit ... Intel 80386
```
(`./install.sh` does this step and creates an empty `keys.txt`.)

**2. Add lumalinux to the `LD_PRELOAD` line that loads with Steam.** The exact
file depends on which SLSsteam installer you used:

- **Stock SLSsteam (`setup.sh` from AceSLS):** patches `/usr/bin/steam` with
  an `LD_AUDIT=` export. Add a `LD_PRELOAD` line just before the final `exec`:
  ```sh
  sudo steamos-readonly disable
  sudo sed -i '/^exec \/usr\/lib\/steam\/steam/i export LD_PRELOAD="/home/deck/.local/share/lumalinux/liblumalinux.so${LD_PRELOAD:+:}${LD_PRELOAD:-}"' /usr/bin/steam
  ```

- **h3adcr-b (cr-testbranch) — also installs CloudRedirect:** see the next
  section, *"Coexisting with CloudRedirect"*. The fix is different (h3adcr-b
  reshapes `~/.local/share/Steam/steam.sh` and that flow needs lumalinux added
  to its `INJECT_CR` line instead).

> ⚠️ Do **NOT** put lumalinux in the `LD_AUDIT` line — a 32-bit lib in
> LD_AUDIT corrupts the heap and Steam crashes on startup.
> ⚠️ SteamOS updates **reset `/usr/bin/steam`** — re-apply this line after
> each SteamOS update.

**3. Start Steam.** On load you get a desktop toast (via `notify-send`):
- `lumalinux: v0.8.1 loaded — 4/4 hooks active` → all good.
- `lumalinux: v0.8.1: 3/4 hooks — GMRC FAILED. Steam update? See …` → a hook's
  byte pattern stopped matching (usually a Steam update). Re-derive with
  `tools/derive_patterns.py` (Ghidra headless postScript that auto-derives the
  string-anchored hooks and validates the rest — see
  [`docs/RESEARCH.md`](docs/RESEARCH.md) §8.1), paste the fresh patterns into
  `src/patterns.hpp`, and rebuild.

Confirm from the log instead:
```sh
grep -E "v0\.8|hook: INSTALLED|NOTIFY" ~/.cache/lumalinux/lumalinux.log
```
Expect `LoadPackage`, `DepotKey`, `DepotDependency`, `GMRC` all `INSTALLED`.

Log: `~/.cache/lumalinux/lumalinux.log`. Env vars (set before launching):
- `LUMA_NO_NOTIFY` — silence the startup toast.
- `LUMA_NO_LOADPKG` / `LUMA_NO_DEPOTKEY` / `LUMA_NO_BUILDDEP` / `LUMA_NO_GMRC` — disable an individual hook.
- `LUMA_LOADPKG_IDX=N` — pick a different LoadPackage candidate if the default isn't right.

In Game Mode the toast may not render (gamescope replaces the desktop), but
the log file is still written.

## Coexisting with CloudRedirect

CloudRedirect (Selectively11/CloudRedirect) targets the **cloud-save RPC path**
in `steamclient.so` via vtable swaps; lumalinux targets the **content / depot
path** via inline byte hooks. The two hooked sets are disjoint, the mechanisms
don't overlap, and they're designed to run side by side. There's only one
gotcha — the install order of the `LD_PRELOAD` env var.

The h3adcr-b installer (cr-testbranch) drops a wrapper at
`~/.local/share/Steam/steam.sh` that ends with:

```bash
INJECT_SLS="LD_AUDIT=$HOME/.local/share/SLSsteam/library-inject.so:$HOME/.local/share/SLSsteam/SLSsteam.so"
INJECT_CR="LD_PRELOAD=$HOME/.local/share/CloudRedirect/cloud_redirect.so"
GameLauncher(){
    export $INJECT_SLS &> /dev/null
    export $INJECT_CR &> /dev/null
    source $STEAM_CLIENT "$@" &> /dev/null
}
```

The `export $INJECT_CR` line **overwrites** whatever `LD_PRELOAD` you may have
set earlier (e.g. in `/usr/bin/steam` or in the shell env) — it does not append.
If you only add lumalinux to `/usr/bin/steam`, this `export` will erase it and
only `cloud_redirect.so` ends up loaded.

**Fix:** add lumalinux's `.so` to the same `INJECT_CR` line, separated by `:`:

```sh
# Backup first
cp ~/.local/share/Steam/steam.sh ~/.local/share/Steam/steam.sh.bak

# Append lumalinux to the cloud_redirect entry
sed -i 's|cloud_redirect.so"|cloud_redirect.so:/home/deck/.local/share/lumalinux/liblumalinux.so"|' \
    ~/.local/share/Steam/steam.sh

# Verify
grep INJECT_CR ~/.local/share/Steam/steam.sh
```

The line should now read:
```bash
INJECT_CR="LD_PRELOAD=$HOME/.local/share/CloudRedirect/cloud_redirect.so:/home/deck/.local/share/lumalinux/liblumalinux.so"
```

Restart Steam and check the lumalinux log: all four hooks should report
`INSTALLED` alongside CloudRedirect coming up successfully in its own log
(`~/.config/CloudRedirect/cloud_redirect.log` shows `CloudRedirect initialized
successfully`).

If a future `h3adcr-b` update rewrites that `steam.sh`, you'll need to re-apply
the `sed`. Same idea as the SteamOS-update note above.

## Configuring a game

The runtime side is generic — you point lumalinux at any app whose `.lua` +
`.manifest` files you legitimately have, and it fills the gaps that SLSsteam
doesn't cover so the native Install button can complete the flow. Tooling:

```sh
# Places .manifest files into depotcache (+ config/depotcache), writes
# keys.txt in the extended format (depot;parent_app;manifest_gid;size;hexkey),
# and adds the app + depots to SLSsteam's AdditionalApps in config.yaml.
python3 tools/steamidra_lite.py <your-zip>.zip
```

`keys.txt` lives at `~/.config/lumalinux/keys.txt`. `tools/vdf_inject_keys.py`
is a belt-and-suspenders helper that writes depot keys into Steam's
`config.vdf` without the `vdf` Python module — but Steam prunes those entries
for depots you don't own, which is exactly why the runtime DepotKey hook is
the path that actually serves the keys.

## Updating after a Steam / SteamOS update

Two distinct breakage cases. Symptoms tell them apart:

### A) Steam **client** update → a hook's pattern stops matching
Toast says `N/4 hooks — <HOOK> FAILED`; log shows `pattern NOT FOUND`. Fix:

1. **Grab the new `steamclient.so`** from the Deck:
   `~/.local/share/Steam/linux32/steamclient.so`.
2. **Re-derive the patterns** with the maintainer script:
   ```sh
   # one-time import (analysis ~5–15 min):
   analyzeHeadless <proj> sc -import steamclient.so
   # re-derive:
   analyzeHeadless <proj> sc -process steamclient.so \
       -noanalysis -scriptPath tools -postScript derive_patterns.py
   ```
   String-anchored hooks (BuildDep, GMRC) auto-derive; anchorless ones
   (DepotKey, LoadPackage) auto-validate or get flagged for manual re-derive
   (see [`docs/RESEARCH.md`](docs/RESEARCH.md) §8.1).
3. **Paste** the printed patterns into `src/patterns.hpp` and **rebuild and
   redeploy**.

Most updates only move the anchored hooks, which the script fixes
automatically — usually copy-paste-rebuild, not a full RE session.

### B) **SteamOS** update → launcher script reset
SteamOS updates overwrite `/usr/bin/steam`, dropping any `LD_PRELOAD` /
`LD_AUDIT` lines you added. Steam then starts with no hooks at all (no
toast). Fix: re-apply the relevant install step. The deployed `.so` and
`keys.txt` survive — only the launcher line needs re-adding.

## Credits / notes

- Hook design references **LumaCore** (Windows), reimplemented for Linux.
- Coexists with **SLSsteam** (ownership / licensing layer); does not fork or
  modify it.
- Coexists with **CloudRedirect** (cloud-save RPC layer); see the section
  above for the `LD_PRELOAD` ordering note.
- Manifest request codes via **gmrc.wudrm.com** (the endpoint SteaMidra uses;
  the `manifest.steam.run` endpoint LumaCore originally used is dead).
- Research / educational. Use with your own Steam account and content. Do not
  redistribute Valve binaries.
