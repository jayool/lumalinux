# lumalinux

A 32-bit shared object loaded into Steam's `steamclient.so` on Steam Deck / Linux.
It supplies the depot keys, manifests, and package plumbing that Steam's **native
Install** needs to download a game you configured locally, so installs go through
Steam itself with no external downloader.

Designed to run **alongside SLSsteam** (never forking it). Its install-path hooks
stay out of SLSsteam's way, and one small, reversible in-memory patch to
`SLSsteam.so` additionally lets LumaDeck games earn native achievements (auto-update
is handled at the config level — see [What it does](#what-it-does)).

> ⚠️ **Educational / research use only.** Use it with your own Steam account
> and content. Do not redistribute Valve binaries. The author does not host or
> distribute any third-party content; this repo is only the hooking code and
> the RE workflow that made it possible.

## Installation

Two paths: the LumaDeck plugin (recommended, tested) or a manual install.

### Recommended: LumaDeck (gamemode)

1. Install **Decky Loader** (<https://decky.xyz/>).
2. Install **LumaDeck** as a custom plugin in Decky: download the zip from
   <https://github.com/jayool/LumaDeck> and point Decky at it.
3. Open LumaDeck in the QAM and tap **Quick Install**. On a fresh setup it installs
   and configures everything in one tap, in the right order: SLSsteam +
   CloudRedirect, the .NET 9 runtime, and lumalinux — all via the self-contained
   wrapper installer (`setup.sh`). For individual pieces or after a Steam update, use
   **Settings → Dependencies**.
4. (Optional) Cloud saves: switch to desktop once, open the **CloudRedirect** app,
   and sign into your provider.

LumaDeck installs SLSsteam and lumalinux for you; you run nothing below.

### Manual (desktop, no plugin)

For driving lumalinux directly with Hubcap-style zips.

1. **Install the whole stack** with the self-contained installer, **from desktop
   mode** (its Steam restarts can trip gamemode's crash-loop detector):

   ```bash
   curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/setup.sh | bash
   ```

   One idempotent script: fetches SLSsteam + `library-inject` + CloudRedirect +
   netsock + `liblumalinux.so`, writes the injection **wrapper** at
   `~/.local/share/SLSsteam/path/steam`, and wires coverage (patched `*steam*.desktop`
   for Desktop, a PATH drop-in for terminals, and a systemd drop-in on
   `steam-launcher.service` for Game Mode). Your `steam.sh` and `/usr/bin/steam` stay **vanilla**.
   Launch Steam once so it settles.

2. **Add a game** for each Hubcap zip, with Steam closed:

   ```bash
   python3 tools/steamidra_lite.py <appid>.zip
   ```

   Restart Steam and press **Install**. Full step and flag reference in
   [`docs/manual-install.md`](docs/manual-install.md).

### After a Steam update

The wrapper is reached by patched `.desktop` files (Desktop), a PATH drop-in
(terminals), and a systemd drop-in on `steam-launcher.service` (Game Mode); a systemd
`--user` guardian re-affirms the `.desktop` coverage — so a Steam self-update no longer
erases injection the way a patched `steam.sh` did (and if a bad update breaks the
byte patterns, the wrapper's crash-loop fail-safe boots vanilla instead of bricking).
If injection ever stops, or to pull fresh `.so`s, reapply: in LumaDeck, **Settings →
Dependencies → Install / Reapply lumalinux**; manually, re-run the `setup.sh`
one-liner above (idempotent).

### Tested platforms

SteamOS gamemode via LumaDeck (the tested path); SteamOS desktop manual (works);
other Arch-based distros (Bazzite, CachyOS, etc.) should work but are untested.

## What it does

Inside `steamclient.so`, lumalinux supplies what Steam's native install needs for a
game you configured locally:

- **depot keys** from a local `keys.txt` (DepotKey hook)
- **manifest pinning** to the right version per depot (SLSsteam `config.yaml`
  `ManifestIds`; lumalinux's BuildDep hook is disabled by default)
- the **manifest request code** fetch via a provider cascade — `opensteamtool`
  (primary) → `wudrm` → `steamrun` (GMRC hook)
- a per-game **shader-cache skip** so keyless games don't loop on `Missing
  decryption key` (ShaderDepot hook)
- an **active package-0 finder** (a worker thread) that seeds depot ids into
  Steam's per-depot licence filter so content depots aren't dropped

Per-hook signatures and the finder internals are in
[`docs/RESEARCH.md`](docs/RESEARCH.md) (§4, §13).

### Alongside SLSsteam

SLSsteam owns the licensing layer (fakes ownership, PICS tokens,
family-share/offline); lumalinux owns the install-path plumbing above, and the two
stay orthogonal there. On top of that, lumalinux applies one surgical, reversible,
fail-closed in-memory patch to `SLSsteam.so` so LumaDeck games behave like owned
ones (opt-out via env var):

| Patch | What it does | Off with |
|---|---|---|
| **native achievements** (`sls_achievement_unblock`) | Scopes SLSsteam's native-achievement schema borrow to `isSubscribed && !isAddedAppId`, so LumaDeck games earn native Steam achievements while genuinely-owned games stay untouched ([RESEARCH §17](docs/RESEARCH.md)). | `LUMA_NO_SLS_ACH_UNBLOCK=1` |

> The old **update-unblock** (`sls_update_unblock`) patch was **removed in v0.16.18**:
> SLSsteam reverted its update-block mechanism on `20260714131044`, so the
> instruction it anchored on no longer exists. The countermeasure now lives in
> LumaDeck, which writes `DisableUpdates: no` into SLSsteam's `config.yaml`. See
> [slssteam-analysis §7.6](docs/slssteam-analysis.md).

## Usage

With lumalinux loaded, configure a game from a Hubcap-style zip (`.lua` +
`.manifest` files you legitimately have), with Steam **closed**:

```bash
python3 tools/steamidra_lite.py <appid>.zip
```

Restart Steam; the game appears ready to **Install** and downloads natively. That
one command does the full deploy (depotcache manifests, `keys.txt`, the SLSsteam
`AdditionalApps` entry, `config.vdf` keys, a clean `.acf`, the `stplug-in` lua, and
the ACCELA markers). The step-by-step and every flag are in
[`docs/manual-install.md`](docs/manual-install.md).

If you use **LumaDeck**, the plugin calls this for you when you tap "Download
Manifest"; you don't run it by hand.

## Troubleshooting

Log: `~/.cache/lumalinux/lumalinux.log`. The startup toast shows `X/Y hooks active`.

### Env vars (set before launching Steam)

- `LUMA_NO_NOTIFY`: silence the startup toast.
- `LUMA_LOG_LEVEL`: `error` / `warn` / `info` / `debug` (or `0`..`3`); default
  `info`. Set `debug` for the per-call hook traces.
- `LUMA_NO_DEPOTKEY` / `LUMA_NO_BUILDDEP` / `LUMA_NO_GMRC`: disable one install-path
  hook. (`LUMA_NO_BUILDDEP` is a no-op unless `LUMA_FORCE_BUILDDEP` is also set,
  since BuildDep isn't installed by default.)
- `LUMA_NO_SHADERSKIP`: disable the per-game shader-skip (ShaderDepot); installs are
  unaffected.
- `LUMA_NO_PKG0_FINDER=1`: disable the package-0 finder (the sole depot injector, on
  by default). `LUMA_PKG0_FINDER=diag` runs it log-only.
- `LUMA_NO_SLS_ACH_UNBLOCK=1`: disable the SLSsteam native-achievement patch.
  `LUMA_SLS_ACH_TRACE=1` traces the achievement guard. (The former
  `LUMA_NO_SLS_UNBLOCK` was removed with the update-unblock patch in v0.16.18.)
- `LUMA_LOADPKG_DEBUG=1`: install the diagnostic LoadPackage hook (off by default;
  logs `PackageId + AppIdVec`). `LUMA_LOADPKG_IDX=N` picks a candidate.

### Something's wrong

Almost always a Steam client or SLSsteam update. The log tells the cases apart, and
[`docs/maintenance.md`](docs/maintenance.md) has the fix per case:

- **No startup toast at all**: the wrapper isn't being reached (coverage lost — e.g.
  a Steam update regenerated a `.desktop`, or the Game Mode `steam-launcher.service`
  drop-in was dropped). Reapply lumalinux (see [After a Steam update](#after-a-steam-update)).
- **`X/Y hooks … FAILED`**: a byte pattern moved after a Steam update (maintenance
  §A). DepotKey + GMRC (plus the package-0 finder) failing breaks installs;
  BuildDep is disabled by default and non-critical, and ShaderDepot is cosmetic.
- **Install hangs at "0 target depots"**: the package-0 finder couldn't locate its
  anchors (maintenance §C).
- **Native achievements off / `SLS-ach: could not resolve`**: SLSsteam changed;
  fail-safe, not a crash (maintenance §D).

## Building from source

Most users don't need this: the installer pulls the prebuilt `.so` from
[releases](../../releases). To build the 32-bit `.so` yourself:

```bash
# 32-bit toolchain (Debian/Ubuntu names; on Arch: multilib gcc + cmake + ninja)
sudo apt-get install -y gcc-multilib g++-multilib cmake ninja-build

./tools/fetch_libmem.sh    # pinned libmem static lib + headers
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLUMA_NO_UPDATE=OFF
ninja -C build             # -> build/liblumalinux.so (32-bit ELF i386)
```

`fetch_libmem.sh` downloads a pinned `rdbo/libmem` release (version + SHA256 pinned
in the script); the fetched files are gitignored, not vendored. CI
(`.github/workflows/build.yml`) runs exactly these steps on every push to `main`
and publishes a release on each `v*` tag.

## How it works

lumalinux is 32-bit and hooks 32-bit `steamclient.so`. It loads via **`LD_PRELOAD`**,
not `LD_AUDIT` (which lands it in a separate linker namespace and corrupts the heap).
The injection point is the **wrapper** at `~/.local/share/SLSsteam/path/steam`
(`setup.sh`'s model, from `slsteam-moon`): it exports `LD_AUDIT` for SLSsteam and
`LD_PRELOAD` for CloudRedirect + lumalinux, then `exec`s the real Steam. The wrapper
is reached by patched `*steam*.desktop` files (Desktop), a PATH drop-in (terminals),
and a systemd drop-in on `steam-launcher.service` (Game Mode) — never by editing
`steam.sh` or `/usr/bin/steam` — so those stay vanilla and coverage survives Steam
self-updates (a guardian re-affirms the `.desktop` coverage). Exact anchor and namespace detail in
[RESEARCH §5](docs/RESEARCH.md).

### Related docs

- [`docs/method.md`](docs/method.md): how an unowned-game install works end to end
  (the six gates, the phase-by-phase native flow, the ecosystem comparison)
- [`docs/manual-install.md`](docs/manual-install.md): driving `steamidra_lite` by
  hand, every step, flag, and `keys.txt` format
- [`docs/RESEARCH.md`](docs/RESEARCH.md): every hook's signature, the RE workflow,
  the package-0 finder, and the SLSsteam native-achievement patch (§17; the
  removed update-unblock is §16)
- [`docs/cloudredirect.md`](docs/cloudredirect.md): running beside CloudRedirect,
  and the `LD_PRELOAD` ordering
- [`docs/maintenance.md`](docs/maintenance.md): fixing things after a Steam client
  or SteamOS update

## Credits / notes

- Hook design references **LumaCore** (Windows), reimplemented for Linux.
- Runs alongside **SLSsteam** (ownership / licensing layer); **never forks it**.
  Beyond coexisting, it applies one surgical, reversible in-memory patch to
  `SLSsteam.so` (native achievements §17): no source fork, fail-closed and opt-out.
  (Update-unblocking used to be a second such patch, §16, but SLSsteam changed that
  mechanism to a config-gated hook — LumaDeck now handles it via `DisableUpdates:
  no` in `config.yaml`, so the patch was removed in v0.16.) The SafeMode
  hash-whitelist update
  flow (`src/update.cpp`, `src/curl.cpp`, `src/sha256.cpp`, `src/globals.cpp`,
  `res/version.txt`, `res/updates.yaml`) was ported from SLSsteam (AGPL-3.0) with
  minimal adaptations; see the AGPL-3.0 header in each of those files. The full
  AGPL-3.0 text is in [`LICENSE-AGPL`](LICENSE-AGPL); the rest of lumalinux remains
  under GPL-3.0 ([`LICENSE`](LICENSE)). Per AGPL-3.0 §13 the network-interaction
  clause applies to the combined binary; in practice inert for lumalinux because it
  is not a network service.
- Coexists with **CloudRedirect** (cloud-save RPC layer); see
  [`docs/cloudredirect.md`](docs/cloudredirect.md) for the `LD_PRELOAD` ordering.
- Manifest request codes come from a provider cascade: `manifest.opensteamtool.com`
  (primary), with `gmrc.wudrm.com` and `manifest.steam.run` as fallbacks
  (RESEARCH §7).
- Research / educational. Use with your own Steam account and content. Do not
  redistribute Valve binaries.
