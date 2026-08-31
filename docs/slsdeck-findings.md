# Findings — SLSDeck vs LumaDeck (ecosystem analysis)

*Investigation date: 2026-07-22. Reference:
[`Kaal31/slsdeck`](https://github.com/Kaal31/slsdeck) v0.01 (BSD-3-Clause, ~6
commits, first release 2026-07-22). Read via the public `raw.githubusercontent`
files (the repo is outside this session's GitHub scope, so no API/clone —
`main.py`, `py_modules/lt/{slssteam,downloads,fixes,netsock,apis,denuvo,`
`cloudredirect,ryuu}.py`, `README.md`, `plugin.json`, `defaults/`). Some helpers
(`get_diagnostics`, the full `_run_install`/`_run_headcrab_shimmed` installer,
the pin helpers) were mapped but not read line-by-line — flagged where relevant.*

> **Update 2026-08-02.** LumaDeck now implements **netsock** (per-game
> SteamNetworkingSockets fix) plus a full native online route (FakeAppId 480 +
> netsock, in a dedicated Online Fixes tab) — so the netsock gap this doc flagged
> against SLSDeck (§4, §6.2, §7) is **closed**.
>
> **Update 2026-08-31.** The other open borrow, the gamescope `sessions.d`
> injection (§6.1), is **closed too — and it closed by us moving first, not by
> copying them.** This doc was written when we still injected by patching
> `steam.sh`. We don't: `setup.sh` runs `neutralize_steam_sh`, which restores
> `steam.sh` to vanilla, and injects through three surfaces instead. See §6.1.

This is the **"pelada" (stripped) build** — `plugin.json`: *"add games via
slsteam-moon, apply game fixes (ryuu + perondepot), manifest version pinning.
**No hypervisor/Denuvo**."* A separate "heavy" build (root + a "decky hypervisor"
+ Denuvo-crack download) was announced on r/SteamDeckPirates but is **not** this
repo. This doc analyses only the published pelada.

Unlike moon/SLSsteam (C++ `.so`, analysed in `slsteam-moon-findings.md` /
`slssteam-analysis.md`), **SLSDeck is a Decky plugin** — the same *category* as
LumaDeck. So this compares plugin-to-plugin, and the backend it drives
(slsteam-moon) is the one we already dissected.

---

## 1. What SLSDeck is

A Decky Loader plugin, **a thin frontend for slsteam-moon**. Its own About:
*"This is a frontend for sls-moon for decky loader."* Credits: *"SLSsteam /
slsteam-moon — ownership injection via `LD_AUDIT`."* It is a **Linux port of the
Windows `SLSDeck` Millennium plugin** (which used SteamTools); the port swaps
SteamTools for the Linux stack (SLSsteam/moon + headcrab).

- Stack: Python ~54% + TS ~38% + Shell ~7% — `main.py` + `py_modules/lt/*` +
  `src/` (React/TS). The **standard Decky plugin template** shape.
- Backend package `lt`: `apis, cloudredirect, denuvo, downloads, fixes, netsock,`
  `ryuu, settings, slssteam, steam`.
- Bundled in `defaults/slssteam/`: `headcrab.sh`, `config.default.yaml`,
  `netsock/`.

## 2. Architecture — a thin frontend; moon does the pipeline

Every `Plugin` method in `main.py` delegates to an `lt.X` module. **The plugin
has no download pipeline of its own** — moon (the `.so`) clears gates 3–6.

**Add-game flow** (`downloads.start_add → _add_worker`, verified):
1. If a source (Hubcap/Morrenus) has it, download the manifest `.lua` →
   write `{appid}.lua` into `config/stplug-in/`, extract `addtoken(...)` into
   SLSsteam `AppTokens:`, optionally write DLC into `DlcData:`.
2. **Primary + sufficient path: `slssteam.add_app` → write the AppId under
   `AdditionalApps:`.** Verbatim: *"Steam treats it as owned and fetches depot
   keys itself. The lua/manifest written above is kept as a **fallback**."*
3. No source has it → *"On SteamOS SLSsteam alone is enough"* → `AdditionalApps`
   only.

`add_app` (`slssteam.py:355`) is a **line-targeted, comment-preserving, atomic**
YAML edit: inserts `  - <appid>   # <name>` after the last block item, name
sanitised of CR/LF, idempotent. Exactly as the README claims.

**It installs slsteam-moon, NOT vanilla** (`_resolve_moon_zip_url`, downloads
`slsteam-moon-linux-*.zip` from `swwayps/slsteam-moon` releases, prefers the
non-`lumen` build). The code even states *why*, which **validates our own
analysis**:
> *"Stock AceSLS/SLSsteam only fakes ownership — it has **NO depot-key support**,
> so genuinely unowned games download but stay encrypted. slsteam-moon reads the
> depot decryption keys from `config/stplug-in/<appid>.lua`."*

That is the same gate-5 hole LumaDeck fills with lumalinux's `DepotKey` hook;
SLSDeck fills it by using moon.

**Injection** (`activate_injection`, `slssteam.py:1164`) — the standout: it
**prefers a `gamescope sessions.d` override** (rootless, persistent) that
re-points STEAMCMD at the LD_AUDIT wrapper — *"the method the maintained Linux
port uses; **Steam cannot overwrite it**"* — and only falls back to a `steam.sh`
wrapper (`client.sh` + `chmod 555` + `steam.cfg`) when there's no gamescope
session. **Client downgrade** for "Steam too new" is headcrab shimmed
(`start_client_fix → _run_headcrab_shimmed`), same as ours.

## 3. Features / flows

- **Fixes** (`fixes.py`): **online fixes from the `perondepot` mirror**, matched
  by **game name** (the per-appid index is HTTP-429-rate-limited); plus **Ryuu**
  fixes (`generator.ryuu.lol/fixes`, HTML-scraped to an appid→fixes index,
  bundled snapshot + refresh). Note: *"Unlike the moon port we KEEP
  hypervisor-badged fixes (routed into the Denuvo toggle)"* — a hint it derives
  from a "moon Linux port" and that Ryuu carries Denuvo/hypervisor fixes (applied
  only by the heavy build). Also Generic fix + auto-fix + online-fix username.
- **netsock** (`netsock.py`): applies `yesyes0649/steamnetsock-patch`
  (`netsock.so`) as a per-game launch option — a **SteamNetworkingSockets
  multiplayer fix for FakeAppIds**. LumaDeck has **no equivalent**.
- **CloudRedirect** (`cloudredirect.py`): the **same `Selectively11/CloudRedirect`
  flatpak LumaDeck uses** — `DisableCloud` toggle, Google-Drive saves. Full
  convergence.
- **Denuvo** (`denuvo.py`): **detection-only** for a badge — bundled seed
  (`denuvo_seed.json`) + fetch (looks for "denuvo" in the Steam notice), cached,
  **no false positives** ("unbadged until Steam confirms"). *No bypass* — the
  code says *"this build can't bypass Denuvo."*
- **Manifest sources** (`apis.py`): a **template system** — per-source URL
  templates with `{KEY}` placeholders and per-source keys (Hubcap, **Morrenus**
  [new to us], "free apis").
- **Badges** (8): sls / legit / denuvo / onlineFix / fixed / storePage /
  gamePage / library. LumaDeck has no badge system.
- **Pinning**: moon's **`ManifestPins`** (`appid → {locked, depots:{depot:gid}}`),
  written on fix-apply so an update can't break a version-specific crack.
- **UI**: floating buttons, store injection, gamebar row/panel, library buttons,
  QAM toggle, hide-on-owned.
- **Rootless**; **requires a Steam reload after add** (no no-restart path exposed
  despite moon having a reconcile).

## 4. LumaDeck vs SLSDeck

| Axis | **LumaDeck** | **SLSDeck** |
|---|---|---|
| Runtime backend | SLSsteam **vanilla + lumalinux** (2 `.so`) | **slsteam-moon** (1 `.so`, fork) |
| Gate 5 (depot keys) | lumalinux `DepotKey` hook ← `keys.txt` | moon reads keys from the `.lua` in stplug-in |
| Ownership | SLSsteam `AdditionalApps` | moon `AdditionalApps` (identical) |
| Add data | steamidra **pre-seeds** (keys.txt + manifests→depotcache + config.vdf + AdditionalApps + `.lua`→stplug-in). No `.acf`: it seeded a stub until its issue #41, where the stub was orphaned by an install to another library | writes `AdditionalApps` + `.lua`→stplug-in |
| Injection | wrapper model, three surfaces: patched `.desktop` (Desktop), PATH drop-in (terminals), systemd drop-in on `steam-launcher.service` (Game Mode), plus a `.path`-unit guardian that re-asserts `.desktop` coverage. `steam.sh` is left VANILLA — `neutralize_steam_sh` restores it | **gamescope sessions.d** (preferred, survives Steam updates) → **steam.sh wrapper** fallback |
| Steam-too-new | headcrab downgrade | headcrab shimmed (same) |
| No-restart add | **reconcile** (lumalinux) | ❌ requires Steam reload |
| Robustness to Steam updates | **SafeMode + cron auto-whitelist**; our patterns survived the 2026-07-21 build | depends on **moon** re-adapting its patterns (it had to, 07-22) |
| Cloud saves | CloudRedirect (Selectively11) | CloudRedirect (Selectively11) — same |
| Fixes | Crack/Bypass + Online (our sources) | Ryuu + **perondepot** (by name) |
| DLC | `add_game_dlcs` (≤64 native / >64 config) | same |
| Their extras | — | **badges**, Denuvo detection, template sources |
| Our extras | ShaderDepot skip, Goldberg, Steamless, achievements, self-update, no-restart, **netsock + native online (480+netsock) routing** | — |
| netsock (SNS multiplayer) | **yes** (2026-08-02: Online Fixes tab + native route) | yes |

**Verdict:** LumaDeck is stronger on **product engineering** — robustness to
Steam updates (SafeMode + cron; our patterns held where moon had to re-adapt),
no-restart add, offline pre-seed, and more mature owned features. SLSDeck is
**simpler and offloads the weight to moon**, with two ideas worth borrowing (§6).

## 5. Is SLSDeck a copy of LumaDeck? — No; convergence

The similarity is real on the surface (Decky plugin, same features), but the
evidence points to **independent convergence, not a fork/copy**:

- **Zero traces.** No occurrence of `DeckTools`, `LumaDeck`, `lumalinux`,
  `lopesleo`, `jayool`, or `steamidra` anywhere in SLSDeck's code.
- **Different declared lineage.** SLSDeck is a *"port of the SLSDeck **Millennium**
  plugin"* (Windows/SteamTools); LumaDeck is a *fork of DeckTools*. Different
  ancestors.
- **Different module structure.** Of SLSDeck's 9 and LumaDeck's 24 backend
  modules, only **2 names overlap** (`downloads.py`, `fixes.py`) — the most
  generic names possible. The runtime engines are entirely different (moon vs
  vanilla+lumalinux; `.lua` vs `keys.txt`; gamescope-hook vs steam.sh).

Why they *look* alike: both solve **the same problem with the same ecosystem
pieces** (SLSsteam/moon, CloudRedirect-Selectively11, Ryuu, headcrab, the same
manifest providers) on the **same Decky plugin template**. The "identical"
features (DLC ≤64/>64, `AppTokens`, `AdditionalApps`, pinning) are **the SLSsteam
API**, not copied code — any plugin on SLSsteam lands on that same logic.

**Honest caveat:** re-implementing from reading leaves no trace, so a glance at
LumaDeck can't be *disproven* — but there's no evidence for it, and convergence
fully explains the observations. Best read: cousins on the same
SteamTools/SLSsteam→Decky tree, not parent-child.

## 6. What's borrowable (actionable)

1. ~~**★ gamescope `sessions.d` injection — a fix for our M7.**~~ — **NO LONGER
   APPLICABLE (2026-08-31).** The premise was that we patch `steam.sh` and that a
   Steam update wipes the hook when the file's size changes (M7, moon-findings).
   That stopped being true with the wrapper model: `setup.sh` calls
   `neutralize_steam_sh` to restore `steam.sh` to **vanilla**, and injects through
   a patched `.desktop` (Desktop), a PATH drop-in (terminals) and a systemd
   drop-in on `steam-launcher.service` (Game Mode), with a `.path`-unit guardian
   that re-asserts `.desktop` coverage when Steam regenerates it.

   Re-comparing on today's code, their override buys us nothing: it covers Game
   Mode only, where our systemd drop-in already sits, and their own fallback when
   there is no gamescope session is **the `steam.sh` wrapper we abandoned**. What
   they do have is one surface where we have three plus a guardian — simpler to
   maintain, and covering less.
2. ~~**netsock (`steamnetsock-patch`)**~~ — **DONE (2026-08-02).** Implemented in
   LumaDeck as the native online route (FakeAppId 480 + netsock launch option),
   applied alongside online fixes and via a dedicated Online Fixes tab, with an
   anti-cheat hard stop. See `LumaDeck/FIXES_MAP.md` → "Online multiplayer".
3. **`perondepot`** as an additional **online-fix source** (matched by name), and
   **Morrenus** as an additional manifest provider — both are just endpoints, no
   architecture change.

Nothing here touches lumalinux's own hooks — it's all plugin/headcrab layer.

## 7. Verdict

SLSDeck (pelada) is a **competent but thin frontend for slsteam-moon**: it does
the ownership write (`AdditionalApps`) + `.lua` keys for moon, and leans on moon
for everything hard. It is **not a LumaDeck copy** — convergent ecosystem design.
LumaDeck leads on robustness, no-restart, and maturity. **Both borrows this doc
opened are now closed**: netsock was implemented (2026-08-02), and the gamescope
injection stopped applying when we moved off `steam.sh` to the wrapper model
(2026-08-31) — so nothing here is outstanding. The announced "heavy" build (root
+ hypervisor + Denuvo) is the only place they aim higher, at a real cost in risk
— and it isn't in this repo.

**Where they are genuinely ahead of nobody, and where they aren't.** Their
`add_app` is a careful atomic YAML edit and their Denuvo detection is
conservative (badge only when Steam confirms, no bypass claimed). Their one real
weak point is the add flow: when no manifest source has the game it adds it
anyway on ownership alone and reports `success` with a green "Installed ✓ (no
manifest found)" — a gamble that works for some titles and, when it doesn't,
leaves the user with a game that never appears and no error to read. Their own
reason for shipping moon rather than vanilla SLSsteam is that ownership without
depot keys isn't enough; that fallback path writes no `.lua`, which is where moon
reads the keys from.

## References

- SLSDeck: [`Kaal31/slsdeck`](https://github.com/Kaal31/slsdeck) — `main.py`,
  `py_modules/lt/slssteam.py` (`add_app`, `activate_injection` /
  `_activate_steam_sh_wrapper`, `_resolve_moon_zip_url`, `ManifestPins`,
  `start_client_fix`), `py_modules/lt/downloads.py` (`start_add` / `_add_worker`),
  `fixes.py` (perondepot + ryuu), `netsock.py`, `apis.py`, `denuvo.py`,
  `cloudredirect.py`, `ryuu.py`; `README.md`, `plugin.json`.
- slsteam-moon: see `docs/slsteam-moon-findings.md` (the backend SLSDeck drives).
- SLSsteam vanilla: see `docs/slssteam-analysis.md` (why moon's fork exists —
  gate-5 depot keys).
- LumaDeck: `jayool/lumadeck` (`backend/*`, `tools/steamidra_lite.py`).
