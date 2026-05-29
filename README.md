# lumalinux

Makes Steam's native **Install** button work on the Steam Deck / Linux for games
you don't own, by hooking `steamclient.so`. Designed to **coexist with SLSsteam**
(it does not replace or modify it).

**Status:** v0.8.0 — working end-to-end. Verified: Balatro (AppID 2379780)
downloads and runs via the native Install button on a Steam Deck (SteamOS),
with SLSsteam loaded and the depot keys / manifests in place.

## What it does

The Linux piracy stack normally needs an external downloader (DepotDownloader)
because SLSsteam only spoofs *ownership* — it doesn't provide depot decryption
keys, doesn't surface the content depots, and can't get the manifest request
code Valve hands out to authorize a download. So the native Install button
either jumps straight to "Play" (0 depots) or dies with *"Missing decryption
key"* / *"No connection"*.

`lumalinux` closes those gaps with four function-level hooks inside
`steamclient.so` (the Linux equivalents of what LumaCore does on Windows):

| Hook | Function | What it does |
|---|---|---|
| **LoadPackage** | `CPackageInfoCache::LoadPackage` | Injects our depot ids into `PackageId=0`'s `AppIdVec`, so Steam's per-depot license filter keeps the content depots instead of dropping them. |
| **DepotKey** | `LoadDepotDecryptionKey` | Serves the depot AES keys from `keys.txt` (Steam can't get them from Valve, and `config.vdf` gets pruned for unowned depots). |
| **BuildDep** | `CUserAppManager::BuildDepotDependency` | Patches each surfaced depot's `ManifestGid`/`ManifestSize` to pin the right manifest (LumaCore-style — patch only, never inject). |
| **GMRC** | `CContentServerDirectory…BYieldingGetManifestRequestCode` | The missing piece: fetches the **manifest request code** from `gmrc.wudrm.com` and returns it as if Valve had authorized the download. |

The division of labour:

- **SLSsteam** (loaded via `LD_AUDIT`) → ownership spoof, PICS access token, family-share bypass.
- **lumalinux** (loaded via `LD_PRELOAD`) → the four hooks above.

## Why LD_PRELOAD (not LD_AUDIT)

lumalinux is a 32-bit library that hooks `steamclient.so` (also 32-bit). Loading
it via `LD_AUDIT` places it in a separate linker namespace and corrupts the heap
(observed: `realloc(): invalid pointer` crash on startup). Loading via
`LD_PRELOAD` runs it in the normal namespace and works. It exports both
`la_objopen`/`la_preinit` and a `constructor` fallback, but **LD_PRELOAD is the
supported path**.

## Build

32-bit Linux shared object. Needs a multilib toolchain + cmake.

```sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j
# -> build/liblumalinux.so  (ELF 32-bit i386)
```

CI (`.github/workflows/build.yml`) builds it on every push to `main`.

## Install / load

1. Copy the built `.so`:
   ```sh
   mkdir -p ~/.local/share/lumalinux
   cp build/liblumalinux.so ~/.local/share/lumalinux/liblumalinux.so
   ```
2. Make Steam's launcher load it via `LD_PRELOAD`. On SteamOS, SLSsteam patches
   `/usr/bin/steam` with an `LD_AUDIT=` line; add lumalinux right **after** it
   and before the final `exec`:
   ```sh
   sudo steamos-readonly disable
   # add this line in /usr/bin/steam, just before `exec /usr/lib/steam/steam ...`:
   export LD_PRELOAD="$HOME/.local/share/lumalinux/liblumalinux.so${LD_PRELOAD:+:}${LD_PRELOAD:-}"
   ```
   (`install.sh` automates copying the `.so` + creating dirs; the launcher edit
   is left manual because the launcher path/format varies by setup.)
3. Log: `~/.cache/lumalinux/lumalinux.log`. Debug env vars to disable individual
   hooks: `LUMA_NO_LOADPKG`, `LUMA_NO_DEPOTKEY`, `LUMA_NO_BUILDDEP`, `LUMA_NO_GMRC`.

## Setting up a game (the full recipe)

You need: SLSsteam installed, lumalinux loaded, and the game's `.lua` +
`.manifest` files (e.g. from a ManifestHub-style zip). Then:

```sh
# places manifests into depotcache (+ config/depotcache), writes keys.txt in the
# extended format (depot;parent_app;manifest_gid;size;hexkey), and adds the app
# + depots to SLSsteam's AdditionalApps in config.yaml
python3 tools/steamidra_lite.py <appid>.zip
```

Restart Steam → click **Install** on the game. lumalinux fetches the manifest
request codes from `gmrc.wudrm.com` on demand and serves the depot keys; Steam
downloads and mounts the depots natively.

`keys.txt` lives at `~/.config/lumalinux/keys.txt`. `tools/vdf_inject_keys.py` is
an optional helper that writes depot keys into Steam's `config.vdf` without
needing the `vdf` python module — but Steam prunes those entries for unowned
depots, which is exactly why the runtime DepotKey hook is what actually serves
the keys.

## Credits / notes

- Hook design mirrors **LumaCore** (Windows).
- Coexists with **SLSsteam** (ownership/licensing); does not fork or modify it.
- Manifest request codes via **gmrc.wudrm.com** (the endpoint SteaMidra uses;
  the `manifest.steam.run` endpoint LumaCore used is dead).
- Educational / for your own Steam setup. Don't redistribute Valve binaries.
