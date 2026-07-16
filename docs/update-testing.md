# Testing installs and updates (pin → unpin → auto-update)

A practical, reproducible procedure for exercising the two halves of the game
lifecycle on Linux/lumalinux:

1. **Install a specific (old) version** by pinning a manifest.
2. **Auto-update to Valve's current version** by unpinning it — the native
   Steam client does the update itself, like a legit owner.

Both were validated end-to-end (Balatro, Vampire Survivors) on the canonical
stack. For the *why* behind every step see [`method.md`](method.md) §6 and
[`RESEARCH.md`](RESEARCH.md) §14.

## Prerequisites

- Native Steam + SLSsteam (via enter-the-wired/Headcrab) + lumalinux installed
  via `install.sh` (so `steam.sh` carries the `LD_PRELOAD` patch). Confirm with a
  toast `lumalinux … N/N hooks active` and `~/.cache/lumalinux/lumalinux.log`
  showing `3/3 hooks active` + `PKG0_FINDER: HIT`.
- A Hubcap `.zip` for the game (the **current** version) and **one old
  `.manifest`** per content depot you want to start from. The old manifests come
  from SteamDB / a previous Hubcap; their filename must be
  `{depot}_{gid}.manifest`.

Throughout: appid `APP`, content depot `DEPOT`, new gid `GID_NEW` (in the zip),
old gid `GID_OLD` (the manifest you downloaded).

## Step 1 — build an "old" zip

The zip's `.lua` only needs the depot's `setManifestid` GID changed and the
depot's `.manifest` swapped for the old one. The **depot key is unchanged** (keys
are per-depot, valid for every version). Nothing else moves.

```python
# python3 - <<'PY'   (run from the folder with the zip + old/ manifests)
import zipfile
APP, DEPOT = "APP", "DEPOT"
NEW_M = f"{DEPOT}_GID_NEW.manifest"
OLD_M = f"{DEPOT}_GID_OLD.manifest"
GID_NEW, GID_OLD = "GID_NEW", "GID_OLD"

zin = zipfile.ZipFile(f"{APP}.zip")
old_data = open("old/"+OLD_M, "rb").read()
with zipfile.ZipFile(f"{APP}_old.zip", "w", zipfile.ZIP_DEFLATED) as zout:
    for it in zin.infolist():
        if it.filename == NEW_M:
            continue                       # drop the new manifest for this depot
        data = zin.read(it.filename)
        if it.filename.endswith(".lua"):
            txt = data.decode("utf-8")
            assert txt.count(GID_NEW) == 1
            txt = txt.replace(GID_NEW, GID_OLD)   # repin the .lua to the old gid
            data = txt.encode("utf-8")
        zout.writestr(it.filename, data)
    zout.writestr(OLD_M, old_data)         # add the old manifest
# PY
```

(The zip filename is irrelevant — `steamidra_lite` reads the appid from the
`.lua`, not the filename. Repeat the swap per depot if pinning more than one.)

## Step 2 — deploy + install the old version, pinned (Fase A)

> **NOTE — BuildDep is disabled by default.** SLSsteam now owns version pinning
> via its `config.yaml` `ManifestIds`, so lumalinux's BuildDep hook is **not in
> the default hook set** and the zip `--pin` flag only writes `keys.txt` (whose
> sole consumer was BuildDep). This pin-an-old-version test path therefore
> **only works if you launch Steam with `LUMA_FORCE_BUILDDEP=1`**; without it
> the `--pin` write lands in `keys.txt` but nothing actually pins.

Steam must be **closed** first (`steamidra_lite` writes `config.vdf`, which Steam
rewrites on exit). We use **`--pin`** so the game installs at the old version and
**stays** there (the default is no-pin / auto-update).

```bash
steam -shutdown 2>/dev/null || ~/.local/share/Steam/steam.sh -shutdown; sleep 8
python3 tools/steamidra_lite.py ./APP_old.zip --pin
# verify: keys.txt has  DEPOT;APP;GID_OLD;<size>;<key>
grep "^DEPOT;" ~/.config/lumalinux/keys.txt
```

Start Steam (with `LUMA_FORCE_BUILDDEP=1`) and click **Install**. Watch
`LoadDepotKey: SERVED` in `lumalinux.log`; the `BuildDep: PATCH … -> GID_OLD`
line **only appears when `LUMA_FORCE_BUILDDEP=1`** (BuildDep is off in the
default set). When it finishes, the `.acf` proves the old version is installed:

```bash
grep -A3 InstalledDepots ~/.local/share/Steam/steamapps/appmanifest_APP.acf
# "DEPOT" { "manifest" "GID_OLD" }   + StateFlags "4"
```

> A first-attempt `Missing decryption key` on the shader-cache depot can be
> transient — Steam retries (~300 s) and lands on `StateFlags=4` (RESEARCH
> §13.7). Don't kill it; let it retry.

## Step 3 — unpin → auto-update (Fase B)

The clean way is to **re-deploy without `--pin`** — the default mode does all
three unpin steps (gid→0, comment `setManifestid`, prune the old cached
manifests):

```bash
steam -shutdown 2>/dev/null || ~/.local/share/Steam/steam.sh -shutdown; sleep 8
python3 tools/steamidra_lite.py ./APP_old.zip      # default = no-pin
```

Or do it by hand (equivalent), which is useful when you don't want to re-run the
tool:

```bash
# 1) keys.txt: gid+size -> 0, keep the key
sed -i -E 's/^(DEPOT;APP;)[0-9]+;[0-9]+;/\10;0;/' ~/.config/lumalinux/keys.txt
# 2) comment the pin in the stplug-in lua (interop / Windows-method parity)
sed -i -E 's/^([[:space:]]*)setManifestid/\1--setManifestid/' \
    ~/.local/share/Steam/config/stplug-in/APP.lua
# 3) REQUIRED: nuke this depot's cached manifests so Steam refetches the current one
rm -f ~/.local/share/Steam/depotcache/DEPOT_* \
      ~/.local/share/Steam/config/depotcache/DEPOT_*
```

Restart Steam. With `gid=0` there is no pin, so by default there are **no
BuildDep log lines at all** (the hook is disabled) — the auto-update follows
directly from `gid=0`: Steam plans Valve's current manifest, fetches it (GMRC
supplies its request code at runtime), downloads the delta, and updates — **on
its own**. Verify:

```bash
grep -iE 'APP.*finished update|DEPOT \(' ~/.local/share/Steam/logs/content_log.txt | tail
grep -A3 InstalledDepots ~/.local/share/Steam/steamapps/appmanifest_APP.acf
# "DEPOT" { "manifest" "GID_NEW" }   ← flipped to the current version
```

The delta can be tiny and fast (seconds) if old→new share most chunks — watch
`content_log.txt`, not just the UI.

## Reset between runs

```bash
# uninstall via the Steam UI, or:
rm -f  ~/.local/share/Steam/steamapps/appmanifest_APP.acf
rm -rf ~/.local/share/Steam/steamapps/common/<installdir>
```

`steamidra_lite` leaves `.bak` files (`keys.txt.bak`, `config.yaml.bak`,
`*.acf.bak`, `config.vdf.bak`) to revert individual steps.
