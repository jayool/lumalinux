# SLSDeckUniversal vs the LumaDeck stack — exhaustive analysis

*Phase 1 of 5 (inventory, provenance, trust). Phases 2–4 — the layer-by-layer
install-path comparison, the weighted score and the per-profile verdicts — are
marked **PENDING** in place. Nothing here is a recommendation to change LumaDeck
or lumalinux; the actionable list in §8 is preliminary and deliberately
unimplemented.*

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
   **[inferred]** This is arguably more rigorous than ours: it *verifies*
   readiness against explicit success criteria, where lumalinux's
   `license_reconcile` broadcasts and assumes. Parity at minimum.

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

*§3.2–§3.8 are **PENDING** (Phase 2): ownership/licensing, the six install
gates, the add-game pipeline, post-Steam-update maintenance, failure modes, the
plugin/UX layer and the auxiliary ecosystem. §3.1 is complete because the
evidence surfaced during inventory.*

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

---

## §4 Non-functional axes

### §4.1 Verification and test coverage

**[read]** SLSDeckUniversal: **zero test files** in the repository. The only CI
is `.github/workflows/build-test.yml`. Their CHANGELOG's *"280 assertions across
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

*Tokeer's activation workflow itself is analysed in §5.7 — **PENDING** (Phase 3).*

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

---

## §6 Weighted scoring matrix

**PENDING (Phase 4).** Axes and weights were fixed in advance:
A1 install end-to-end 15%, A2 Steam-update survival 15%, A3 injection 8%,
A4 game updates/pinning 7%, A5 failure modes 10%, B1 game features 12%,
B2 UX 8%, C1 user risk 10%, C2 quality/maintainability 8%, C3 project health 7%.

## §7 Verdicts by user profile

**PENDING (Phase 4).**

---

## §8 Preliminary actionables

*Not implemented. Listed here as findings only; prioritisation belongs to §7.*

**Ours to fix, surfaced by this analysis:**

1. `LumaDeck/backend/downloads.py:956` — `extractall()` on a third-party archive
   as root (§5.3). `steamidra_lite`'s basename-only pattern is the in-house
   precedent.
2. No detection of `*.slsdeck-disabled` artefacts (§5.6). A user returning from
   their plugin is silently broken; we would be the only side offering a return
   path.
3. `setup.sh` verifies no checksums (§5.1). Provenance discipline is not a
   substitute for verification.

**Gaps where they lead:**

4. DLC layers 2 and 3 (§2.6) — the clearest functional gap, and the one that
   does not require adopting any of their risk surface.
5. Direct-download acquisition as a fallback for what the native path cannot do:
   specific older builds, unowned DLC depots (§2.3).

**Explicitly not recommended for adoption:** the prebuilt kernel module (§5.2),
the generic CDP credential interceptor (§5.5), and the `steam.sh` patching model
(§3.1).

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
- **SLSsteam (stock)**: [`slssteam-analysis.md`](slssteam-analysis.md).
- **Ours**: `jayool/LumaDeck` @ `eea30e5`, `jayool/lumalinux` @ `87ee544`.
- **Superseded**: `slsdeck-findings.md` (2026-07-22) — analysed a repository
  that no longer exists (§1.1).
