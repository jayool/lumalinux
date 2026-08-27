# SLSDeckUniversal vs the LumaDeck stack — exhaustive analysis

*Complete, all five phases: inventory and provenance (§1–§2), the layer-by-layer
comparison (§3), non-functional axes (§4), trust and risk (§5, including the
Tokeer activation path in §5.7), the weighted score (§6) and the per-profile
verdicts (§7). **§10 records the adversarial pass** over the conclusions that
favour our own stack: it revised two of them and reversed the §6 aggregate. §11
adds field reports — the only observed-outcome evidence in the document.
**Read §12 first**: nothing was executed, and it separates what was measured from
what was judged. Nothing here is a recommendation to change LumaDeck or lumalinux
— §8 is a findings list and remains deliberately unimplemented.*

---

## §0 Scope, method, evidence rules

### What is being compared

| Side | Components |
|---|---|
| **Ours** | LumaDeck (Decky plugin) + lumalinux (`.so`) + SLSsteam (stock, unforked) + CloudRedirect |
| **Theirs** | SLSDeckUniversal (Decky plugin) + slsteam-moon (SLSsteam fork) + CloudRedirect / OpenSave |

### Frozen references

Every claim below is against these exact revisions. Re-verify before reusing
this document; the counterpart repository moves fast (§4.3).

| Project | Ref | Version | Date | Size |
|---|---|---|---|---|
| LumaDeck | `eea30e5` | 0.7.2 | 2026-08-26 | 13.393 L Python, 5.911 L TS |
| lumalinux | `87ee544` | 0.18.0 (SafeMode `20260611150000`) | 2026-08-26 | 5.590 L C++ |
| SLSDeckUniversal | `c243cf3` | 0.9.61 | 2026-08-26 | 32.157 L Python, 18.242 L TS |
| slsteam-moon | `d3402a1` | v2.8 | 2026-08-23 | 226 commits |

Unlike the previous investigation (`slsdeck-findings.md`, 2026-07-22), which
read a handful of files over `raw.githubusercontent`, this one works from **full
clones with history** of both counterpart repositories.

### Evidence rules

Every factual claim carries one of three tags:

- **[read]** — verified by reading the code at the cited `file:line`.
- **[inferred]** — a conclusion drawn from read evidence, with the inference stated.
- **[their claim]** — asserted by their README, CHANGELOG or code comments, and
  *not* independently verified. Their documentation has already been shown to
  drift from their code (§4.2), so this tag is load-bearing.

### Method note on bias

This document is written by the author of the competing stack. Two
counter-measures are applied deliberately, and the reader should hold the
document to them:

1. **Axes and weights were fixed before reading their code** (§6, pending).
2. **Every axis where they win, or where the two are equal, is stated
   explicitly** — including three where a previous version of this analysis
   wrongly claimed a LumaDeck advantage (§2.4). A comparison that finds no
   losses is not a comparison.

---

## §1 What SLSDeckUniversal is today

### §1.1 The previously analysed repository no longer exists

`slsdeck-findings.md` analysed `Kaal31/slsdeck` v0.01: ~6 commits, a "stripped"
build, explicitly *no hypervisor / no Denuvo*, described as a thin frontend for
slsteam-moon.

That repository is gone. **[read]** The current history begins on **2026-08-20**
with commit `6da03a1`, titled *"SLSDeckUniversal (slsdeckdlc) v0.9.59"* — a
squashed code drop. There is no v0.01 in the history, no migration, no
continuity. The plugin was renamed (`plugin.json` → `SLSDeckUniversal`), the
`root` flag was added, and the hypervisor/Denuvo build that the old document
recorded as *announced but unpublished* **is now the published build**.

Every conclusion in `slsdeck-findings.md` should be treated as void, not stale.

### §1.2 Shape of the history

**[read]** 369 commits in 7 days:

| Date | Commits |
|---|---|
| 2026-08-20 | 56 |
| 2026-08-21 | 92 |
| 2026-08-22 | 125 |
| 2026-08-23 | 22 |
| 2026-08-24 | 48 |
| 2026-08-25 | 17 |
| 2026-08-26 | 9 |

**[read]** Authorship: `Vibe-coder Jimmy <mashroomgodzillo@gmail.com>` 299
commits, `github-actions[bot]` 65, `Worker1 <apkspy@gmail.com>` 5.

**[read]** The `Latest` release tag points at `6da03a1` — the *first* commit,
not `HEAD`. The published release is the initial drop, 369 commits behind the
source.

**[read]** Three modules are empty stubs carrying the same textual excuse:

> `py_modules/lt/charon.py`, `impersonate.py`, `diagnostics.py`:
> *"Kept as an empty stub only because **the mount blocks deletion**"* /
> *"the file could not be deleted in place"*.

**[inferred]** That is an automated agent operating in a sandbox without delete
permission on its working tree — the phrasing is self-descriptive and appears
verbatim in three unrelated modules. Combined with the declared author handle
and the 125-commit day, the code is machine-generated at high velocity. This is
an observation about *process*, not a value judgement; its consequences are
measured in §4.

**[read]** `diagnostics.py` also states: *"This build is **private and
single-user**, so it's dead weight."* The build is published as a public GitHub
release.

### §1.3 Their CHANGELOG describes its own origin

**[their claim]** `CHANGELOG.md:1-8` describes the project as a merge of the
creator's `sls_deck_source` (v0.01, the previously analysed build) and
`slsdeckAIO` (v0.0.2), plus a correctness pass, verified by *"280 assertions
across 10 suites"*.

**[read]** There is not one test file in the repository. See §4.1.

---

## §2 Inventory

### §2.1 Scale

| | SLSDeckUniversal | LumaDeck | lumalinux |
|---|---|---|---|
| Backend Python | 29.974 + 2.183 (`main.py`) | 12.677 + 716 | — |
| Frontend TS/TSX | 18.242 | 5.911 | — |
| C++ | — | — | 5.590 |
| Backend modules | 59 (+ `lt/hv/`, 7 files) | 24 | — |
| RPC endpoints | 366 | 106 | — |

Roughly 2,6× our total line count and 3,5× our exposed API surface.

### §2.2 Provenance — breadth by aggregation

The single most important structural fact about SLSDeckUniversal is **where its
functionality comes from**. Their own module docstrings attribute it:

| Their module | Upstream | Tag |
|---|---|---|
| `smart_merge.py:1` | *"Faithful Python port of luatools-moon's `smart_merge.lua`"* | [their claim] |
| `depot_history.py:6` | *"Ported/adapted from **SteaMidra's** `depot_history.py`"* | [their claim] |
| `assella.py:1,10,51` | ASSella (niwia), *"vendor-trimmed"*; vendors the DepotDownloader DLL bundle | [read] |
| `confighealer.py:15` | ASSfixer (niwia) | [their claim] |
| `workshop.py:3` | *"Reuses WorkshopDL's mechanism — SteamCMD `+workshop_download_item`"* | [their claim] |
| `hypervisor.py:1-6` | HV-Decky, prebuilt kernel module from GitHub releases | [read] |
| `tokeer.py:3` | Tokeer upstream — **not vendored, fetched at runtime** | [read] |
| `creamysteamy.py:11` | CreamySteamyLinux; vendors `proxy.c` | [read] |
| `smokeapi.py`, `dlcunlockers.py` | acidicoala (SmokeAPI, CreamAPI, Uplay R1/R2) | [read] |
| `opensave.py:1` | Liquid-co/OpenSave, driven over its daemon HTTP API | [read] |
| `crakfiles.py`, `hvauto.py` | KoriaPolis | [read] |

Plus headcrab (`h3adcr-b`), netsock, CloudRedirect, Ryuu, perondepot, Hubcap,
Morrenus and Charon as data or binary sources.

**[inferred]** The two stacks pursue opposite strategies:

- **lumalinux is original engineering.** The depot-key, GMRC, ShaderDepot and
  package-0 hooks are our own reverse engineering of `steamclient.so`; nobody
  else ships them, and the byte patterns, the RVA feed and the maintenance
  procedure are ours to fix.
- **SLSDeckUniversal is integration.** The hard part of its stack — getting
  depot keys into Steam — is done by slsteam-moon, which is a third party's
  fork. Their code is the remote control.

Neither is wrong. The consequence is **where each stack breaks and who can fix
it**: ours breaks when Valve moves a byte pattern, and the repair is in our
hands and documented (`docs/maintenance.md`); theirs breaks when any of ~15
upstreams changes, is abandoned or disappears, and the repair is not.

### §2.3 Functional inventory

**Theirs, with no LumaDeck equivalent:**

`assella` (direct-download acquisition), `depotdl` (specific builds + unowned
DLC depots), `buildarchive`/`buildpicker`/`buildhistory`/`depot_history`
(build downgrade via SteamDB), `workshop` (SteamCMD), `art` (library artwork),
`storage` (microSD libraries), `audit`/`watchdog`/`confighealer` (self-heal),
`opensave` (second cloud-save engine), `multiplayer` (feasibility check),
`backup`/`survival_backup`, `depot_cleanup`, `dlcunlockers`/`smokeapi`/
`creamysteamy`/`dlcdepot` (DLC), `hypervisor`/`hvauto`/`proton` (Denuvo),
`tokeer`/`ubisoft_packages`, `crakfiles`, `custom_fixes`, `pinsource`,
`smart_merge`.

**Ours, with no SLSDeckUniversal equivalent:**

`achievements` (Web API schema generation), `goldberg` (full Steam emulator),
`self_update` (in-plugin plugin update), `desktop_handoff` + `quick_install_cli`
(armed KDE autostart for tasks Game Mode cannot perform), `platform_info`
(portability beyond SteamOS), `slssteam_schema` / `slssteam_version`,
`steam_freeze`, `components`, `subprocess_env`.

**Convergent (both, independently):** luatools account auth, netsock,
CloudRedirect, Denuvo detection, manifest-source templating, Ryuu, fixes,
Steamless/SteamStub, DLC config entries, headcrab client downgrade.

### §2.4 Three axes where a previous LumaDeck advantage no longer holds

Recorded prominently because `slsdeck-findings.md` claimed all three:

1. **No-restart add.** The old document recorded *"❌ requires Steam reload"*.
   **[read]** `live_refresh.py:1-7` now performs hot-add reconciliation by
   observing moon's own log, treating an add as ready only once the same
   `HotReload` generation reaches `PackagePatch`'s processed runtime
   package-change state *and* Steam accepts the live appinfo request, with
   defer/timeout falling back to the restart path.
   **[inferred]** Parity, with **different failure coverage** — an earlier draft
   of this document conceded more than the code supports, and the correction is
   recorded because §2.4 exists to catch exactly that kind of error in both
   directions. See §3.4.

2. **Native achievements.** **[read]** `slssteam.py:2991-2992` — moon exposes an
   `Achievements:` key in `config.yaml` and *"fetches the real achievement
   schema live by impersonating an owner"*; SLSDeckUniversal only toggles it.
   Ours requires `sls_achievement_unblock`, a reversible in-memory patch of
   `SLSsteam.so` maintained against a moving target (RESEARCH §17, maintenance
   §D). Same outcome; their fork gets it for free.
   **[inferred]** A direct, quantifiable cost of our "never fork SLSsteam"
   decision. The decision remains defensible on other grounds; this axis is not
   one of them.

3. **netsock.** Already recorded as closed on 2026-08-02. Still parity.

### §2.5 Where the line count is not product

**[read]** **Workshop is exposed twice over one engine.** `main.py:2107-2144`
defines eleven `ws_*` RPCs; `main.py:2147-2185` defines five `workshop_*` RPCs
that call the same `workshop.start_download`. `main.py:2172` concedes: *"kept for
call compatibility"*. The 366-endpoint figure is inflated by back-compat shims.

**[read]** **Two live, parallel cloud-save systems.** `cloudredirect.py` (475 L,
injects a `.so`) and `opensave.py` (749 L, drives a headless daemon). `opensave.py:3`
states it *"replaces CloudRedirect's inject-a-.so approach"*, yet both remain
exposed (`cr_*` and `os_*` RPCs, plus `sections/CloudRedirect.tsx` and
`sections/OpenSave.tsx`). **[read]** Neither module references the other.
**[inferred]** Nothing prevents a user enabling both against the same save files.

### §2.6 Where the line count *is* product — DLC

Recorded as a concession. **[read]** Their DLC support is three genuinely
distinct layers, not redundancy:

1. **Steam-client ownership** — moon's config entries. *(We have this:
   `slssteam_ops.add_game_dlcs`.)*
2. **In-process ownership** — `smokeapi.py:1`, `dlcunlockers.py:1`: a
   `steam_api` proxy that answers the game's own Steamworks DLC checks, because
   *"SLSsteam only unlocks DLC at the Steam-client level"*. `creamysteamy.py`
   goes further and compiles a **version-matched proxy per game**, reading that
   game's own exported symbols and overriding only the 9 DLC functions — using a
   45 MB `zig cc` toolchain fetched at runtime (`creamysteamy.py:9,43-44`)
   because SteamOS ships no compiler.
3. **Content** — `dlcdepot.py:1`: fetches the DLC depot bytes, because *"Steam
   won't fetch unowned-DLC depots, so we place the files directly"*.

**[inferred]** We implement layer 1 only. This is the clearest functional gap in
the comparison and it is not closable by aggregation alone — layer 2 is real
engineering.

---

## §3 Layer-by-layer comparison

### §3.1 Injection and boot coverage — **LumaDeck wins, on their own evidence**

The problem both stacks solve: export `LD_AUDIT`/`LD_PRELOAD` before Steam
launches, and survive Steam updating itself.

| | LumaDeck stack | SLSDeckUniversal |
|---|---|---|
| Injection point | Wrapper at `~/.local/share/SLSsteam/path/steam` | `steam.sh`, patched in place |
| Reached via | Patched `*steam*.desktop`, PATH drop-in, systemd drop-in on `steam-launcher.service` | gamescope hook + `path/steam` + `steam.sh` patch |
| `steam.sh` | **Left vanilla** | Patched — by them *and* by headcrab |
| After a Steam self-update | Guardian re-affirms `.desktop` coverage | **Patch is reverted; re-patched on detection** |
| Crash-loop protection | Fail-safe boots vanilla | Failsafe counter |

**[read]** The decisive evidence is in their own comments:

- `slssteam.py:1749` — *"Activating injection (patching steam.sh)…"*
- `slssteam.py:512` — *"**steam.sh got reverted; re-patch quietly** so the next
  launch stays hooked."*
- `slssteam.py:505-508` — `_reset_inject_failsafe()`, an anti-crash-loop counter
  cleared once injection is confirmed live.
- `slssteam.py:1737` — *"Apply OUR steam.sh wrapper LAST — on-device testing
  showed this is what actually injects in Game Mode (headcrab's launcher patch
  alone does not)"*, and `:1726-1727` — headcrab *"also patches steam.sh its own
  way — do it FIRST so our proven wrapper is applied last and wins."*

**[inferred]** Three overlapping mechanisms write to the same files, one of them
a third-party script they do not control, resolved by ordering. The re-patch
path exists because the patch demonstrably does not survive.

This is precisely the dead end recorded as **M7** in
`docs/slsteam-moon-findings.md` — Steam re-extracts `steam.sh` when its size
changes — and **slsteam-moon itself tried it and reverted** (`d829fb2` →
`dc89501`, M7). The engine they depend on abandoned this approach; the plugin
built on that engine has reimplemented it.

Our wrapper cannot be reverted by a Steam self-update because the file Steam
rewrites is not the file we depend on.

### §3.2 Ownership and licensing layer

| | Ours | Theirs |
|---|---|---|
| Ownership engine | **Stock SLSsteam, unmodified** (AceSLS upstream) | **slsteam-moon**, a fork |
| Who maintains it | AceSLS | swwayps |
| Our/their own code in it | One reversible in-memory patch | The whole layer |

**[read]** moon is 42.630 hand-written lines (excluding generated protobufs).
That figure is not comparable to lumalinux's 5.590 — moon *is* SLSsteam plus the
install core, where our equivalent is stock SLSsteam (a third party's binary we
do not count) plus lumalinux. The comparable subset is §3.3.

**[inferred]** The fork/no-fork decision is the root of most differences below.
Forking lets moon hook anywhere — including the protobuf message dispatch — and
own features outright (§2.4, native achievements). Not forking forces lumalinux
onto function-layer hook points that SLSsteam does not touch, which is why
DepotKey hooks the local KeyValues accessor rather than the depot-key network
message, and why gates 2, 4 and 6 below repeat the same "not portable, would
collide" conclusion.

The cost is symmetric and worth stating: moon also inherits the *maintenance* of
the entire ownership layer, where we inherit AceSLS's upstream for free.

### §3.3 The install path — gate by gate

Comparable subsets: moon's install core (`depotkey`, `pics`, `packagepatch`,
`manifestbind`, `manifestcode`, `manifestid`, `manifeststore`, `appinfo_*`,
`hotreload`, `prewarm`, `depotquarantine`, `reconcilepin`, `cmclient`) is
**15.846 L**; lumalinux is **5.590 L**. **[read]**

Gate-by-gate conclusions are carried forward from
[`slsteam-moon-findings.md`](slsteam-moon-findings.md), which analysed these
function by function and remains current to `d3402a1`.

| Gate | Ours | Theirs | Lead |
|---|---|---|---|
| 2 — PICS / appinfo | Depends on the ownership spoof yielding a full appinfo | **Anonymous appinfo provisioning** + `appinfo.vdf` splice (`appinfo_provision.cpp`, 4.269 L) | **Theirs** |
| 3 — depot surfacing (package-0) | **Active finder**, polls the cache BST | `LoadPackage` hook | **Ours** |
| 4 — manifest pinning | BuildDep, function layer, patch-only-primary | PICS response, message layer | Parity |
| 5 — depot keys | `LoadDepotDecryptionKey` ← `keys.txt`, **watched live** | Lua import **at startup only**; plugin-side cache workaround | **Ours** (engine design); parity delivered |
| 5b — bad-key detection | **None, by construction** | **Depot quarantine** (`depotquarantine.cpp`) | **Theirs** |
| 6 — GMRC | Function hook + 3-provider cascade | Three message-layer hooks + disk-staging fallback | Parity |

Three of these deserve expanding.

**Gate 2 — their clearest engine-layer advantage.** lumalinux cannot fabricate a
depot list: injecting depots into BuildDep SIGSEGVs Steam, so it can only *patch*
GIDs of depots Steam already surfaced. If the CM returns a stripped appinfo
despite the ownership spoof, we have no depot list and the install fails. moon
opens its **own anonymous CM session** (`cmclient.cpp`) — product info is public
anonymously — mines the depots and gids, and splices synthetic entries into
`appinfo.vdf` for the next start. **[inferred]** This is portable to us in
principle (an outbound CM connection plus an offline file write, neither a
message hook), and it is the one engine-layer capability that closes a case we
currently cannot survive.

**Gate 3 — our clearest engine-layer advantage, re-verified against `d3402a1`.**
**[read]** moon still captures package 0 via the `LoadPackage` hook
(`packagepatch.cpp:47,67-68`: the `PackageInfo*` is *"captured the first time
LoadPackage is called with PackageId == 0"*). Much of that module's 1.290 lines is
scaffolding around the hook's timing: a gated `LicensesUpdated_t` broadcast to
force a licence re-read (`:117-121,160`), an explicit cold-cache path
(`:141,167`), and a snapshot kept *"even while package 0 is unavailable so a later
LoadPackage(0) can reconcile the exact latest state"* (`:100-101`). lumalinux
dropped the hook and polls the cache BST until package 0 appears, catching it
whenever it arrives including on a slow login. Nothing to port.

**[inferred]** Fairness: ours is not free either — the finder is a worker thread
with its own anchor-resolution failure mode (*"Install hangs at 0 target
depots"*, `docs/maintenance.md` §C). The advantage is in timing robustness, not
in having no failure mode.

**Gate 5 — live key ingestion, an engine-design advantage found in the second
pass.** **[read]** lumalinux runs an inotify watcher on the `keys.txt` directory
(`key_store.cpp:172-213`, `IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE`), reloads
the store in-process and signals `LicenseReconcile::NotifyKeysChanged()`. A game
added while Steam is running has its keys immediately.

**[read]** moon does not: SLSDeckUniversal's own code documents the limitation at
`slssteam.py:822-834` — *"The moon imports Lua depot keys only once, at startup
(`DepotKey::onStartup -> importLuaScripts` is idempotent), so a game added while
Steam is already running never gets its keys — and the moon then hands Steam a
**zero key** for the AdditionalApps depot, which downloads but can't decrypt
(empty folder)."*

**[read]** Their workaround is competent: the plugin writes directly into moon's
private key cache (`<config>/cache/depotkey_<depotId>.yaml`, base64 of the raw 32
bytes, `managed:true`), *"mirrors `DepotKey::saveKeyToCache` exactly"*, relying on
`getCachedKey()`'s on-demand fallback. moon reads that directory back in
`packagepatch.cpp:190-205`.

**[inferred]** So the delivered outcome is parity while the engine design is ours,
and their fix carries a coupling ours does not: a plugin reaching into the
engine's private on-disk format. If moon changes `saveKeyToCache`, the workaround
breaks silently — the failure mode is a game that downloads and cannot decrypt,
which is precisely the symptom the workaround exists to prevent.

**Gate 5b — a real gap.** **[read]** `depotquarantine.cpp` hooks `OnChunkUnpacked`
(in two calling conventions) and detects when a key *they supplied* fails to
decrypt a chunk, identifying each failed chunk by its 20-byte SHA; the depot is
quarantined and `ManifestBind` stops offering it. Our key hook validates *shape*
only (`key_store.cpp:45`: `hex.size() != 64`) — a good key and a bad key are 32
indistinguishable bytes until a chunk decrypts. We cannot distinguish "wrong key"
from "no key" by construction.

### §3.4 Add-game pipeline

| | Ours | Theirs |
|---|---|---|
| Model | **Offline pre-seed** before Steam starts | Runtime read by the engine |
| What is written | manifests → `depotcache`, `keys.txt`, `config.vdf` keys, `.acf` stub, `AdditionalApps`, `stplug-in` lua, ACCELA markers | `AdditionalApps` + `<appid>.lua` into `stplug-in` |
| Who parses the `.lua` | `steamidra_lite.py`, offline | moon, at startup |
| No-restart add | `license_reconcile` (broadcast) | `live_refresh` (verified against moon's log) |

**[inferred]** Ours front-loads work into a deterministic offline step: by the
time Steam runs, every artefact is already on disk in the shape Steam expects.
Theirs defers to the engine, which is less to go wrong at add time and more to go
wrong at runtime. Both are coherent; the practical difference shows in §3.6.

**No-restart: both verify, at opposite ends of the operation.** **[read]**

- **Ours verifies capability before acting, as a cross-component interlock.**
  `license_reconcile.cpp:39-51` resolves `NotifyLicensesUpdated` RVA-feed-first,
  cross-checks it against the byte pattern and warns on drift, and requires a
  **unique** match — a wrong-build pattern caches 0 and the feature *no-ops
  rather than calling a garbage address* (`:126-131`). lumalinux then publishes
  that outcome as `status.json` hook `"Reconcile"`, and LumaDeck reads it:
  `slssteam_ops.py:38-60` (`_hot_reload_active`) **suppresses every config poke**
  when the engine reports `"failed"`, because poking a just-added game into a
  live view whose reconcile is dead would *"surface a game whose appinfo has no
  depot list -> it installs 0 files ('installed but broken' trap)"*. The unknown
  case is explicitly not suppressed — *"we never suppress on a guess"*.
- **Theirs verifies effect after acting.** `live_refresh.py` parses moon's log
  for the `HotReload` generation reaching `PackagePatch`'s processed state and
  the live appinfo request being accepted, deferring to the restart path on
  timeout.

**[inferred]** Neither dominates. Ours prevents the bad state and cannot enter
it; theirs detects a fire that resolved and dispatched but did not land — a case
ours would not notice. Ours is an interlock, theirs is an observation. The
earlier framing of this as "they verify, we assume" was wrong.

### §3.5 Surviving a Steam client update — **the decisive axis, and closer than expected**

This is axis A2 (15%), the highest-weighted single axis, and the honest answer is
that **moon is structurally ahead on the hard case while we are ahead on
detection**.

| | lumalinux | slsteam-moon |
|---|---|---|
| Feed content | **RVAs** per build (`res/rvas/<sha256>.yaml`) | **Full locator catalog** — signatures *and* addresses |
| Feed authenticity | Hardcoded HTTPS source, own repo, **no signature** | **Ed25519 detached signature**, public key baked in at build time |
| Validation before use | Hash-match ⟹ correct-by-construction (CI derived them from that exact binary) | **Runtime probe evidence** — `locator-resolved`, `hook-installed`, `hook-invoked` |
| Fallback | **Own baked `patterns.hpp`** — a stale feed never leaves a hook with nothing | Baked patterns |
| Detection of a new Steam build | **Daily CI cron** (`watch-steam.yml`) fetches the new `steamclient.so` and validates every hook | Their own `swwayps/steam-monitor` infrastructure |
| Escalation | **Automatic**: clean → auto-PR bumping `updates.yaml`; blocking → auto-issue | Manual |
| Recovery from a *moved prologue* | **Human re-derivation + release** | **Ship a new signature in the signed catalog** |

**[read]** Their mechanism is fully wired, not a dev aid: `src/patterns.cpp:101,160,189`
loads the catalog at runtime and reads `entry->signature`; `bin/pattern-refresh`
is a companion binary built alongside `SLSsteam.so`; the Makefile **fails the
build** if `res/pattern-public-key.hex` is absent. `tools/pattern-refresh/catalog.cpp:775-776`
fetches from `raw.githubusercontent.com/swwayps/steam-monitor/main/` with a
jsDelivr mirror. `res/runtime-probes.toml` gates activation on real-Steam
evidence and is explicitly data-only — *"they cannot execute code or supply
commands."* `d3402a1` made an ambiguous signature match **fail closed**.

**[read]** The structural asymmetry: our RVA derivation is
`scan_rvas(segments, patstr)` (`tools/check_patterns.py:226`) — it locates the
function **by scanning with the existing pattern**. When a prologue changes, the
scan finds nothing, no RVA can be emitted, the cron exits 3 and opens a blocking
issue for manual Ghidra re-derivation (`docs/maintenance.md` §A.2/A.3). Their
catalog carries signatures, so the same case is a data update.

**Where we lead, and it is narrower than it first looked.** **[read]**
`watch-steam.yml` runs daily, gates cheaply on a version check, then downloads the
new `steamclient.so`, runs the full hook validation, and opens either a hash-bump
PR (which propagates to every Deck on next boot with **no release**) or a blocking
issue. That automated escalation has no counterpart on their side.

> **Revised by §10.1.** This section originally added that moon has no `.github/`
> directory and that their detection cadence was *"unverifiable from here"*. That
> was an assumption, and it was wrong: their feed names its own source, and
> `swwayps/steam-monitor` publishes every 3–8 days, most recently the day before
> this freeze, carrying **six signed build catalogs across Stable and Beta**
> against our **one, unsigned, Stable-only**. See §10.1 for the full comparison
> and §10.2 for the score revision it forced.

**[read]** Recorded for fairness: `res/updates.yaml` is still fetched from
`AceSLS/SLSsteam` by moon — it consumes upstream's SafeMode feed while having
dropped its own hash whitelist (M2).

**[inferred]** Net: on the common case (bytes moved, prologue intact) both
recover server-side without a rebuild. On the hard case moon recovers as a data
update and we do not. Against that, our detection and escalation are automated
and theirs is not visible. This axis does **not** resolve cleanly in our favour,
and the previous document's claim that *"our patterns survived where moon had to
re-adapt"* was a single data point, not a structural advantage.

### §3.6 Failure modes and recovery

| Failure | Ours | Theirs |
|---|---|---|
| Bad Steam update bricks boot | Wrapper crash-loop fail-safe boots vanilla | Failsafe counter around the `steam.sh` patch (§3.1) |
| Injection coverage lost | systemd guardian re-affirms `.desktop` | Re-patch loop (§3.1) |
| Ambiguous pattern match | Byte-pattern fallback to baked patterns | **Fail closed** (`d3402a1`) |
| Key that does not decrypt | **Not detectable** (§3.3, gate 5b) | Depot quarantine |
| Unknown `steamclient.so` hash | SafeMode is **advisory**, not a gate | Catalog miss → baked patterns |
| Broken `config.yaml` | `slssteam_schema.py` append-only completion | `confighealer.py` (476 L), `audit.py`, `watchdog.py` |

**[inferred]** Both stacks are defensive, with different emphases: ours prevents
bad states at the engine layer and self-heals coverage; theirs detects bad states
at runtime and repairs config at the plugin layer. Their `audit`/`watchdog`/
`confighealer` trio has no counterpart in LumaDeck beyond `components.py`.

### §3.7 Plugin and UX layer

**[read]** The asymmetry here is the largest in the document: 366 RPCs vs 106,
18.242 L of frontend vs 5.911, 20 UI sections vs 6 pages.

Theirs and not ours: an 8-badge system (library, game page, store page,
including emoji mode), store-page injection, a gamebar row/panel, a floating
status window, per-game sections injected into the Steam library, CDP-driven
capture flows (§5.5), and an in-plugin help hub.

Ours and not theirs: the Desktop hand-off (`desktop_handoff.py` +
`quick_install_cli.py`) — arming a one-shot KDE autostart for work Game Mode
cannot perform — in-plugin self-update from our own releases, and a unified
component-health model (`components.py`) shared by all three components.

**[inferred]** On breadth of user-facing surface this is theirs by a wide margin,
and §2.5's shim inflation does not close the gap.

### §3.8 Auxiliary ecosystem

Fully inventoried in §2.3 and §2.6. Summary of leads:

- **DLC**: theirs, decisively (§2.6) — three layers to our one.
- **Acquisition breadth**: theirs — native *and* DepotDownloader, plus specific
  older builds and unowned DLC depots the native path cannot reach.
- **Cloud saves**: parity in capability, theirs in redundancy (§2.5, two live
  parallel systems).
- **Offline emulation**: ours — Goldberg has no counterpart.
- **Achievements**: parity (§2.4).
- **Online multiplayer**: parity (netsock both sides).
- **Portability beyond SteamOS**: ours — `platform_info.py` and the CachyOS port
  notes vs their `is_steamos()`.

### §3.9 Manifest durability — an unverified assumption in our own stack

Reading moon's manifest handling surfaced a concrete risk in **our** design. It
is recorded here because the chain is specific and testable, not because it is
proven.

**The Steam behaviour both projects are working around.** **[their claim]**
`manifeststore.hpp:5-7`: *"Steam purges `depotcache/<depot>_<gid>.manifest` after
a base commit and on re-plans."* moon builds two subsystems on that premise:

- **`ManifestStore`** — a durable archive under `~/.config/SLSsteam/manifests/`
  that *"Steam never touches"*, restored into `depotcache` on demand, no network
  needed. All versions are kept.
- **`prewarm`** (754 L) — a background worker that keeps every AddedApp's depot
  manifests staged **for every OS a key is held for**, re-staging to heal the
  post-commit purge. **[their claim]** `prewarm.hpp:12-18` names the two triggers
  precisely: *"forcing a Proton compat tool on a native-Linux AddedApp re-plans
  to the WINDOWS build, but its manifests were purged after the native commit ->
  BYldRequestDepotManifest -> 'Access Denied' -> one ~30s retry (confirmed in
  testing)"*, and *"Steam re-validating files hits the same gap."*

**What we do.** **[read]** `tools/steamidra_lite.py:274-286` (`_write_manifest_both`)
writes each manifest to **both** `depotcache/` and `config/depotcache/`, with this
rationale, inherited from SteaMidra and not independently verified:

> *"Steam lee de cualquiera de los dos según la fase, y sincronizarlos evita un
> 'Missing manifest' intermitente."*
> (Steam reads from either one depending on the phase.)

**[read]** Nothing restores them. lumalinux never touches `depotcache` at
runtime; LumaDeck reads it for lua enrichment (`downloads.py:796-851`) and
deletes from it on uninstall (`slssteam_ops.py:961`), and there is no
`config/depotcache` → `depotcache` copy anywhere in either project.

**The contradiction.** **[read]** moon does **not** appear to share our
assumption: `depotkey.hpp:17,84` and `depotkey.cpp:344` copy
`<Steam>/config/depotcache/*.manifest` **into** `depotcache` at startup. If Steam
read the config location directly, that copy would be pointless.

**Why this matters for us specifically.** **[read]** `downloads.py:1403` calls
`steam_utils.set_compat_tool_for_app(appid)`, which writes a `CompatToolMapping`
entry pinning the app to `proton_experimental` (`steam_utils.py:440`). That is
**exactly** the first trigger `prewarm.hpp` documents: forcing Proton on an app
re-plans to a different build whose manifests may have been purged.

**[inferred]** So the chain is: we force a compat tool → Steam re-plans → if Steam
purged `depotcache`, our only remaining copy is in `config/depotcache` → whether
that copy saves us rests entirely on an inherited claim that moon's own
implementation contradicts. If the claim is wrong, the symptom is a manifest
fetch falling through to `BYldRequestDepotManifest` and an Access Denied retry —
which would present to a user as an install or re-validate that stalls, not as an
obvious lumalinux failure.

**This is not proven.** moon may simply be belt-and-braces, and our comment may
be correct. It is recorded as the highest-value *testable* question this analysis
produced about our own stack: instrument a purge, then re-plan, and observe
whether Steam finds the manifest in `config/depotcache`. Until that is answered,
neither "we are covered" nor "we have a bug" is supportable.

### §3.10 Engine capabilities with no LumaDeck counterpart

Found by reading moon modules that the previous analysis never opened. Recorded
as inventory, not as recommendations.

| moon module | Capability | Our position |
|---|---|---|
| `manifeststore` (414 L) | Durable, purge-proof manifest archive with on-demand restore; keeps **all** gids | Partial — we write a second copy, never restore (§3.9) |
| `prewarm` (754 L) | Background re-staging across every OS a key is held for | None |
| `steamstub` (644 L) | **Automatic** Steamless run inside the `LaunchApp` hook, under a dedicated Wine prefix, before Proton starts | `steamless.py` — manual, user-initiated |
| `ticket` | `EncryptedAppTicket` capture and replay; ownership-ticket eresult stamping covering added apps *and* provisioned DLC ids | None — this is the Denuvo path |
| `parental` | Local parental-controls unlock | None (noted as D4 previously, still not applicable) |
| `appinfostate` (1.338 L) | Explicit appinfo state machine with policy separation | None — we depend on Steam's own appinfo |
| `ownerqueue` (726 L) | Owner-thread dispatch discipline for client calls | Equivalent by construction — our inotify thread only touches `NotifyKeysChanged` |

**[inferred]** `steamstub` is the most interesting of these for feature parity:
running Steamless automatically at launch rather than as a button is a UX
difference, but it also means their DRM removal is applied to the *currently
planned* build after any re-plan, where ours is applied once, manually, to
whatever was on disk at the time.

### §3.11 The layer where the quality gap actually sits

**[inferred]** The single most important structural conclusion of Phase 2, and it
cuts against reading either stack as a monolith:

**slsteam-moon's repository contains the artefacts of a disciplined process.**
> **Qualified by §12.2–§12.3.** An earlier wording read *"slsteam-moon is a
> disciplined project"*. Nothing here was executed: these are artefacts of
> process, not evidence of delivered reliability, and the only outcome evidence
> available disputes the stronger claim.

**[read]** ~55 test targets in its Makefile, 19 test shell scripts in `scripts/`, ABI-compatibility checks
(`check-sls-abi.sh`, `check-pattern-refresh-abi.sh`), a cryptographically signed
pattern feed, fail-closed ambiguity handling, and every one of its four most
recent pattern fixes shipped with a test.

**SLSDeckUniversal is not.** **[read]** Zero tests (§4.1); its only CI builds the
frontend on a `test` branch and runs `python3 -m compileall` — a **syntax check**,
not a test — then commits the bundle.

So the comparison is really two comparisons. **lumalinux vs moon is a fair fight
between two serious engineering projects**, and moon leads on several axes
(§3.3 gate 2, §3.3 gate 5b, §3.5 hard case, test coverage) while we lead on others
(§3.1, §3.3 gate 3, §3.5 detection). **LumaDeck vs SLSDeckUniversal is not a fair
fight on engineering discipline** — but SLSDeckUniversal wins on delivered
breadth anyway (§3.7, §3.8).

Any verdict in §7 that averages these two layers into one number will mislead.

---

## §4 Non-functional axes

### §4.1 Verification and test coverage

**[read]** SLSDeckUniversal: **zero test files** in the repository. The only CI
comprises **12 workflow files**, and **not one of them runs a test**: seven
invoke `python3 -m compileall` (a syntax check) and the rest only build the
frontend bundle. Most are single-purpose per-branch pipelines
(`fix-tokeer-snapshot`, `swap-qam-section-labels`, `wire-tokeer-guided-flow`,
`release-ui-redesign`…) left in place after their branch was merged — itself a
signature of the development process described in §1.2. This finding applies to the
**plugin layer only**: slsteam-moon, the engine beneath it, is comprehensively
tested (§3.11). Their CHANGELOG's *"280 assertions across
10 suites"* **[their claim]** is not reproducible by anyone.

**[read]** Ours: 9 Python suites in `LumaDeck/tests/`, plus C++ self-tests
(`tools/gmrc_xref_selftest.cpp`, `tools/vaddr_xlate_test.cpp`,
`tools/test_update_feed_guard.cpp`) and Python tool tests in `lumalinux/tools/`.

### §4.2 Code quality

**[read]** Broad exception swallowing, normalised:

| | `except Exception:` | per KLOC |
|---|---|---|
| SLSDeckUniversal | 1.207 / 32,2 KLOC | 37,5 |
| LumaDeck | 349 / 13,4 KLOC | 26,1 |

**[read]** Bare `except:` — **zero in both**. Recorded because it was tested as
a differentiator and is not one.

**[their claim]** Their own CHANGELOG documents the concrete harm: Denuvo
detection was *"entirely dead"* for every uncached appid because *"a bare except
swallowed it"*.

**An endpoint that reports success for work it did not do.** **[read]**
`downloads.py:1421-1429`:

```python
def install_game_build(appid: int, build_id: str) -> Dict[str, Any]:
    """Install a specific build version and pin it so updates don't overwrite it."""
    ...
    slssteam.pin_app_current(appid)
    return {"success": True, "appid": appid, "buildId": build_id, "pinned": True}
```

`build_id` is never read. No build is installed. The return value echoes the
requested `buildId` with `success: True`, so the UI reports an install-and-pin
that never happened. **[read]** Still wired at `main.py:1926,1929`, despite
their CHANGELOG listing it as *"deliberately not wired"* **[their claim]** —
which is itself evidence of §4.3.

**[their claim]** Their CHANGELOG's self-reported defect list is a precise
profile of fast machine-generated code: `rmtree` on an unvalidated `modid` that
resolved to `<library>/steamapps` (*"Workshop mod removal could delete every
installed game"*), `re.MULTILINE` patterns whose `\s` crossed a newline and
rewrote the user's `AdditionalApps` key to a bare `no`, manifest gids sorted as
strings, `priority: 0` ranking last through `(x or inf)`, non-atomic
`config.yaml` writes, unbounded caches.

**Recorded in their favour:** those defects were found, fixed and *published*.
That is more transparency than is typical in this ecosystem.

**Recorded in their favour:** `slssteam.py:3876-3893` is careful work. They
established by scanning the moon v2.8 binary that `ManifestPins` occurs 8 times
and `ManifestIds` zero — *"exactly inverted from upstream"* — and **refuse to
write a pin on stock SLSsteam** rather than guess a schema whose wrong shape
*"makes SLSsteam fail config parsing outright… disabling everything rather than
just pinning."*

### §4.3 Project health

**[read]** No `LICENSE` file. `package.json` declares BSD-3-Clause, while the
tree vendors third-party code under its own terms (§2.2).
**[read]** Ours: `LICENSE` (GPL-3.0) + `LICENSE-AGPL`, with per-file AGPL
headers on the ported SafeMode files; LumaDeck MIT, inherited from DeckTools.

**[read]** Their CHANGELOG describes 177 RPCs; `main.py` defines 366. The
documentation fell behind the code within days.

**[read]** `dist/` ships a committed prebuilt frontend bundle (`index.js`,
753 KB) and `dist/umipcompatd`, a **4,4 MB binary with no source in the tree**.

**[read]** Runtime dependencies declared in `requirements.txt`: `httpx`,
`py7zr`. Ours: none — `http_client.py:1` is a stdlib-only client written
specifically to avoid a runtime dependency.

### §4.4 Privilege hygiene — both plugins run as root

Recorded as its own axis because it is the one place where the two projects'
**shared** design decision (the Decky `root` flag, §5 preamble) creates
symmetric obligations, and both discharge them incompletely — in opposite
proportions.

#### Ownership of files created in the user's home

A plugin running as root that *creates* a file or directory produces a
root-owned one. Writing to an already-existing user-owned file preserves
ownership, so the exposure is confined to newly created paths.

**[read]** Theirs: 21 `os.makedirs` calls resolve into user-home paths;
**20 have no ownership fix**, among them `buildarchive.py:40`, `backup.py:136`,
`cloudsave.py:128`, `luatools.py:86`, `opensave.py:469`, `creamysteamy.py:68`,
`compat.py:252` and `denuvo.py:64`. Across the whole backend there are 87
`makedirs` and 84 write-opens against 19 `chown` sites
(`slssteam.py:1370,2093`, `ubisoft_packages.py:152`).

**[inferred]** They are aware of the class — `_chown_to_user` exists and is used
after the headcrab fetch — so this reads as incomplete application rather than
ignorance. The concrete failure mode is visible inside their own code:
`opensave.py:87` de-escalates the OpenSave daemon to the real user
(`sudo -u <user> env HOME=…`), while `opensave.py:469` creates directories for
it as root. A root-owned `0755` directory is traversable but not writable by
that daemon.

**[read]** Ours: only 2 `makedirs` resolve into user-home paths.
`paths.py:664` is followed by an explicit `chown` of both the directory and the
file (`:671-672`). `self_update.py:217` creates `~/Downloads` when missing and
does **not** chown it — a real, if minor, instance of the same defect on our
side. `dotnet.py:144-162` goes further than either project elsewhere: it runs
`chown -R` over the installed tree and, on failure, logs the exact command for
the user to repair it themselves.

**[inferred]** Asymmetric but not one-sided: 20 unfixed against 1. Root-owned
artefacts also **outlive the plugin that created them**, so this is part of what
a user inherits after trying one stack and returning to the other (§5.6).

#### Credentials at rest — **theirs is better, ours has a gap**

**[read]** Theirs restricts the files holding secrets: `settings.py:100,105`
`chmod`s both the temp file and the final settings store to `0o600`, and
`luatools.py:108` does the same for the token file. Their settings store holds
the manifest-source API keys and the Ryuu browser session captured over CDP
(§5.5).

**[read]** Ours does not. There is **no `0o600` anywhere in `LumaDeck/backend/`**.
`api_manifest.py:189-203` (`save_ryu_cookie`) writes the Ryuu session cookie to
`data/ryuu_cookie.txt` with a plain `open(path, "w")` and no mode change, then
mirrors it to the settings directory; `update_hubcap_key` (`:247`) follows the
same pattern.

**[inferred]** On a single-user Deck the practical exposure is limited, but
these are live session credentials written by a root process at default
permissions, and the counterpart project — with every other quality signal
against it — handles this correctly and we do not. Listed in §8.

#### Branch note

**[read]** `origin/tokeer-automation`, named in their README as *"active Tokeer
automation development and rolling builds"*, is **zero commits ahead of
`origin/main`**. There is no unreleased work there; the branch is a label on the
same tip.

### §4.5 The upstream author's assessment of moon

**[their claim]** AceSLS, author of SLSsteam — the project moon forked — has
described it as *"a vibecoded piece of shit adding broken features and breaking
existing ones."* Relayed to this analysis second-hand, not sourced from a
citable post.

Recorded because it is the only outside assessment of the engine layer available,
and tested because **the source is the most interested party in the ecosystem**:
the upstream author of a fork he did not make. Interest does not make a claim
false; it makes it worth checking. Three separable charges:

**"Vibecoded" — not supported for moon.** **[read]** 226 commits spanning
2026-05-31 to 2026-08-23, ~3 months, at 1–6 commits per day, with one primary
author (`unplausible`, 218) and three occasional contributors. Every one of the
four most recent pattern fixes shipped with a test (§3.11), and the Makefile
carries ~55 test targets. That is not the profile of bulk unreviewed generation.

**[inferred]** The label fits the *plugin* precisely — SLSDeckUniversal is 369
commits in 7 days, up to 125 in one day, by an author self-identified as
"Vibe-coder Jimmy", with zero tests (§1.2, §4.1). A pejorative accurate about one
layer appears to be travelling across the ecosystem onto the other.

**"Adding broken features" — partially supported.** **[read]** moon's own commit
subjects run **112 `fix` to 74 `feat`**, and several describe real breakage in
their own new work: *"drop empty depots that crash the client on reconfigure"*,
*"drop the app-info build pin that stalled startup"*, *"drop unsupported content
dlcs"*. **[inferred]** A 1.5:1 fix-to-feature ratio is high, though not
anomalous for a project hooking a target Valve keeps moving — the same pressure
produces our own `maintenance.md`.

**"Breaking existing ones" — supported, with a concrete instance.** **[read]**
moon dropped SLSsteam's hash-whitelist SafeMode
([`slsteam-moon-findings.md`](slsteam-moon-findings.md) M2), a safety mechanism
that existed upstream, and `chore: remove flatpak installation support` removes
another upstream capability. **[read]** moon still fetches
`AceSLS/SLSsteam/.../res/updates.yaml` at runtime while having discarded its own
equivalent (§3.5) — consuming upstream's safety feed after removing the local one.

**Assessment.** **[inferred]** Two of the three charges carry evidence; the
headline pejorative does not fit moon's development profile. The quote is
overstated as a whole and not baseless in its parts. Nothing in it displaces the
findings of §3 — the signed feed, the runtime probe validation and the test
coverage are in the repository and were read there — but §4.5 is the honest place
to record that the upstream author's view of this engine is sharply negative, and
that his specific complaint about removed safety mechanisms is correct.

---

## §5 Trust and risk

### §5.1 Supply chain — the highest-severity finding

**[read]** `slssteam.py:1474-1527` defines
`_download(url, dest, sha256: str = "")` with working SHA-256 verification:
*"If a sha256 is known, the file must match it or it is discarded."*

**[read]** It is called five times — `:1658`, `:1692`, `:3274`, `:3696`, `:3783`
— and **not one call passes a hash**. The capability is built and never used.

**[read]** What is fetched unverified and then executed:

- `slssteam.py:1126` — `HEADCRAB_RAW_URL =
  "https://raw.githubusercontent.com/Deadboy666/h3adcr-b/refs/heads/main/headcrab.sh"`,
  fetched cache-busted (`:1658`), `chmod 0o755`, then run (`_run_headcrab_shimmed`).
  A **live branch ref**: whatever is on `main` at that instant.
- The engine `.so` (moon) that is then loaded into Steam (`:1692`).
- The HV kernel module (§5.2) and the Tokeer runtime.

**[read]** 33 modules perform network I/O; 20 spawn subprocesses.

**Recorded honestly — we are no better on verification.** **[read]**
`setup.sh:1036,1088,1100,1109,1127` fetch `SLSsteam-Any-release.7z`,
`cloud_redirect.so`, `netsock.so`, `liblumalinux.so` and the tools with
`curl -fL` and **no checksum**. On this axis the two stacks are equal, and this
document does not claim otherwise.

**Where we do differ is provenance discipline, not verification.** **[read]**
`setup.sh:71-75` documents why we abandoned headcrab's rolling tag:

> *"that is a rolling tag on a fork whose script has been frozen since May: the
> file is replaced in place, so the URL says nothing about which version is
> behind it, and it has been seen lagging the real releases."*

We resolve versioned releases (`resolve_slssteam_asset()`,
`resolve_cloudredirect_asset()`). They fetch `refs/heads/main`.

### §5.2 The kernel module

**[read]** `hypervisor.py:31-33` fetches a prebuilt
`cpuid_fault_emulation-<kernel>.ko` from:

```
https://api.github.com/repos/PareidoliaDev/glowing-tribble/releases/latest
https://api.github.com/repos/2804u13j200-spec/glowing-tribble/releases/latest
```

**[inferred]** `glowing-tribble` is a GitHub auto-suggested repository name;
`2804u13j200-spec` has the shape of a throwaway account. Neither is a
recognisable upstream identity.

**[read]** `hypervisor.py:231-260`: the module is loaded with `insmod` **as
root**, after `modprobe -r kvm` / `kvm_amd`. No signature check, no hash.

**Recorded in their favour:** **[read]** `hypervisor.py:180-186` hardens the
*transport* — the URL must start with `https://`, and curl is passed
`--proto '=https' --proto-redir '=https'` so a redirect cannot drop to plaintext.

**[inferred]** That protects the channel, not the origin. Whoever controls those
two repositories — or takes either account — delivers an arbitrary kernel module,
as root, to every user who enables the feature. This is the single
highest-severity item in the comparison; nothing else is close.

**[read]** `dist/umipcompatd` (4,4 MB, no source, §4.3) runs alongside it
(`hv/umip.py:9-13`).

### §5.3 Archive extraction

**[read]** They implement `safe_extract` (rejecting symlink, hardlink and
absolute-path members) — defined twice, used four times. **Eight raw
`extractall()` calls bypass it**, and they are the consequential ones:

| Site | What it extracts |
|---|---|
| `slssteam.py:1582` | the engine `.so` injected into Steam |
| `hv/core.py:550`, `hv/operations.py:851,880` | the kernel-module archives |
| `tokeer.py:394` | the Tokeer runtime |
| `ubisoft_packages.py:149`, `creamysteamy.py:106` | activation packages, proxy source |

**[inferred]** The hardening their CHANGELOG advertises was applied to the
Archive/Backup paths and not to the download-and-execute paths.

**Recorded honestly — we have two of our own.** **[read]**
`LumaDeck/backend/downloads.py:956` calls `archive.extractall(tmp_dir)` on the
**third-party** zip from Hubcap/LuaTools, as root; `self_update.py:127` does the
same on our own release archive.

**Recorded in our favour.** **[read]** `tools/steamidra_lite.py:289-320`
(`extract_zip`) is safe by construction: it never calls `extractall`, iterates
`namelist()`, keeps only `Path(member).name` — discarding the archive path
entirely — and handles only `.lua` and `.manifest` members. Traversal is not
possible by design. The exposure is in the plugin path, not the lumalinux path.

### §5.4 Network surface, and the absence of telemetry

**[read]** 29 distinct hosts vs our 18.

Theirs and not ours: `buzzheavier.com`, `pixeldrain.com` (anonymous file hosts),
`cdn.discordapp.com`, `media.discordapp.net`, `steamdb.info`, `ziglang.org`,
`luastools.xyz`, `luatools.vercel.app`, `api.perondepot.xyz`,
`steamcdn-a.akamaihd.net`, `shared.fastly.steamstatic.com`, `dl.flathub.org`,
`steamdeck-packages.steamos.cloud`.

Shared: `toolsdb.piqseu.cc`, `api.steamcmd.net`, `applist.morrenus.xyz`,
`lua.tools` / `db.lua.tools` / `files.luatools.work`, `hubcapmanifest.com`,
`generator.ryuu.lol`, GitHub, Steam.

**Recorded in their favour, explicitly: no telemetry.** **[read]** Every
outbound `POST` resolves to the steamcmd API (`workshop.py:127,145`) or luatools
authentication (`luatools.py:164,209,307`). There is no phone-home to
author-controlled infrastructure. Given the rest of §5, this deserves stating
plainly.

### §5.5 CDP automation of the user's authenticated sessions

**[read]** ~4.093 lines drive the user's logged-in Steam CEF over
`localhost:8080/json` + `Runtime.evaluate`:

| Module | Behaviour |
|---|---|
| `hubcapCapture.ts` (130 L) | Scrapes the Hubcap API key from the DOM of the user's session |
| `steamdbCapture.ts` (133 L) | Scrapes full SteamDB manifest history (needs a signed-in session) |
| `keyCapture.ts` (133 L) | **Injects a `fetch`/XHR wrapper capturing the key from any JSON response body**, and clicks page buttons (Ryuu "Reset") to force a fresh key |
| `tokeerDiscordCapture.ts` (1.633 L) | Drives the user's Discord session to create tickets in the "DeDevision" guild (`discord.gg/denuvo`) |

Ours: `backend/cef_cdp.py`, 168 lines, Ryuu cookie import only.

**[inferred]** A real capability advantage, and simultaneously a dual-use
mechanism: a general-purpose credential interceptor operating inside the user's
authenticated browser context. It exfiltrates nothing today (§5.4); a
compromised update would not need to build the harvester.

*Tokeer's activation workflow itself is analysed in §5.7.*

### §5.6 Coexistence — they disable our stack on install

**[read]** `slssteam.py:3557-3579` (`_detect_foreign_engine`) probes
`~/.local/share/lumalinux`, `~/.config/lumalinux`, `~/.steam/steam/keys.txt`,
and greps `steam.sh` for `"lumalinux"`.

**[read]** `slssteam.py:3609-3646` (`disable_foreign_engines`), invoked when
Install is pressed, **renames** those paths to `*.slsdeck-disabled`. That
includes `~/.config/lumalinux`, which holds our `keys.txt`.

**Recorded in their favour — it is disclosed, not covert.** **[read]**
`sections/SlsSteamCompact.tsx:201` shows *"Detected lumalinux — Install will
disable it (reversibly) and set up slsteam-moon"*, and
`sections/Dependencies.tsx:344` offers it as an explicit, separately labelled
button. `system_status()` (`slssteam.py:3581`) exists so onboarding *"knows what
to do vs defer."*

Two substantive defects remain:

1. **There is no return path.** **[read]** `slsdeck-disabled` appears only
   inside `disable_foreign_engines`. No function, no button and no documentation
   restores the renamed artefacts. "Reversible" means the bytes still exist.

2. **Their model of our stack is obsolete, so the handover is incomplete.**
   **[read]** `slssteam.py:3612-3615`: *"a steam.sh-hook engine like lumalinux
   is disabled by renaming its deployed artifacts… after which the moon install
   reclaims steam.sh, so the foreign hook no longer loads."*
   **[inferred]** Since the wrapper model, lumalinux does not hook `steam.sh`
   (§3.1). Reclaiming `steam.sh` removes nothing of ours: the `.desktop`
   patches, the PATH drop-in, the systemd drop-in and the guardian all survive.
   The resulting state is not a clean handover but a **broken hybrid** —
   lumalinux still preloads on every boot with its key store renamed underneath
   it.

**[read]** A second, deeper incompatibility: the config schemas have forked
(§4.2). Pinning is spelled `ManifestIds` on stock SLSsteam (ours) and
`ManifestPins` on moon (theirs), with the nesting inverted. Coexistence does not
fail only over injection; the configuration files are mutually unintelligible.

**[read]** We implement no detection of their presence and no handling of
`*.slsdeck-disabled` artefacts. A user returning from their plugin lands in a
non-working state with no diagnostic. See §8.

### §5.7 Tokeer — the Denuvo activation path

The largest single feature in SLSDeckUniversal by frontend line count
(`tokeerDiscordCapture.ts` alone is 1.633 L) and the one with the most
consequential risk profile. Analysed here mechanically; the portability
assessment is at the end.

#### What it is

**[read]** `tokeer.py:1-6`, `docs/TOKEER_IMPLEMENTATION.md:3`: SLSDeck does not
vendor Tokeer. It fetches the upstream Linux runtime from
`Tesla697/TokeerDRM-App` at the moment the user prepares a game
(`RUNTIME_ZIP`, `tokeer.py:29`) and orchestrates the upstream verifier and
redeemer locally.

**[their claim]** `docs/TOKEER_IMPLEMENTATION.md:21-30` describes the upstream
model: a native hook (`ost_native_hook.so`) appended to `LD_PRELOAD` by a launch
wrapper, a validator that emits a signed setup report, and a redeemer that POSTs
a 6-character activation code to `/drm/redeem` and receives `app_id`,
`appticket` and `eticket`.

#### The transaction, step by step

1. **A signed setup report is produced locally.** **[read]** `tokeer.py:627-644`
   runs the upstream validator and extracts a `TLX1.<payload>.<signature>` token,
   decoding the base64url payload locally to show which setup checks passed
   (`_decode_tlx`, `:616-624`). **[their claim]** the checks cover install folder,
   Proton prefix, native hook, launch option and Proton mapping.
2. **That report is uploaded into a Discord ticket, by the plugin, as the user.**
   **[read]** `tokeerDiscordCapture.ts` drives the user's signed-in Discord
   session over CDP: sign-in state detection (`getDiscordSignInState`), ticket
   discovery, composer focus, and `cdpSetDiscordFileInput` (`:121`) — programmatic
   population of a file input to attach the report. Target guild
   `1464130182364270696`, invite `discord.gg/denuvo`, named "DeDevision"
   (`:4-14`).
3. **A 6-character code comes back and is redeemed.** **[read]**
   `tokeer.py:429` overwrites the upstream `server_config.py` with
   `SERVER_URL = "https://luastools.xyz"`, so the redeem request goes to that
   host rather than upstream's default.
4. **Real Steam ownership tickets are written into the game's prefix.**
   **[their claim]** `docs/TOKEER_IMPLEMENTATION.md:45-52`: the redeemer writes
   `AppTicket` and `ETicket` as `REG_BINARY` under
   `HKCU\Software\Valve\Steam\Apps\<appid>` inside
   `steamapps/compatdata/<appid>/pfx`.

**[read]** A separate, folder-based **Ubisoft mode** exists: no native hook, no
launch-option rewrite; `upc_r2` + `dbdata` folders. SLSDeck hosts its own "care
packages" for it (`ubisoft_packages.py:19-21`) from its own GitHub release,
indexed by `assets/ubisoft-packages/hostedgames.json` with per-game
`carePackageId`, `ubisoftProductIds`, `tokenRequestIds` and a `sourceSha256`.

#### Recorded in their favour

**[read]** `cc8d1f5` ("Generate genuine Ubisoft Tokeer verification codes") is
**not** code forgery, despite how the subject reads. The upstream `verify-ubi`
wrapper occupies a positional slot such that the validator silently fell back to
Steam mode; the fix passes the correct arguments and adds a guard that
**refuses to submit** a code whose actual mode does not match the requested one
(`tokeer.py:634-639` and the mismatch branch). It is a correctness fix with a
fail-closed check.

**[read]** `hostedgames.json` pins a `sourceSha256` per care package — the one
place in the codebase where a fetched artefact carries a recorded hash (contrast
§5.1).

#### Risk assessment

**[inferred]** Five distinct exposures, listed by severity:

1. **Ownership tickets of unknown provenance.** The `appticket`/`eticket` pair
   written into the user's prefix is a *real* Steam ownership credential that the
   user did not obtain from Valve. Nothing in the reviewed code establishes where
   the issuing service sources them. This is the substance of the feature and the
   irreducible risk in it.
2. **The user's own Steam account carries it.** The tickets are presented from
   the user's machine and prefix. Any correlation by Valve or the publisher lands
   on the user's account, not on the service's.
3. **The user's own Discord account transacts it.** The automation acts *as the
   user* in a piracy-oriented guild — sign-in, ticket creation, attachment
   upload. The account bearing that activity is theirs.
4. **A third-party service receives a signed machine report.** The `TLX1` payload
   is produced by an upstream binary that SLSDeck fetches rather than vendors, so
   its exact contents cannot be established from this repository alone. What is
   established is that it is uploaded off-machine.
5. **Fetch-and-execute, again.** **[read]** The runtime zip is fetched from a
   GitHub releases URL and executed; if the bundle carries no prebuilt hook,
   `tokeer.py` **compiles one on the device** (`_run_as_user(["bash", build])`).
   §5.1 and §5.3 apply in full.

**[read]** Licensing, from their own planning document
(`docs/TOKEER_IMPLEMENTATION.md:59-62`): *"The upstream repository currently
exposes no repository license. Avoid copying/vendoring its Python/C source into
SLSDeck unless the upstream author supplies compatible licensing/permission."*
They followed their own advice — the runtime is fetched, not vendored. Recorded
in their favour, and noted as a live constraint for anyone considering the port.

#### Portability

**Technically**: high. SLSDeck's own layer here is orchestration — process
invocation, CDP automation, file placement. There is no reverse engineering in
it, and no part of it depends on their architecture.

**In substance**: the feature is not a capability that can be built, it is a
*relationship* that must be entered. Porting it means becoming a client of a
third-party activation service, driving the user's authenticated Discord session
on their behalf, and injecting Steam ownership credentials of unestablished
origin into their prefix. The engineering is the small part; the three exposures
above are the feature.

**Assessment for LumaDeck: do not port.** Not on legal or moral grounds, which
are the user's to weigh, but on the same engineering grounds this document
applies elsewhere — it fails the test that §5.1, §5.2 and §5.3 apply to every
other fetched artefact, and it moves the blast radius of a failure onto the
user's own accounts, where LumaDeck cannot contain it. This is the one item in
§8 recorded as a red line rather than a gap.

### §5.8 What their code knows about LumaDeck

Prompted by a public statement from the SLSDeck author, relayed to this analysis
(undated, and not independently sourced):

> *"What is lumadeck? With dlcs you just add them as another game with sls. In
> newer versions sls deck has smoke api integration but it wasn't tested yet."*

Their codebase is not neutral about LumaDeck. The complete inventory, and then
the part that matters — which is the alternative explanation.

#### Every reference, and when it arrived

**[read]** In the current tree, outside the compiled bundle:

| Location | Content |
|---|---|
| `slssteam.py:3558` | *"is a **different** depot-key engine (e.g. **LumaDeck's lumalinux**) already managing injection?"* |
| `slssteam.py:3562,3564,3621,3622` | Our three install paths, by name: `~/.local/share/lumalinux`, `~/.config/lumalinux`, `keys.txt` |
| `slssteam.py:3574` | A grep of `steam.sh` for the literal string `lumalinux` |
| `slssteam.py:3612` | *"a steam.sh-hook engine like **lumalinux** is disabled by renaming its deployed artifacts"* |
| `src/sections/Archive.tsx:21` | *"**Modelled on LumaDeck's** list → detail layout (GameList → GameDetail)"* |
| `depot_history.py:6`, `hvauto.py:5,11` | *"Ported/adapted from **SteaMidra's** `depot_history.py`"* — our lineage |
| `dist/index.js` | One surviving `LumaDeck` string in the shipped bundle |

**[read]** Timeline: `disable_foreign_engines`, the detection paths, and the UI
copy *"Detected lumalinux — Install will disable it (reversibly)"* are all
present in **`6da03a1`, the first commit of the repository** (2026-08-20,
v0.9.59). None was added later. The knowledge arrived complete, with the code
drop.

**[read]** One reference was **removed**. Commit `65232aa`, *"feat: add
**Luma-style** session and header auth transport"*, added an `httpc.py` docstring
reading *"This mirrors the useful part of **LumaDeck's** auth model without
duplicating auth logic throughout every downloader call site."* The feature was
Ryuu-session capture over Steam's CEF — our `ryuu_cookie.py`. The docstring no
longer exists in `httpc.py`; it survives only in history.

**[read]** A development branch was named **`lumainject`**, with its own CI
workflow (`.github/workflows/build-lumainject.yml`, still present; the branch is
gone from the remote). Merged as `c4b5a81` on 2026-08-21, it carried
`live_refresh.py` — the no-restart hot-add — plus `cloudredirect_reinstall.py`,
`survival_backup.py`, `depot_cleanup.py`, and edits to
`SlsSteamCompact.tsx`/`Dependencies.tsx`, the two files that render the
foreign-engine notice.

#### What this does and does not establish

**[inferred]** **Established:** the codebase knows LumaDeck in operational
detail. It knows where lumalinux installs, what its key store is called, how it
used to hook Steam, that it is a depot-key engine rather than an ownership one,
and it ships a feature whose sole purpose is to switch it off. One UI component
cites LumaDeck as its design reference.

**Not established: that any person knew.** This is the honest limit, and it is
load-bearing. §1.2 records that this code is machine-generated at a rate of up to
125 commits per day, by an author handle of "Vibe-coder Jimmy", in a sandbox whose
own stub files complain that *"the mount blocks deletion"*. A model assembling a
Decky plugin for the SLSsteam ecosystem would surface LumaDeck and lumalinux from
its own knowledge without the operator ever reading a line of it — including the
outdated `steam.sh` model (§5.6), which is precisely the kind of stale detail a
model reproduces and a person tracking the project would not.

> **Revised by §11.3.** The reading below was written before a second field
> report surfaced, in which a reviewer names LumaDeck to the developer twice — by
> repository path — in a review whose every other technical point was subsequently
> implemented. The innocent explanation survives as *possible*; it is no longer
> the *likely* one. Read this paragraph with §11.3.

**[inferred]** So the plain reading is available and defensible: the code knows;
the person plausibly does not. The public statement and the codebase are
consistent with a human who has never looked at LumaDeck shipping a tool that
has. That reading also explains the deleted attribution better than concealment
does — a later generation pass rewrote a docstring without preserving a citation
it never understood as one.

**[inferred]** What survives regardless of who knew: the disable path is real, it
is aimed at us by name, its model of our stack is three iterations out of date,
and it has no return path (§5.6). That is a defect to document whether it was
authored deliberately or generated incidentally.

### §5.9 Is any code copied from LumaDeck? — measured, and no

`slsdeck-findings.md` answered this by grepping for names and concluded
"convergence, not copying" with the caveat that re-implementation leaves no
trace. That caveat is now unnecessary: the question was measured.

#### Method

Both Python backends were normalised — comments, docstrings, imports and
whitespace stripped, lines under 12 characters dropped — and reduced to sets of
*n* consecutive identical lines originating in the same file. The same was done
for both frontends. **[read]**

The decisive control is the third corpus: **`lopesleo/DeckTools`**, which
LumaDeck is a declared fork of. Any sequence shared by LumaDeck and
SLSDeckUniversal that *also* appears in DeckTools is inherited from a common
ancestor, not copied between them.

#### Result

| Threshold | Shared LumaDeck ↔ SLSDeck | Also in DeckTools | **Exclusive** |
|---|---|---|---|
| 3 lines | 105 | 99 | 6 |
| 4 lines | 73 | 72 | 1 |
| 8 lines | 23 | 23 | **0** |
| 12 lines | 11 | 11 | **0** |
| Frontend, 4 lines | 1 | 1 | **0** |
| Frontend, 8 lines | 0 | — | **0** |

**[read]** The seven "exclusive" hits at the 3- and 4-line thresholds are
`appid = int(appid)`, bare `except Exception:` blocks, and equivalents — the
noise floor of two Python codebases doing similar work. Above four lines the
exclusive count is **zero**, in the backend and in the frontend alike.

**[inferred]** **No code was copied from LumaDeck.** Every non-trivial shared
sequence traces to DeckTools. The frontends — 5.911 lines against 18.242 — share
nothing at all beyond one four-line fragment that is itself inherited.

#### The shared inheritance, sized

**[read]** Measured against DeckTools at the 8-line threshold:

| | Sequences shared with DeckTools |
|---|---|
| LumaDeck (declared fork) | **1.226** |
| SLSDeckUniversal | **22** |

**[read]** Their 22 are concentrated in `utils.py` (19), `downloads.py` (2) and
`config.py` (1) — the manifest-text normalisation helpers and the
`DEFAULT_HEADERS` constant, whose `User-Agent` still points at
`BossSloth/Steam-SteamDB-extension`.

**[inferred]** So the resemblance §5.8 catalogues is not derivation from us. Both
projects independently carry a small set of DeckTools helpers, and everything
else was arrived at separately. This is the ecosystem-convergence reading the
previous document reached by inference, now established by measurement — and it
should be stated at least as plainly as the §5.8 findings that cut the other way.

**[read]** One asymmetry worth recording: their README credits h3adcr-b,
SLSsteam/moon, Ryuu and perondepot, and does **not** credit DeckTools, whose code
it carries. DeckTools is MIT, so the use is unencumbered; the attribution is a
separate matter. LumaDeck credits it in its opening line.

---

## §6 Weighted scoring matrix

### §6.1 The scores

Weights are those fixed in Phase 0, **before any of their code was read** (§0).
Scores are 0–10, assigned from the sections cited, and are the author's
judgement — the sensitivity analysis in §6.3 exists because that judgement is
the weakest link in this document.

| Axis | Weight | Ours | Theirs | Basis |
|---|---|---|---|---|
| A1 Install end-to-end | 15% | 6.5 | **8.0** | §2.3, §3.3. Theirs: native *and* direct-download, specific older builds, unowned DLC depots, gate 2 covered, bad-key quarantine. Ours: one path, gate 2 exposure, §3.9 unresolved. |
| A2 Steam-update survival | 15% | 6.5 | **8.5** | §3.5. Theirs: signed catalog recovers a moved prologue as a data update. Ours: automated daily detection and escalation, but that case needs a human. |
| A3 Injection & boot coverage | 8% | **9.0** | 4.0 | §3.1. `steam.sh` vanilla vs a patch with a re-patch loop and three overlapping mechanisms. |
| A4 Game updates & pinning | 7% | 6.0 | **8.0** | §3.10, §2.3. Theirs adds build archive, SteamDB history and rollback. |
| A5 Failure modes & recovery | 10% | **7.5** | 6.5 | §3.4, §3.6. Ours: cross-component interlock, fail-closed resolution. Theirs: quarantine and plugin-layer self-heal, offset by zero tests (§4.1) and an endpoint that reports false success (§4.2). |
| B1 Game features | 12% | 5.0 | **9.0** | §2.6, §3.8. DLC in three layers vs one; Workshop, artwork, backups, Denuvo. Ours: Goldberg, achievements. |
| B2 UX / Steam integration | 8% | 6.0 | **8.0** | §3.7. 366 RPCs vs 106; badges, store injection, gamebar. |
| C1 User risk | 10% | **7.5** | 3.0 | §5.1, §5.2, §5.5, §5.7. Kernel module from a throwaway account, a third-party activation service transacted on the user's own accounts, a generic credential interceptor — against our unverified downloads and one third-party `extractall`. |
| C2 Quality & maintainability | 8% | **7.0** | 5.0 | §4.1, §4.2, §3.11. Split by layer: their engine is better tested than ours, their plugin has no tests at all, and the plugin is where 32k of their lines live. |
| C3 Project health | 7% | **7.0** | 5.0 | §4.3. Continuous history, licence files, CI, docs vs a 7-day squashed drop, no `LICENSE`, a stale CHANGELOG and a release tag on the first commit. |

> **Read with §12.4.** These scores mix measured properties with quality
> judgements that §12.2 shows are weakly grounded, and nothing was executed.

**Totals: ours 6.72, theirs 6.78** — revised downward for us in §10.2 after
the adversarial pass; as first computed they were 6.79 / 6.70, i.e. the other
way round.

### §6.2 The aggregate is not usable, and that is the finding

A margin of **0.06 on a 10-point scale** is far below the resolution of the
scoring judgement behind it. **[inferred]** It should not be reported as a win,
quoted as a result, or used to choose a stack.

### §6.3 Sensitivity

A two-point revision on **any single axis** — including the 7%-weighted ones —
moves the total by more than 0.09 and reverses the ordering:

| Axis | Weight | Swing from a 2-point revision | Reverses? |
|---|---|---|---|
| A1, A2 | 15% | 0.30 | yes |
| B1 | 12% | 0.24 | yes |
| A5, C1 | 10% | 0.20 | yes |
| A3, B2, C2 | 8% | 0.16 | yes |
| A4, C3 | 7% | 0.14 | yes |

Ten out of ten axes are individually decisive. **[inferred]** That is the
signature of two systems that are genuinely comparable in aggregate and
different in composition — which is why §7 does not report a winner.

### §6.4 The two-level view

Per §3.11, the same numbers separated by layer:

**Engine — lumalinux vs slsteam-moon.** A fair fight between two serious
projects, and moon leads on more axes than we do.

| Ours leads | Theirs leads |
|---|---|
| Injection model (§3.1) | Gate 2, anonymous appinfo (§3.3) |
| Gate 3, package-0 timing (§3.3) | Gate 5b, bad-key quarantine (§3.3) |
| Gate 5, live key ingestion (§3.3) | Moved-prologue recovery (§3.5) |
| Automated update detection (§3.5) | Test coverage (§3.11) |
| Cross-component interlock (§3.4) | Manifest durability (§3.9) |

**Plugin — LumaDeck vs SLSDeckUniversal.** Not a fair fight on engineering
discipline, and they win on delivered breadth regardless.

| Ours leads | Theirs leads |
|---|---|
| Tests, 9 suites vs 0 (§4.1) | Feature surface, 366 RPCs vs 106 (§3.7) |
| Risk posture (§5) | DLC depth (§2.6) |
| Provenance discipline (§5.1) | Acquisition breadth (§2.3) |
| Project health (§4.3) | Self-heal tooling (§3.6) |
| Privilege hygiene, 20:1 (§4.4) | Credentials at rest (§4.4) |

---

## §7 Verdicts by user profile

**[inferred]** Re-weighting the same ten axis scores for four realistic priority
sets. The base row is §6.1. The spread between profiles (−1.75 to +2.42) is an
order of magnitude larger than the spread in the aggregate (−0.06), which is the
whole argument for reporting it this way.

| Profile | Ours | Theirs | Verdict |
|---|---|---|---|
| Base (agreed weights) | 6.72 | 6.78 | **Too close to call** |
| Wants it to work and not touch it | 7.03 | 6.88 | **Ours**, narrowly |
| Wants features | 6.11 | 7.86 | **Theirs**, decisively |
| Concerned about their account | 7.30 | 4.88 | **Ours**, decisively |
| Maintainer — they fix it when it breaks | 7.15 | 6.05 | **Ours**, clearly |

### §7.1 "I want it to work and I don't want to think about it"

**Ours.** Weighting A2, A3 and A5 heavily is what this profile means, and that is
where the injection model (§3.1) and the interlock design (§3.4) pay. The
counterweight is honest: they now have no-restart add too (§2.4), and if Valve
moves a prologue they may be back before we are (§3.5).

### §7.2 "I want the features"

**Theirs, and it is not close** — the largest single margin in the table. DLC in
three layers (§2.6), specific older builds, unowned DLC depots (§2.3), Workshop,
artwork, backups, and Denuvo. Nothing in our roadmap closes this in one step, and
§8 marks only part of it as worth closing at all.

### §7.3 "I care about Denuvo"

**Theirs, as the only option** — and this document declines to score it as a
feature win. The mechanism is §5.7: ownership credentials of unestablished origin
injected into the user's prefix, requested through the user's own Discord
account, presented from the user's own Steam account. It works. What it costs is
not visible in a feature matrix.

### §7.4 "I don't want to risk my accounts"

**Ours, decisively** — the largest margin in our favour. Not because our supply
chain is clean (§5.1 says plainly that it is not), but because the failure modes
differ in kind: ours are bounded by what a bad `.so` can do to a Steam install,
theirs include a kernel module (§5.2) and an activation service transacting on
the user's own credentials (§5.7).

### §7.5 "I am the one who fixes it when it breaks"

**Ours** — and this is the profile the author occupies, which is worth naming as
a bias rather than leaving implicit. lumalinux's hooks, patterns, feed and
maintenance procedure are ours to repair; moon's are not. The mirror image is
that moon's maintainers are demonstrably competent (§3.11), so "not ours" is not
the same as "unreliable".

### §7.6 Running both

**Do not.** §5.6: installing theirs renames our key store with no return path,
their model of our injection is three iterations out of date so the handover
leaves a broken hybrid, and the two config schemas have forked (`ManifestIds` vs
`ManifestPins`). Root-owned artefacts from either plugin outlive it (§4.4). This
is the one recommendation in the document that applies to every profile.

---

## §8 Findings list

*Nothing below is implemented, and this document does not implement it. The
ordering is by what §6 and §7 say actually moves a verdict, not by effort.*

**What the scoring says about priority.** The axes where we are weakest (A1
install breadth 6.5, B1 features 5.0, A4 pinning 6.0) are also the axes this
document argues are theirs by strategy rather than by our neglect (§2.2:
aggregation vs engineering). Chasing them means competing on their terms. The
axes where we lead — A3 injection 9.0, C1 risk 7.5, A5 failure modes 7.5 — are
cheap to defend and expensive for them to match.

**[inferred]** So the defensible reading of §6.3 is not "close a 0.09 gap". It is:
answer item 0 because it may be a live defect, close items 1–5 because they are
small and they erode the axes we actually win, and treat 6–12 as strategic
choices rather than a backlog.

**Ours to answer first — a question, not a fix:**

0. **Does Steam read `config/depotcache`?** (§3.9). Our manifest durability rests
   on an inherited, unverified claim that it does; moon's startup copy implies it
   does not. We force a compat tool (`downloads.py:1403`), which is exactly the
   re-plan trigger their `prewarm` was built for. Instrument a purge and a
   re-plan before deciding whether anything needs building. Every other item
   below is subordinate to this answer.

**Ours to fix, surfaced by this analysis:**

1. `LumaDeck/backend/downloads.py:956` — `extractall()` on a third-party archive
   as root (§5.3). `steamidra_lite`'s basename-only pattern is the in-house
   precedent.
2. No detection of `*.slsdeck-disabled` artefacts (§5.6). A user returning from
   their plugin is silently broken; we would be the only side offering a return
   path.
3. `setup.sh` verifies no checksums (§5.1). Provenance discipline is not a
   substitute for verification.
4. **Credentials at rest are unrestricted** (§4.4). No `0o600` anywhere in
   `backend/`; the Ryuu session cookie and Hubcap key are written by a root
   process at default permissions. Their plugin does this correctly and ours
   does not.
5. `self_update.py:217` creates `~/Downloads` as root without chowning it
   (§4.4).

**Gaps where they lead — plugin layer:**

6. DLC layers 2 and 3 (§2.6) — the clearest functional gap, and the one that
   does not require adopting any of their risk surface.
7. Direct-download acquisition as a fallback for what the native path cannot do:
   specific older builds, unowned DLC depots (§2.3).

**Gaps where they lead — engine layer (lumalinux vs moon):**

8. **Anonymous appinfo provisioning** (§3.3, gate 2). The one capability that
   closes an install case we cannot currently survive, and portable in principle:
   an outbound CM session plus an offline `appinfo.vdf` write, neither of which
   collides with SLSsteam's message hooks.
9. **Bad-key detection** (§3.3, gate 5b). We cannot distinguish a wrong key from
   a missing key by construction. Investigate whether `content_log.txt` already
   carries the signal before considering a chunk-level hook.
10. **Signing the RVA feed** (§3.5). The feed decides where hooks are installed
   inside Steam's process; ours is authenticated only by a hardcoded HTTPS source.
   moon bakes an Ed25519 public key in at build time and fails the build without
   it.
11. **Recovery from a moved prologue** (§3.5). Our RVA derivation scans with the
   existing pattern, so the case that most needs a server-side fix is the one
   case we cannot fix server-side.
12. **Beta-channel lookahead** (§10.1). `fetch_steamclient.py` tracks
    `steamdeck_stable` by design and correctly so, but they hold derived, signed
    locators for Beta builds *before* promotion. Watching beta as an
    early-warning input — deriving without whitelisting — is the cheapest item
    on this list and directly targets A2, the axis §10.2 revised against us.
13. **Signing and widening the RVA feed** (§10.1). One build covered against
    six. The feed is young and the channel has been quiet, so this is not
    neglect, but it is also not yet the mechanism §3.5 describes it as.
14. **Automatic DRM removal at launch** (§3.10). Their `steamstub` runs Steamless
    inside the `LaunchApp` hook, so it applies to the currently planned build
    after any re-plan; our `steamless.py` is a one-shot manual action against
    whatever was on disk at the time.

**Red lines — not gaps, and not recommended at any priority:**

- **Tokeer** (§5.7). The only item here classed a red line rather than a
  declined gap. Its engineering is orchestration and would port easily; its
  substance is a relationship with a third-party activation service, transacted
  through the user's own Discord and Steam accounts, delivering ownership
  credentials of unestablished origin. It fails the same supply-chain test this
  document applies to every other fetched artefact, and it puts the blast radius
  somewhere LumaDeck cannot contain it.
- **The prebuilt kernel module** (§5.2) — an unsigned `.ko` from a throwaway
  account, `insmod`-ed as root.
- **The generic CDP credential interceptor** (§5.5) — a `fetch`/XHR wrapper
  capturing keys from any JSON response inside the user's authenticated session.
  Note that our own `cef_cdp.py` is the narrow, single-purpose version of this
  same mechanism; the objection is to the general form, not to CDP as such.
- **The `steam.sh` patching model** (§3.1).

---

## §9 Appendices

### §9.1 Method note — what "reversible" and "disclosed" mean here

Two findings in §5.6 could be written as hostile and are not: the disable is
announced in the UI before it happens, and it renames rather than deletes. The
criticism that survives is engineering, not intent — no return path, and a model
of our stack three iterations out of date.

### §9.2 References

- **SLSDeckUniversal**: [`Kaal31/slsdeck`](https://github.com/Kaal31/slsdeck) @ `c243cf3`
  — `main.py`, `py_modules/lt/{slssteam,downloads,fixes,assella,depotdl,hypervisor,tokeer,opensave,creamysteamy,dlcdepot,live_refresh,httpc}.py`,
  `py_modules/lt/hv/*`, `src/lib/*Capture*.ts`, `src/sections/*`, `CHANGELOG.md`,
  `plugin.json`.
- **slsteam-moon**: [`swwayps/slsteam-moon`](https://github.com/swwayps/slsteam-moon) @ `d3402a1` (v2.8);
  see [`slsteam-moon-findings.md`](slsteam-moon-findings.md), especially M7.
- **steam-monitor**: [`swwayps/steam-monitor`](https://github.com/swwayps/steam-monitor) @ `e82030d`
  — the signed per-build locator feed moon consumes (§10.1).
- **SLSsteam (stock)**: [`slssteam-analysis.md`](slssteam-analysis.md).
- **Ours**: `jayool/LumaDeck` @ `eea30e5`, `jayool/lumalinux` @ `87ee544`.
- **DeckTools**: [`lopesleo/DeckTools`](https://github.com/lopesleo/DeckTools)
  — the common ancestor, used as the control corpus in §5.9.
- **Superseded**: `slsdeck-findings.md` (2026-07-22) — analysed a repository
  that no longer exists (§1.1).

---

## §10 Adversarial pass

Phase 5. Each conclusion favouring our own stack was attacked with their code in
hand. Four claims were tested; **two were revised, one was narrowed, one held.**
The revisions changed the §6 result, which is recorded rather than smoothed over.

### §10.1 REVISED — "their monitoring cadence is not observable"

**The claim (§3.5, as originally written):** moon has no `.github/` directory, so
whether they detect a Steam update before their users do *"is unverifiable from
here."*

**The attack:** their pattern feed names its own source — `swwayps/steam-monitor`
(`catalog.cpp:775`) — and that repository was never opened. Unverifiable was an
assumption, not a finding.

**Result: the claim was wrong.** **[read]** `swwayps/steam-monitor` publishes
`chore(patterns): publish Steam locator metadata` on **2026-08-01, 08-08 (×2),
08-11, 08-13, 08-19 and 08-27** — every 3–8 days, most recently the day before
this document's freeze. Its `linux32/steamclient/` holds **6 build catalogs, each
with a detached `.toml.sig`**, keyed by `module_sha256`, `module_size`,
`gnu_build_id` and `steam_version`, marked `source = "deterministic"`. Its README
tracks both **Stable and Beta** channels and is auto-generated.

**The comparison this forces.** **[read]** Our `res/rvas/` holds **one** build
catalog. `res/updates.yaml` whitelists 10 hashes, but the whitelist is advisory
(`maintenance.md` §A.1); only the RVA feed gives prologue-independent resolution,
and it covers one build.

**Mitigation, verified.** **[read]** The RVA feed landed 2026-08-12 (`2efd3ea`),
15 days before the freeze, and `watch-steam.yml:94,229,373` does wire
`--emit-rvas`. Steam's **Stable channel has not moved since 2026-08-03**
(steam-monitor's own README), so one entry is a young feed on a quiet channel,
not a neglected one.

**What survives as a real difference.** **[read]** `tools/fetch_steamclient.py:55-60`
defaults to `steamdeck_stable` deliberately — *"tracks exactly what Decks run"* —
and that choice is correct for our target. But it means we have **no lookahead**:
they hold derived, signed locators for Beta builds before those promote to
Stable; on promotion day they are already covered and we begin deriving. Cheap to
close without whitelisting beta (§8).

### §10.2 REVISED — the A2 score

**[inferred]** §10.1 makes the original A2 scoring (ours 7.0, theirs 8.0)
untenable. Their maintenance infrastructure is not merely structurally better on
the hard case (§3.5) but demonstrably better resourced: six signed catalogs
against one unsigned, two channels against one, and a publishing cadence that is
now observable and current. Revised to **ours 6.5, theirs 8.5**.

**This reverses the aggregate.** Base totals become **ours 6.72, theirs 6.78** —
theirs, by 0.06.

**[inferred]** That is the single most useful result in this document, and not
because of who is ahead. §6.3 predicted that a two-point revision on any one axis
would reverse the ordering. A one-point revision on one axis, made by the author
against his own side, did exactly that. The aggregate is confirmed unusable as a
decision tool; the per-profile verdicts (§7) barely moved, which is why they are
the output.

### §10.3 NARROWED — "the injection model is ours, decisively"

**The claim (§3.1):** their `steam.sh` patch is reverted by Steam and needs a
re-patch loop; our wrapper cannot be reverted.

**The attack:** we also run a re-assertion loop. **[read]** `setup.sh:97-99,765`
installs `lumalinux-desktop-guardian.{service,path,timer}` running
`ensure-desktop-coverage.sh --guardian`. A `.path` unit plus a `.timer` is
structurally the same shape as their re-patch loop, and our own README lists
*"a Steam update regenerated a `.desktop`"* as a live failure mode.

**Result: the claim holds, narrowed.** Two differences survive the attack and
both are load-bearing:

1. **What is being re-asserted, and how often it breaks.** Steam rewrites
   `steam.sh` from its own bootstrap as designed — their reversion is routine and
   injection is off until re-patched. `.desktop` regeneration is an occasional
   event, not a designed one.
2. **Redundancy.** **[read]** We have three independent paths — patched
   `.desktop`, a PATH drop-in, and a systemd drop-in on `steam-launcher.service`.
   `setup.sh:967-971` records that **Game Mode rides the systemd drop-in**, not
   the `.desktop` path (*"Game Mode launched steam-launcher.service vanilla while
   Desktop — which uses the .desktop/PATH path, not systemd — worked"*). The
   guardian therefore protects the Desktop path; the primary platform's injection
   does not depend on the mechanism the guardian repairs.

**[inferred]** The honest statement is not "we never need re-assertion" — we do.
It is that our re-assertion covers one of three redundant paths, and the one Game
Mode actually uses is not the fragile one. A3 stands at 9.0/4.0.

### §10.4 HELD — the risk axis

**The claim (§6.1, C1):** ours 7.5, theirs 3.0.

**The attack:** our supply chain is unverified too (§5.1, conceded), lumalinux is
itself a `.so` injected into Steam, and a compromised lumalinux release would be
severe. Is the gap in kind, or is it self-flattery?

**Result: held.** **[inferred]** The blast radii differ in kind, not degree. Our
worst case is bounded by what a malicious `.so` can do inside a Steam process on
that machine. Theirs includes a kernel module fetched unsigned from a throwaway
GitHub account and `insmod`-ed as root (§5.2), and an activation service
transacting on the user's own Steam and Discord credentials (§5.7) — a failure
there follows the user off the machine and onto accounts LumaDeck could not
contain even in principle. The concession that we do not verify our downloads is
already in §5.1 and does not close that distance.

### §10.5 Errors corrected during the analysis

Recorded so the document's own reliability can be judged. Six claims were wrong
when first written, four found before Phase 5 and two by it:

| Claim | Direction | Where |
|---|---|---|
| Gate verdicts carried forward as "still current" without re-verification | Method failure | §3.3, re-verified |
| "Their no-restart verifies where ours assumes" | Against us | §3.4 |
| Gate 5 scored as parity | For them | §3.3 |
| Their `steam.sh` regression treated as their only injection model | Overstated | §3.1 |
| "Their monitoring cadence is unverifiable" | For us | §10.1 |
| A2 scored 7.0/8.0 | For us | §10.2 |

**[inferred]** Five of the six favoured our own stack before correction. That
rate is the strongest available argument for reading §6 and §7 as estimates with
error bars rather than as measurements.

---

## §11 Field reports

Everything above is static analysis: it measures the *probability* of failure.
This section is the only place the document touches observed outcomes.

**Provenance.** User-relayed public comments from before the current release,
undated and not independently sourced. They are tagged **[field, unverified]**
and carry no weight on their own. What gives them weight is that each one names
a symptom traceable to a specific code path identified independently, by reading,
before these reports were seen.

### §11.1 The reports, mapped to mechanism

**1. Injection refuses to activate.**
> *[field, unverified]* *"When I click Activate Injection, I get a pop up that says Could not back up steam.sh… I did a reinstall and it activated."*

**[read]** `slssteam.py:2634` returns the string verbatim:
`{"success": False, "error": "Could not back up steam.sh"}`. The guard above it
(`:2628`) explains the trigger — *"Only back up steam.sh when it's genuinely
pristine"* — so an already-modified `steam.sh` (headcrab's own patch, or a prior
run of theirs) makes the backup refuse and injection fail. This is §3.1's model
failing in the field for §3.1's reason: three agents contending for one file.

**2. Game added, ownership visible, library empty.**
> *[field, unverified]* *"I have Injection enabled and when I go to the Store and add a game then restart, it does not show in my library but if I find it in the store it has the SLS label."*

**[inferred]** The SLS label means the ownership write succeeded — the appid is in
`AdditionalApps`. The library entry missing means the app has no usable appinfo
or the depots were not surfaced. That is a gate 2 / gate 3 outcome (§3.3), the
two gates where their engine's coverage differs most from ours.

**3. Install completes instantly; nothing on disk. ★**
> *[field, unverified]* *"The 'install' shows up alright but when I press the button it's done instantly and the game fails to launch with no executable found… trying to check the files and reinstalling the game shows 0 B on disk."* Logs: manifest downloaded, *"installed lua"*, then *"slssteam: added appid to additional apps"*.

**[read]** This is the failure their own code documents at `slssteam.py:822-834`:
*"a game added while Steam is already running never gets its keys — and the moon
then hands Steam a **zero key** for the AdditionalApps depot, which downloads but
can't decrypt (**empty folder**)"* — or its sibling, an appinfo carrying no depot
list, which Steam reports as zero target depots.

**[read]** It is also, precisely, the trap LumaDeck's cross-component interlock
exists to prevent. `LumaDeck/backend/slssteam_ops.py:38-60` suppresses every live
config poke when lumalinux reports the reconcile is broken, because otherwise
*"poking a just-added game into SLSsteam's live view would surface a game whose
appinfo has no depot list -> it installs 0 files (**'installed but broken'
trap**)"*.

**[inferred]** The symptom their users report is the exact state our §3.4 design
refuses to enter. This is the strongest single validation in the document, and it
lands on the axis (§3.4, A5) that the adversarial pass had already corrected in
our favour.

**4. Broken teardown leaves Steam unlaunchable.**
> *[field, unverified]* *"Steam is no longer launching in desktop mode: 'Could not find the program /home/deck/.local/share/SLSsteam/path/steam'. The file is in the path with a .bak extension added on. I removed the '.bak' and now Steam launches but gives 'missing key' flags."*

**[read]** `slssteam.py:45` defines `BACKUP_SUFFIX = ".bak"` and `:2534` writes
`.bak.<timestamp>` copies. `~/.local/share/SLSsteam/path/steam` is the injection
wrapper itself (`_NATIVE_LIB_DIR`, `:48`) — the launcher had been pointed at it
and then the target was renamed out from under the reference, leaving a dangling
launcher and an unbootable Steam in Desktop mode.

**[read]** The residual *"missing key"* notifications are SLSsteam's own config
error, the one `confighealer.py:8,344` was written to suppress: SLSsteam
*"silently falls back to its own compiled-in defaults"* on an incomplete config.

**[inferred]** Same defect class as §5.6, seen from the other side: their
rename-based enable/disable paths leave references pointing at moved files, with
no automatic repair. §5.6 documents them doing this to *our* artefacts; this
report has them doing it to their own.

### §11.3 A second field report — and it revises §5.8

A longer, more technical public review of the withdrawn version, relayed to this
analysis. **[field, unverified]**, undated, same provenance caveat as §11.1.

#### Its technical points, and their fate

**[read]** Three of its complaints are implemented in `c243cf3`:

| Reviewer's point | State at freeze |
|---|---|
| *"GE proton is bundled with the .zip, but the tooltip states it is not… a 500MB archive isn't fun to download… place it in your github and make a call for it"* | `proton.py:3-11`: *"The ~505 MB build is **NOT bundled** with the plugin. It is obtained on demand"*, from a configurable GitHub location, with a manual fallback |
| *"for the fixes… how about newer game versions that came after the fix, how do you deal with downloading games that work with said fix versions?"* | `pinsource.py:1-8`, a *"layered pin-lua resolver"* that pins to the exact build a fix targets, plus `pin_for_fix` / `pin_for_luatools_fix` RPCs (`main.py:1555,1565`) |
| *"Play button got activated in a second… it did not download game files and there was no button to force the redownload"* | `is_phantom_install`, `provision_depots`, `provision_and_restart` (`main.py:1032-1050`) — the repair path, named after the symptom |

**[inferred]** The review was read and acted on. A substantial part of the
current feature surface is a direct response to it.

**[field, unverified]** Its other observations — inconsistent injection status
across three UI locations, a Denuvo fix list described as *"misleading as there is
no download of HV bypasses made"*, and a factory-reset LegionGo where the first
installed game would not launch — restate §11.1's failures on different hardware.

#### Why this changes §5.8

**[field, unverified]** The same reviewer raised LumaDeck twice, by name and by
repository path:

> *"I'm wondering why you just didn't fork, or PR, **jayool/LumaDeck** instead of
> reinventing the wheel? You got lots of great ideas and I'm sure that if you guys
> worked together we'd end up with a masterpiece."*

> *"**Like LumaDeck**, you should incorporate a web browser so you can fetch the
> API or cookie information the easy way."*

**[read]** The second suggestion describes, precisely, what
`hubcapCapture.ts` / `keyCapture.ts` now do (§5.5) — and the commit that
introduced the auth half of it, `65232aa`, carried a docstring stating it
*"mirrors the useful part of **LumaDeck's** auth model"*, since deleted (§5.8).

**[inferred]** §5.8 concluded that *"the code knows; the person plausibly does
not"*, on the strength of machine authorship. That reading is no longer the more
likely one. A knowledgeable reviewer named LumaDeck to the developer twice, in a
review whose **every other technical point was implemented** — so the review was
demonstrably read. The specific feature suggested by reference to LumaDeck was
then built, and the code that built it cited LumaDeck in a comment that did not
survive.

**What still cannot be established.** **[inferred]** The ordering. The
*"What is lumadeck?"* remark is undated here, and may predate this review
entirely, in which case it was accurate when written. Nothing available dates the
two against each other, and this document does not assert a sequence it cannot
show.

**[inferred]** The honest revision: §5.8's innocent explanation stands as
*possible* and no longer as *probable*. The developer was told about LumaDeck, by
name, in a review he acted on. Whether he had been told before saying he did not
know of it is unresolved and is left unresolved.

### §11.2 What this changes

**[inferred]** Nothing in §6 or §7 — no score was revised on the strength of
unverified reports, and none should be.

What it changes is the standing of the analysis itself. Four independent
symptoms, each tracing to a code path this document had already identified from
source: the `steam.sh` contention model (§3.1), the gate 2/3 coverage difference
(§3.3), the zero-key/no-depot-list trap (§3.3, §3.4), and rename-based teardown
without repair (§5.6). Static analysis predicted the failure modes; the field
reports name them.

**[inferred]** The reverse also holds and is worth stating plainly: **none of
these reports describes a failure our stack would be immune to by luck.** Report
3 is prevented by a designed interlock, report 1 by not touching `steam.sh` at
all, report 4 by not having a rename-based disable path. Reports 2 and 3's gate-2
variant we would *not* survive better — §3.3 records that gap as theirs to our
disadvantage, and §11 does not change it.

---

## §12 Limits of this analysis

Written last, and the section most likely to matter to anyone reusing this
document.

### §12.1 Nothing was executed

**Not one line of either codebase was run.** No install was performed, no game
was added, no Steam client was launched, no hook was observed resolving. Every
finding in §1–§10 is static: reading, counting, and measuring source.

That is a hard boundary, and several conclusions above cross it without saying
so. They are corrected here rather than quietly softened in place.

### §12.2 Process artefacts were used as a proxy for reliability, and they are not one

The specific error: **§3.11 and §4.5 infer engineering quality from the presence
of quality machinery.**

- *"~55 test targets in its Makefile"* — a Makefile target is not a passing test.
  Nothing here ran them.
- *"a cryptographically signed pattern feed"* — a signature proves authenticity,
  not that the locators inside resolve correctly on a device.
- *"six signed build catalogs"* (§10.1) — six published files. Whether any of
  them made a Deck work is not established here.
- *"fail-closed ambiguity handling"* — read in the diff of one commit.

**[inferred]** From these, §3.11 concluded moon is *"a disciplined project"* and
that lumalinux vs moon is *"a fair fight between two serious engineering
projects."* That is a jump from **has the machinery** to **the machinery
delivers**, and this document contains no evidence for the second.

### §12.3 The only outcome evidence points the other way

**[field, unverified]** Everything in this document that touches delivered
behaviour rather than source contradicts the competence inference of §12.2:

- The version this repository replaced was published and **withdrawn** under user
  problems, corroborated in their own code (`diagnostics.py`: *"only made sense
  for a public tool taking bug reports… this build is private"*, §1.2).
- §11's field reports describe injection that would not activate, games that
  installed nothing, and a Steam left unlaunchable — across at least two
  different devices.
- The upstream author of SLSsteam calls moon *"broken features and breaking
  existing ones"* (§4.5), and his one **verifiable** charge — removed safety
  mechanisms — checked out.
- moon's own commit log runs **112 `fix` to 74 `feat`** (§4.5).

**[inferred]** Where static analysis and outcome evidence disagree, outcome
evidence wins. §3.11's characterisation of moon should be read as *"the
repository contains the artefacts of a disciplined process"* — which is
verifiable and true — and **not** as *"the software is reliable"*, which is not
established and which the available evidence disputes.

### §12.4 What this does and does not undermine

Distinguishing the two matters, because discarding the whole document would
discard the half that does not depend on the author's judgement.

**Unaffected — measurements, reproducible from the frozen refs:**

- §5.9's copy analysis: 53 shared sequences, all present in DeckTools, zero
  exclusive above four lines. A computation, not an opinion.
- Counts: LOC, module coupling (275 edges, mean 4.6), 366 vs 106 RPCs, 12
  workflows running zero tests, 20 of 21 `makedirs` without `chown`, zero
  `0o600` in our backend, 1 RVA catalog against their 6.
- Presence facts: the `.ko` from a throwaway account `insmod`-ed as root, eight
  raw `extractall` calls, `_download`'s unused `sha256` parameter, the
  `disable_foreign_engines` rename path with no return.
- §11's mapping of field symptoms onto named code paths — the symptoms are
  reported, the paths are read, the correspondence is checkable.

**Affected — judgement, and to be read at lower confidence:**

- Every 0–10 score in §6.1, and both totals. They mix measured properties with
  assessments of quality that §12.2 shows are weakly grounded.
- §3.11's two-layer conclusion, as qualified in §12.3.
- The A2 revision in §10.2, which moved half a point to them on the strength of
  infrastructure this analysis never saw work.

### §12.5 What would actually settle it

**[inferred]** One instrumented install of each stack on the same device, adding
the same game, across one Steam client update. That single experiment outranks
every inference in §1–§10, and this document is not a substitute for it. The
open question in §8 item 0 — whether Steam reads `config/depotcache` — is the
same kind of thing: not opinable, and unanswered here for the same reason.

