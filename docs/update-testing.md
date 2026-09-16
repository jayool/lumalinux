# Testing

> **2026-09-09:** Part 1 below exercises the *follow-Valve* auto-update, which
> needs a manifest request code from a provider. The providers are dead, so that
> procedure now ends in "No internet connection" and must not be used as a
> health check. Updates are delivered by moving the pin (method.md §6, status
> box); the codespace tests that validated that are logged in RESEARCH §19.

Two different questions, and it is worth being blunt about which is which,
because they get confused:

- **Part 1 — does the game lifecycle work?** Install a pinned old version, then
  let Steam auto-update it. This is about *games*, and it is what most of this
  file is.
- **Part 2 — does a change to lumalinux itself work?** What CI proves, what it
  does not, and the on-device procedure that is the only thing that actually
  runs the `.so`. Jump to
  [Validating a change to lumalinux](#part-2--validating-a-change-to-lumalinux).

---

# Part 1 — Testing installs and updates (pin → unpin → auto-update)

A practical, reproducible procedure for exercising the two halves of the game
lifecycle on Linux/lumalinux:

1. **Install a specific (old) version** by pinning a manifest.
2. **Auto-update to Valve's current version** by unpinning it — the native
   Steam client does the update itself, like a legit owner.

Both were validated end-to-end (Balatro, Vampire Survivors) on the canonical
stack. For the *why* behind every step see [`method.md`](method.md) §6 and
[`RESEARCH.md`](RESEARCH.md) §14.

## Prerequisites

- Native Steam + SLSsteam + lumalinux installed via the **wrapper model**
  (`setup.sh`, or LumaDeck's Quick Install), so the stack loads through the wrapper
  at `~/.local/share/SLSsteam/path/steam` and `steam.sh` stays vanilla. Confirm with
  a toast `lumalinux … N/N hooks active` and, in `~/.cache/lumalinux/lumalinux.log`,
  `3/3 hooks active` plus a clean `grep outcome=` — one `outcome=installed` per
  enabled hook **and** `Finder resolve: name=PKG0Finder … outcome=resolved`,
  followed by `PKG0_FINDER: HIT`. If the finder line says `outcome=miss`,
  nothing gets injected and every step below will fail for a reason that has
  nothing to do with the game (`maintenance.md` §C).
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

---

# Part 2 — Validating a change to lumalinux

This is the other question: not "does this game install?" but "did my change to
the `.so` break anything?". There are four layers, and **they prove
progressively more while covering progressively less**. Know which one you are
leaning on.

## The four layers

| layer | what runs | what it proves | what it does NOT prove |
|---|---|---|---|
| `build.yml` — compile | cmake + ninja, 32-bit | it builds | nothing about behaviour |
| `build.yml` — unit tests | every `tools/test_*.py` | the tooling's invariants hold on synthetic inputs | nothing about a real `steamclient.so` |
| `check_patterns.py` | pattern/RVA resolution on a real `steamclient.so` | every address is *findable* on that build | that the detour installs, or that anything runs |
| **on-device** | the real `.so` in a real Steam | it loads, hooks, and injects | only the success path — see below |

Nothing between the third row and the fourth. There is no CI job that loads the
library into a Steam process: `verify-fix.yml` ("Runtime smoke test") is
supposed to be that job, its header claims it is, and **it has never produced a
green run** — its first real executions (2026-09-08) all failed inside the
harness, before reaching any assertion. Do not treat it as a gate, and do not
let its existence talk you out of the on-device check.

## Layer 2 — the unit tests (since 2026-09-08)

Until that date **nothing invoked `tools/test_*.py`**. The tests existed, and
could rot silently. `build.yml` now runs all of them on every push; they are
cheap (synthetic inputs, no `steamclient.so`, no network, seconds).

Two things follow:

- **The glob is the registration.** A new test named `tools/test_*.py` is in CI
  automatically. Named anything else, it is not.
- **They pin cross-file invariants**, which is the part a compile cannot reach.
  The important one is `tools/test_cache_idiom.py`: the cache-access idiom scan
  is implemented four times (runtime, `check_patterns.py`, `derive_patterns.py`,
  `experiment_cache_idiom.py`) so that CI and Ghidra check exactly what the
  Deck does, and that test fails if any of the four drifts from the others.

Run them locally before pushing:

```bash
for t in tools/test_*.py; do echo "== $t"; python3 "$t"; done
```

**Make sure a new test bites.** A test that passes when you break what it
guards is worse than no test, because it buys false confidence. Mutate the
thing under test — change a predicate, flip a comparison — and confirm the test
goes red before you trust it. This repo has already paid for skipping that
step: `verify-fix.yml` read as "green" for months because the run being cited
belonged to a different workflow.

## Layer 3 — resolution on a real binary

Fast, offline, and the right first move for "did a Steam update break us?":

```bash
python3 tools/fetch_steamclient.py --output /tmp/steamclient.so
python3 tools/check_patterns.py /tmp/steamclient.so
```

CLEAN means every critical hook resolves UNIQUE and both package-0 finder
anchors classify `UNIQUE`. It resolves *on paper* — it proves the addresses are
findable, not that a detour can be written at them.

## Layer 4 — on-device, and why it is not optional

The only thing that runs the library. Use it before any release, and after
touching hooking code, a resolver, or the finder.

1. On a SteamOS box or codespace, install the stack normally from LumaDeck
   (`setup.sh`), so every other piece is the shipped one and you are testing
   one variable.
2. Build the branch and swap **only** the library:

   ```bash
   ./tools/fetch_libmem.sh                      # 32-bit libmem, once per clone
   mkdir -p build && cd build
   cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja && cd ..
   file build/liblumalinux.so   # ELF 32-bit LSB shared object, Intel 80386
   cp build/liblumalinux.so ~/.local/share/lumalinux/liblumalinux.so
   ```

   Check the `file` line. A host-native 64-bit build copies over cleanly and
   then silently never loads (`ELF file class ELFCLASS32 incorrect`, no
   lumalinux banner), which looks exactly like a wrapper problem and is not.

3. **Boot Steam logged in.** A logged-out client never builds a licence set, so
   package 0 does not exist, the finder has nothing to find, and a green-looking
   run proves nothing about injection.
4. Read the log:

   ```bash
   grep -E "hooks active|outcome=|PKG0_FINDER|Finder resolve|AMBIGUOUS" \
        ~/.cache/lumalinux/lumalinux.log
   ```

   Healthy: `3/3 hooks active`, one `outcome=installed` per hook,
   `Finder resolve: … outcome=resolved`, and a
   `PKG0_FINDER: HIT pkg=… PackageId=0 AppIdVec{size=N}` with a non-empty
   appid list. The `PKG0_FINDER: GOT UNIQUE` / `cache-access idiom UNIQUE`
   lines are emitted by the **scans**, so their absence is not a failure — it
   means the RVA feed answered instead. `got_method=` / `disp_method=` on the
   resolve line say which.

5. If the change touches installs, keep going into Part 1 — a library that
   loads and hooks can still get the depot logic wrong.

## What none of the four layers cover

**Every failure path.** All of the above exercises the success path. The
branches that only run the day Steam breaks something — an `AMBIGUOUS` verdict,
a scan that resolves nothing and ends the finder thread, an `outcome=miss`,
reading a value from the RVA feed instead of scanning — have never executed
outside the synthetic tests. That is the argument for layer 2 carrying its
weight: it is the *only* thing covering the code that runs on the worst day.

# Part 3 — Validating the GMRC path (native install without local manifests)

Run on 2026-09-15 in the SteamOS codespace, branch
`claude/lumalinux-lumadeck-context-jxgz2c` (`af93594`). The GMRC hook is
opt-in: launch Steam with `LUMA_GMRC=1`. In the codespace the variable goes on
the Game Mode script (`LUMA_GMRC=1 ~/start-gamemode.sh`), and the old
supervisor must be killed first (`pkill -f gamemode-supervisor`), otherwise it
relaunches Steam with the old environment. Check with
`tr '\0' '\n' < /proc/$(pgrep -o -x steam)/environ | grep ^LUMA`.

Test game: Balatro (app 2379780, depot 2379781). "No local manifest" means:

```bash
rm -f ~/.local/share/Steam/steamapps/appmanifest_2379780.acf; rm -rf ~/.local/share/Steam/steamapps/common/Balatro
rm -f ~/.local/share/Steam/depotcache/2379781_*.manifest; rm -rf ~/.local/share/lumadeck/manifests/2379780
```

| Test | How | Result |
|---|---|---|
| T1 native install | no local manifest, `LUMA_GMRC=1`, install from Steam | **pass** — `20770407 -> unavailable (HTTP 502)` ×3 with 5 s waits, then `got code … (via manifestdex)`, `CDN accepted … (HTTP 206)`, `INJECTED`; content_log `manifest request received 200`, 75 chunks, `finished update`, `StateFlags 4`; Steam wrote `2379781_<gid>.manifest` into depotcache itself |
| T2 keyed shader depot | let the shader pre-cache run when a provider is up. Uninstall + install Lethal Company (1966720) on 2026-09-16 — "verify integrity" does NOT start the shader job, an install does | **pass** — content depot 1966721 injected, then the shader depot 1966720 asked for two manifests (one per shader variant), both `via 20770407`, `CDN accepted (HTTP 206)`, `INJECTED`; content_log `Shader update changed : Running Update,Downloading,Staging` … `starting commit … 1 updated` … `None`; `steamapps/shadercache/1966720/` holds `fozpipelinesv6` and `transcoded_video.foz`; no popup. The precompiled shaders every managed game lost on 09-09 are back. (2875150 the day before showed the hook letting the job through but no manifest request: that app has no shader manifest to fetch.) |
| T3 dead provider | 20770407 was down all day | **pass (half)** — three paced attempts, 12 s, then the next provider. The full "all providers down → pins" path is what v0.20.1 runs in production |
| T4 bogus code | `LUMA_GMRC_URL="http://127.0.0.1:8765/manifest/%llu/%llu"` to a local server answering `123456789` | **without the CDN check**: injected, CDN 401 on every host, `update canceled … (Unspecified Error)`, `Update Paused`, removed from schedule, "Unknown error" in the UI, no retry — the wudrm failure of 09-10. **With it** (`af93594`): `CDN REJECTED code 123456789 (HTTP 401) — not handing it to Steam`, Steam goes its own path, `Failed to get manifest request code, 'Access Denied'`, "No internet connection", `Update delayed for 30 secs`, retries by itself — the benign case B |

Provider facts measured the same day (see gmrc_store.hpp header):
20770407.xyz needs depot + gid, answers `Unauthorized` for a gid it cannot
serve, 10 requests / 10 s per IP (Cloudflare 429, body `error code: 1015`),
backend `Service temporarily unavailable - please retry` under load, and was a
Cloudflare 502 from ~13:40 UTC for the rest of the day. manifestdex needs
`User-Agent: ManifestDeX/1.0`, gid only, and answers a number for ANY gid
(a made-up one included), which is why the CDN check exists and why it is
second in the table.

Provider measurements (coverage, old builds, the two pools, code lifetime,
rate limits) live in `docs/RESEARCH.md` §20.

Cost noted for later: with the first provider down, every depot pays the three
attempts (~12 s) before the next provider answers. A big game pays it per
depot. Remembering a failed provider for a minute would remove that.
