# lumalinux

Makes Steam's native **Install** button work on the Steam Deck / Linux for games
you don't own, by hooking `steamclient.so`. Designed to **coexist with SLSsteam**
(it does not replace or modify it).

**Status:** v0.8.0 — working end-to-end. Verified: Balatro (AppID 2379780)
downloads and runs via the native Install button on a Steam Deck (SteamOS),
with SLSsteam loaded and the depot keys / manifests in place.

> 📒 **Extending this?** Read [`docs/RESEARCH.md`](docs/RESEARCH.md) first — full
> internals, every hook's signature/pattern and how it was found, the Steam
> content-install flow, the dead-ends we hit (and why), the GMRC endpoint notes,
> and the Ghidra reverse-engineering workflow to re-derive patterns on a new
> Steam build.

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

## Install / load (the exact recipe that works on a Steam Deck)

Prerequisites: **SLSsteam already installed** (it injects via `LD_AUDIT` in
`/usr/bin/steam`). lumalinux loads via **`LD_PRELOAD`** (see "Why LD_PRELOAD").

**1. Deploy the `.so`** (Desktop Mode, Konsole):
```sh
mkdir -p ~/.local/share/lumalinux ~/.config/lumalinux
cp build/liblumalinux.so ~/.local/share/lumalinux/liblumalinux.so
chmod +x ~/.local/share/lumalinux/liblumalinux.so
file ~/.local/share/lumalinux/liblumalinux.so   # must say: ELF 32-bit ... Intel 80386
```
(`./install.sh` does this step and creates an empty `keys.txt`.)

**2. Add the `LD_PRELOAD` line to `/usr/bin/steam`.** SteamOS makes `/usr/bin/steam`
the entry point and SLSsteam patches it with an `LD_AUDIT=` export. Add lumalinux
as `LD_PRELOAD` just **before the final `exec`**:
```sh
sudo steamos-readonly disable
sudo sed -i '/^exec \/usr\/lib\/steam\/steam/i export LD_PRELOAD="/home/deck/.local/share/lumalinux/liblumalinux.so${LD_PRELOAD:+:}${LD_PRELOAD:-}"' /usr/bin/steam
tail -3 /usr/bin/steam
```
The tail should look like:
```sh
export LD_AUDIT="/home/deck/.local/share/SLSsteam/library-inject.so:/home/deck/.local/share/SLSsteam/SLSsteam.so"
export LD_PRELOAD="/home/deck/.local/share/lumalinux/liblumalinux.so${LD_PRELOAD:+:}${LD_PRELOAD:-}"
exec /usr/lib/steam/steam -steamdeck "$@"
```
> ⚠️ Do **NOT** put lumalinux in the `LD_AUDIT` line — a 32-bit lib in LD_AUDIT
> corrupts the heap and Steam crashes on startup (`realloc(): invalid pointer`).
> ⚠️ SteamOS **updates reset `/usr/bin/steam`** — re-apply this line after each
> SteamOS update.

**3. Start Steam.** On load you get a desktop toast (via `notify-send`, same as
SLSsteam):
- `lumalinux: v0.8.1 loaded — 4/4 hooks active` → all good.
- `lumalinux: v0.8.1: 3/4 hooks — GMRC FAILED. Steam update? See …` → a hook's
  byte pattern stopped matching (almost always a Steam update). Re-derive the
  patterns (see [`docs/RESEARCH.md`](docs/RESEARCH.md) §8) and rebuild.

To confirm from the log instead:
```sh
grep -E "v0\.8|hook: INSTALLED|NOTIFY" ~/.cache/lumalinux/lumalinux.log
```
Expect `LoadPackage`, `DepotKey`, `DepotDependency`, `GMRC` all `INSTALLED`.

Log: `~/.cache/lumalinux/lumalinux.log`. Env vars (set before launching):
- `LUMA_NO_NOTIFY` — silence the startup toast.
- `LUMA_NO_LOADPKG` / `LUMA_NO_DEPOTKEY` / `LUMA_NO_BUILDDEP` / `LUMA_NO_GMRC` — disable a hook.
- `LUMA_LOADPKG_IDX=N` — pick a different LoadPackage candidate if it grabs the wrong one.

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
