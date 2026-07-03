# Findings — OpenSteamTool vs lumalinux + LumaDeck (research notes)

*Investigation date: 2026-07-03. Reference: [`OpenSteam001/OpenSteamTool`](https://github.com/OpenSteam001/OpenSteamTool)
@ `main` (GPL-3.0, C++20/CMake, Windows-only). These are notes for a possible
future port of selected ideas into lumalinux / LumaDeck — nothing here is
implemented yet. Companion to [`slsteam-moon-findings.md`](slsteam-moon-findings.md);
the same coexistence lens applies.*

## Context

OpenSteamTool (OST) is a **proxy DLL injected into `steam.exe`** (masquerades as
`dwmapi.dll` / `xinput1_4.dll`). It runs **in-process inside the Steam client**
and never replaces Steam's downloader: it feeds the client forged ownership,
depot keys, manifest-GID pins and manifest-request-codes so **Steam's own
machinery downloads and decrypts** the game (Model A, `method.md` §2). It is a
clean reverse-engineering of SteamTools and is unusually principled — it mutates
the client's real internal data structures instead of hooking high-level API
surfaces (`BIsSubscribedApp` etc. are deliberately **not** hooked).

Functionally OST is a **superset that spans all three of our components**: its
ownership/PICS/family-share/DLC layer is **SLSsteam's** territory, its
keys/pin/GMRC layer is **lumalinux's**, and its stats/online-fix/cloud/ticket
layer is **LumaDeck's**. So this doc compares OST (whole) against
**SLSsteam + lumalinux + LumaDeck** (whole).

### Architecture constraint (why most OST tricks are not drop-in) — same as moon

OST, like slsteam-moon, is **one in-process thing** and hooks freely at the
**message / wire / IPC layer**: outbound PICS/stats packets (`Hooks_NetPacket.cpp`),
the `IPCProcessMessage` dispatch (`Hooks_IPC*.cpp`), the WebSocket frame path.
Our stack runs **vanilla SLSsteam *plus* lumalinux and deliberately does not modify
SLSsteam**, and SLSsteam already owns the message dispatch (`CProtoBufMsgBase`).
So any OST mechanism implemented as a **network/IPC/message hook is not portable
to lumalinux** — it would collide with SLSsteam (double-wrapping / ordering /
crashes). lumalinux must use distinct **function-layer** seams, exactly as it
already does for DepotKey / BuildDep / GMRC. This rules out a large fraction of
OST's cleverness for us, and the ruling is the same one recorded for moon.

### OST feature inventory (reference)

- **Ownership**: in-memory package-0 `AppIdVec` injection (real `PackageInfo`,
  token `10660652434190618804`) + `MarkLicenseAsChanged`/`ProcessPendingLicenseUpdates`
  re-index, plus a belt-and-suspenders `CheckAppOwnership` override
  (`Hooks_Package.cpp`).
- **PICS**: per-app `access_token` forged on the **outbound** `PICSProductInfoRequest`
  (EMsg 8903, `Hooks_NetPacket.cpp`) to un-strip appinfo.
- **Keys**: `addappid(depot, "hex")` → served by impersonating Steam's local
  ConfigStore `…\DecryptionKey` reads (`Hooks_Decryption.cpp`).
- **Pin**: `setmanifestid` → `BuildDepotDependency` GID rewrite (`Hooks_Manifest.cpp`).
- **GMRC**: intercepts `ContentServerDirectory.GetManifestRequestCode#1`, fetches
  via Lua hooks or **one** HTTP provider (no failover), WinHTTP, UA `OpenSteamTool/1.0`
  (`ManifestClient.cpp`).
- **Stats/achievements**: donor SteamID64 from `stats.opensteamtool.com/<appid>`,
  substituted on outbound `Player.GetUserStats#1` (`StatsClient.cpp` + NetPacket).
- **Tickets/DRM**: SteamStub off-by-four ticket forgery (AppId-7 reuse),
  Denuvo via captured `EncryptedAppTicket` replay over IPC, online-fix via
  Spacewar-480 CGameID redirect, cloud via `cloud_redirect.dll` (`AppTicket.cpp`,
  `Hooks_IPC_ISteamUser.cpp`, `Hooks_Misc.cpp`, `CloudRedirectHost.cpp`).
- **Anti-fragility**: byte-pattern scanning + a **SHA-256-of-steamclient-DLL-keyed,
  crowd-sourced remote pattern & IPC database** (`steam-monitor` repo + jsDelivr),
  so one binary survives Steam updates without a new release (`PatternLoader.cpp`,
  `RemoteToml.cpp`, `IPCLoader.cpp`).

---

## Where our stack already matches or leads (quick pass, so the findings below are the real deltas)

| Capability | OST | Our stack | Verdict |
|---|---|---|---|
| Ownership (package-0 inject + re-index) | `Hooks_Package.cpp` (`MarkLicenseAsChanged`) | SLSsteam `CheckAppOwnership` + lumalinux `package_zero_finder` | **Parity.** OST's `MarkLicenseAsChanged` re-index == the licence-reconcile already flagged in moon-findings §package-0. |
| Depot keys | impersonate ConfigStore `\DecryptionKey` | lumalinux `depot_key_hook` (same `LoadDepotDecryptionKey` accessor) | **Parity** — literally the same seam. |
| Manifest pin | `BuildDepotDependency` GID rewrite | lumalinux `BuildDep` (same function) | **Parity** — same function-layer point, same LumaCore lineage. |
| Manifest request code | **single** provider, **no** failover, **no** cache | lumalinux **3-provider cascade + failover + per-gid cache** (v0.15.8) | **lumalinux AHEAD.** OST regresses to the single-source fragility we already fixed. |
| Shader-cache depot | **nothing** (treated like any depot) | lumalinux `ShaderDepot` + **v0.16 reachability-gated skip** | **lumalinux WAY AHEAD.** OST would hit the keyless "Missing decryption key" loop and the keyed "No connection" popup that we solved. |
| Workshop | **none** | LumaDeck Workshop page | **LumaDeck AHEAD.** |
| Achievements | donor-SteamID runtime substitution | LumaDeck **SLScheevo** offline `UserGameStatsSchema_*.bin` generation | **Parity, different means** (see Finding 3). |
| DLC / family share | implicit (owned package) + shared-lib evict block | SLSsteam DLC unlock + family-share | **Parity via SLSsteam.** |
| Cloud saves | `cloud_redirect.dll` (optional) | CloudRedirect component (optional) | **Parity via a separate component.** |
| Content download | Steam native | Steam native | **Parity** (both Model A). |

The genuinely new material is below.

---

## Finding 1 — runtime, steamclient-hash-keyed pattern database (the big one)

### The idea

OST does **not** ship byte-patterns compiled into the binary. `PatternLoader.cpp`
+ `RemoteToml.cpp` download the patterns (and IPC method specs) at launch, keyed
to the **SHA-256 of the on-disk `steamclient64.dll`**, from a crowd-sourced repo:

```
https://raw.githubusercontent.com/OpenSteam001/steam-monitor/{channel}/{component}/{sha256}.toml
  (jsDelivr mirror fallback: https://cdn.jsdelivr.net/gh/OpenSteam001/steam-monitor@{channel}/…)
```

channels `pattern` and `ipc`; cached under `<Steam>/opensteamtool/…`; stale cache
used on fetch failure; a new DLL hash with no matching TOML degrades gracefully
(logs the missing functions, points the user at the repo). Function names are
FNV-1a hashed. So **a single OST binary tracks Steam client updates without any
new release** — when Steam ships a new `steamclient64.dll`, whoever derives the
new offsets first pushes a `<newsha>.toml` and every OST install picks it up.

### Why this is the most interesting thing OST does for us

lumalinux's **#1 fragility is exactly this**: patterns live in `src/patterns.hpp`
**compiled into `liblumalinux.so`**, so a Steam client update that shifts a
prologue breaks a hook, shows `N/4 hooks — XXX FAILED`, and the fix requires a
**new release + the user re-running `install.sh`** (RESEARCH §maintenance). We
have excellent CI *auto-derivation* (Ghidra) to **produce** new patterns fast,
but the delivery is still "cut a release, user reinstalls." OST decouples the two:
patterns are **data fetched at runtime**, not code in the binary.

Adopting this would mean: when Steam breaks a hook, the user's existing
`liblumalinux.so` **self-heals** on next launch by pulling fresh patterns for the
new `steamclient.so` hash — no release, no reinstall.

### Fit with our architecture — good

- The SafeMode machinery already **hashes `steamclient.so`** (`src/sha256.cpp`,
  `src/update.cpp`) and already **fetches over HTTPS** (`src/curl.cpp`, dlopen'd).
  So both primitives — hash the target module, fetch a keyed resource — already
  exist; this is wiring them to the pattern table, not new infrastructure.
- It's **100% lumalinux's own code**, no SLSsteam overlap (a startup fetch + a
  parse), and no message hook.
- The CI auto-derivation would change from "commit new patterns → release" to
  "publish `<sha>.toml` to a patterns repo/branch"; the derivation work already
  exists.

### Caveats

- **Trust/supply-chain**: runtime-fetched patterns are code-execution-adjacent
  (a wrong/hostile offset = a bad hook = a crash or worse). OST mitigates with
  the SHA-of-DLL key (patterns are scoped to an exact known binary) and jsDelivr
  as a read-only mirror. lumalinux should serve them from its **own** repo over
  HTTPS with the same SHA scoping, and keep the **compiled-in patterns as the
  fallback** (fetch is an *override*, never the only source) so an offline or
  compromised-fetch case still boots with the shipped patterns.
- The SafeMode hash-gate (`res/updates.yaml`) is a *block* list; this is the
  inverse (a *resolve* by hash). They coexist but shouldn't be conflated.

### Verdict — high value, good fit, medium effort. The standout candidate.

This is the one OST idea that attacks lumalinux's single biggest weakness with
primitives we already have. Recommended as the flagship follow-up.

### Implementation sketch (if pursued)

1. At startup, after locating `steamclient.so`, compute its SHA-256 (reuse
   `sha256.cpp`).
2. `Curl::getString` a `…/patterns/{sha256}.hpp.toml` from a lumalinux-owned repo
   (+ a mirror). On success, parse into the in-memory pattern table **overriding**
   the compiled-in defaults; on any failure, keep the compiled-in patterns.
3. Cache the fetched TOML under `~/.cache/lumalinux/patterns/` and use the cache
   when offline.
4. Repoint the Ghidra CI to publish `{sha256}.toml` to that repo on a new Steam
   build, instead of (or in addition to) bumping `patterns.hpp` + releasing.
5. Log which source won (`compiled-in` vs `fetched {sha}`), so a mis-resolve is
   diagnosable from the startup toast/log.

---

## Finding 2 — runtime `.lua` ingestion with hot-reload (reinforces moon-findings §1)

### The idea

Like moon, OST reads the community `.lua` **itself, at runtime**, and serves keys
straight from it — `LuaConfig::ParseDirectory` over `<Steam>/config/lua/` with
**file watchers for hot-reload** (`addappid`, `setmanifestid`, `addtoken`, …).
Add/remove a game and it takes effect **without a Steam restart**
(`NotifyLicenseChanged` reconciles the injected package live).

This is the same finding already recorded against moon (moon-findings Finding 1:
"lumalinux could ingest the `.lua` directly"), now **independently confirmed** by
a second reference implementation — plus one extra angle: **hot-reload**.

### What's new vs the moon writeup

Nothing changes the earlier verdict (real but bounded: reading the `.lua` lets
lumalinux drop the **keys.txt generation** and *maybe* the config.vdf key write;
`steamidra_lite` still owns SLSsteam `AdditionalApps`, the `.acf`, and staging
`.manifest` files into `depotcache/`). OST adds a **UX** data point: its
add-a-game-without-restart flow comes from the live license re-index
(`MarkLicenseAsChanged`), which is the same primitive noted for moon. lumalinux's
`package_zero_finder` already re-injects idempotently on a poll, so a lighter
version (re-read `.lua` on a watch, let the finder pick up the new depots) is
plausible — but the **restart-free add** ultimately needs SLSsteam to re-read its
own `AdditionalApps`, which is SLSsteam's layer, not ours.

### Verdict — same as moon-findings §1 (a simplification, not an elimination), plus a note that "no restart" is mostly an SLSsteam-side property. See that doc for the sketch.

---

## Finding 3 — donor-SteamID achievements vs SLScheevo (parity, keep ours)

### The two mechanisms

- **OST (runtime, message layer):** `StatsClient.cpp` GETs a **donor SteamID64**
  that owns the game from `stats.opensteamtool.com/<appid>`, and `Hooks_NetPacket.cpp`
  rewrites the outbound `Player.GetUserStats#1` / `k_EMsgClientGetUserStats` (818)
  to `req.set_steamid(donor)`, so Valve returns the **donor's** unlocked
  achievements/stats. Live, no files on disk.
- **LumaDeck (offline, file layer):** `backend/achievements.py` runs the
  **SLScheevo** binary to generate `UserGameStatsSchema_<appid>.bin` +
  user-stats files into Steam's `appcache/stats/`. Offline, local files, no live
  substitution.

### Verdict — parity of outcome; OST's mechanism is not portable and not better for us

OST's approach is a **wire hook on the stats request** — the exact message-layer
territory we don't touch (SLSsteam collision), so **not portable**. And it makes
achievements read from a *donor's* progress (all-or-the-donor's), whereas
SLScheevo generates the **schema locally** so the user unlocks their own. For our
stack, **SLScheevo is the right design** (offline, no message hook, user-owned
progress). Nothing to port. Recorded so the donor-SteamID trick isn't
re-evaluated as a gap later — it isn't one.

*(One tiny transferable idea: OST's `stats.opensteamtool.com/<appid>` is a public
"who owns this / donor" oracle. If SLScheevo ever needs a donor SteamID for
schema pull, that endpoint exists — but SLScheevo already solves this.)*

---

## Finding 4 — Denuvo / EncryptedAppTicket replay (a real gap, but SLSsteam-side and heavy)

### The idea

OST supports **Denuvo** titles by replaying **real captured tickets**: a companion
`tools/extract_tickets` dumps `appticket.bin` + `eticket.bin` from a machine that
*owns* the game; `setappticket` / `seteticket` load them; `Hooks_IPC_ISteamUser.cpp`
serves them over IPC (`GetAppOwnershipTicketExtendedData`,
`RequestEncryptedAppTicket` → a fabricated async `EncryptedAppTicketResponse`),
and they're planted in Steam's **own registry ticket cache**
(`SteamCredentialStore.cpp`). Docs note Denuvo's **30-minute** ticket window.

slsteam-moon has the **same** capability (moon-findings "Block D": ticket =
AppOwnership + EncryptedAppTicket cache/replay). So **two** reference tools do
Denuvo via encrypted-app-ticket replay; **our stack does not** (no ticket/Denuvo
code in lumalinux or LumaDeck — confirmed).

### Verdict — genuine gap, but it belongs to SLSsteam, and it's heavy

- **Layer:** encrypted-app-ticket serving is an **ownership/ticket** concern —
  **SLSsteam's** territory (it already forges the AppOwnership ticket), not
  lumalinux's. Porting it means an SLSsteam feature or a LumaDeck-orchestrated
  capture flow, not a lumalinux hook.
- **On Linux the store differs:** OST plants tickets in the **Windows Registry**;
  the Linux equivalent is Steam's on-disk ticket cache / `config.vdf`, which
  `steamidra_lite`/LumaDeck already write to — so a capture-and-replay flow *could*
  live LumaDeck-side, but it requires an **owning machine** to capture from and
  only lasts Denuvo's 30-minute window.
- **Value:** Denuvo titles are a minority and the UX (capture from an owner, 30-min
  window) is poor. Worth **recording as the one true capability gap** OST/moon have
  over us, but low priority and not a lumalinux change.

### If ever pursued: LumaDeck feature (capture tool + `setappticket`-equivalent into `config.vdf`/ticket cache) + SLSsteam serving the encrypted-app-ticket — not a lumalinux hook.

---

## Finding 5 — online-fix via Spacewar-480 redirect (alternative to LumaDeck's crack download)

### The two mechanisms

- **OST (runtime, spawn-env layer):** `Hooks_Misc.cpp::BuildSpawnEnvBlock`
  redirects the launched game's **primary `CGameID` to AppId 480 (Spacewar)**
  (`kOnlineFixAppId`) for lobby matchmaking, keeps the **real AppId on the overlay
  `CGameID`**, re-substitutes the real AppId for Steam Input, and NetPacket rewrites
  the reported game name. Enabled with `-onlinefix` in launch params. One
  online-fix game at a time.
- **LumaDeck (files):** the Fixes tab downloads an **Online Fix** payload (crack
  DLLs) from the CDN (`FIXES_MAP.md`) and applies it, plus a Proton launch-option
  recompute.

### Verdict — different philosophies; OST's is not cleanly portable, LumaDeck's is fine

OST's Spacewar-480 redirect is elegant and **file-free**, but it lives in the
**game-spawn / CGameID** path inside the client and rides NetPacket rewrites — a
runtime hook layer we'd have to add to lumalinux (and part of it is message-layer,
SLSsteam-adjacent). LumaDeck's downloaded-crack approach is coarser but already
works and is decoupled from the client internals. **Keep LumaDeck's**; record
OST's 480-redirect as a *possible* future runtime alternative if the CDN cracks
ever become a maintenance burden — but it's a lumalinux-hook project, not a quick
win. Note also OST's neat detail worth stealing conceptually: **real AppId on the
overlay CGameID** so the overlay/Steam-Input still bind to the right game while
matchmaking runs under 480.

---

## Finding 6 — small niceties & explicitly-not-porting

**Nicety — "purchased" render with a plausible date.** OST sets an unowned app's
`PurchasedTime` to the **`.lua` file's mtime** (`Hooks_SteamUI.cpp::FillInAppOverview`,
`g_purchaseTime`) so the library shows a believable purchase date. LumaDeck/`steamidra_lite`
render the game via the `.acf`; if the "owned since" date ever looks wrong or
empty in Game Mode, this is the trick (set purchase time from the `.lua`/zip mtime).
Cosmetic, cheap, optional.

**Nicety — Lua-scriptable code source.** OST lets a `.lua` define
`fetch_manifest_code_ex(app,depot,gid)` / `fetch_manifest_code(gid)` to override
the HTTP provider. lumalinux's cascade is compiled. Not worth adding (our cascade
already covers the providers), but noted.

**Not porting (Windows-specific or coexistence-blocked):**
- **SteamStub off-by-four ticket forgery** (`AppTicket.cpp`, reuse a signed
  AppId-7 ticket, inject 4 bytes before the 128-byte signature). Clever, but it's
  a **Windows SteamStub/steamdrmp** exploit; under Proton, SteamStub handling is
  LumaDeck's Steamless-via-ACCELA path (actual unpacking). Different problem space.
- **All the NetPacket / IPC / WebSocket-frame hooks** (PICS token, stats steamid,
  GMRC frame rewrite, family-share evict block, cloud RPC) — message/IPC layer =
  **SLSsteam collision**, same ruling as moon. lumalinux stays function-layer.
- **VEH/INT3 + inline-detour hooking, proxy-DLL injection, WinHTTP, registry
  store, PE parsing** — Windows platform mechanics; lumalinux uses libmem +
  `LD_PRELOAD` + dlopen'd libcurl.

---

## Per-gate summary (OST vs our stack)

| Gate (`method.md`) | OST | SLSsteam + lumalinux + LumaDeck | Who leads |
|---|---|---|---|
| 1 Ownership | pkg-0 inject + `CheckAppOwnership` + license re-index | SLSsteam spoof + lumalinux `package_zero_finder` | parity |
| 2 PICS appinfo | outbound access-token forge (wire) | SLSsteam AppTokens (optional) / ownership spoof; **stripped-appinfo robustness still a gap** (see moon-findings §2) | parity (with the known moon-flagged gap) |
| 3 Depot surfacing | pkg-0 `AppIdVec` | lumalinux `package_zero_finder` (poll-based, robust) | **lumalinux** |
| 4 Manifest pin | `BuildDepotDependency` rewrite | lumalinux `BuildDep` | parity |
| 5 Depot key | ConfigStore impersonation | lumalinux `depot_key_hook` | parity |
| 6 Manifest request code | 1 provider, no failover/cache | **cascade + failover + cache** | **lumalinux** |
| (shaders) | none | `ShaderDepot` + v0.16 reachability gate | **lumalinux** |

**Net:** OST is a beautifully-engineered *single-binary* tool whose standout is
its **runtime, hash-keyed pattern/IPC database** (Finding 1) — the one thing we
should seriously want. Its Denuvo/EncryptedAppTicket path (Finding 4) is our one
true capability gap, but it's SLSsteam-side and low-priority. On the gates we both
cover, we are at parity or ahead (GMRC cascade, shaders, package-0 robustness,
Workshop). Most of OST's remaining cleverness is message/IPC-layer or
Windows-specific and therefore **not portable** to a coexist-with-SLSsteam preload,
the same conclusion reached for slsteam-moon.

---

## Candidate shortlist (ranked)

1. **Runtime steamclient-hash-keyed pattern DB** (Finding 1) — high value, good
   fit, reuses `sha256`+`curl`, keep compiled-in as fallback. **Do this.**
2. **`.lua` runtime ingestion → drop keys.txt generation** (Finding 2 /
   moon-findings §1) — simplification, bounded, already scoped.
3. **Denuvo / EncryptedAppTicket capture-and-replay** (Finding 4) — real gap,
   SLSsteam/LumaDeck-side, heavy, low priority.
4. Cosmetic: purchase-time from `.lua` mtime (Finding 6) — trivial, only if the
   library date looks wrong.

Everything else: parity, we-lead, or not-portable.

---

## References

- OST (`OpenSteam001/OpenSteamTool` @ `main`): `src/dllmain.cpp`;
  `src/Hook/{Hooks_Package,Hooks_Decryption,Hooks_Manifest,Hooks_NetPacket,Hooks_Misc,Hooks_SteamUI,Hooks_IPC_ISteamUser}.cpp`;
  `src/Utils/SteamMetadata/{ManifestClient,PatternLoader,RemoteToml,IPCLoader,StatsClient}.cpp`;
  `src/Utils/Config/{Config,LuaConfig}.cpp`; `src/Utils/Tickets/AppTicket.cpp`;
  `src/Utils/CloudRedirect/CloudRedirectHost.cpp`;
  `src/OSTPlatform/Windows/{Http,SteamCredentialStore,Detour}.cpp`;
  `tools/extract_tickets/`; `opensteamtool.example.toml`.
- lumalinux: `src/patterns.{hpp,cpp}`, `src/sha256.cpp`, `src/update.cpp`,
  `src/curl.cpp`, `src/gmrc_store.hpp`, `src/hooks/{depot_key_hook,depot_dependency_hook,gmrc_hook,shader_depot_hook,package_zero_finder}.cpp`,
  `tools/steamidra_lite.py`; `docs/method.md` (the six gates), `docs/RESEARCH.md`,
  `docs/slsteam-moon-findings.md`.
- LumaDeck: `backend/achievements.py` (SLScheevo), Workshop page, CloudRedirect
  component, `FIXES_MAP.md` (Online Fix), `docs/cloud-saves.md`.
