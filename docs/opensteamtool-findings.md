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
- **SLSsteam complements this (not a competing generator):** `feats/achievements.cpp`
  forces **offline stat usage** — it suppresses the server `USERSTATS_RESPONSE`
  (`eresult = ERESULT_NO_CONNECTION`) so the client falls back to the **local** stat
  store that SLScheevo writes. So the two are a pair: SLScheevo generates the schema +
  stats files, SLSsteam makes the client read them. Neither is OST's donor-SteamID wire hook.

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

## Finding 4 — Denuvo / EncryptedAppTicket: SLSsteam already replays; the only gap is donor-capture

### The idea

OST supports **Denuvo** titles by replaying **real captured tickets**: a companion
`tools/extract_tickets` dumps `appticket.bin` + `eticket.bin` from a machine that
*owns* the game; `setappticket` / `seteticket` load them; `Hooks_IPC_ISteamUser.cpp`
serves them over IPC (`RequestEncryptedAppTicket` → a fabricated async
`EncryptedAppTicketResponse`), planted in Steam's registry ticket cache. Denuvo's
ticket window is **~30 minutes**. slsteam-moon has the same (moon "Block D").

### Correction: our stack already has the serving half (SLSsteam `feats/ticket.cpp`)

An earlier draft said "our stack has no ticket/Denuvo code — confirmed." **That was
wrong.** SLSsteam's `src/feats/ticket.cpp` already does the EncryptedAppTicket work:

- Hooks **both** `APPOWNERSHIPTICKET_RESPONSE` and `ENCRYPTED_APPTICKET_RESPONSE`.
- Caches every **successful** (`eresult() == ERESULT_OK`) response to disk at
  `{config}/cache/encryptedTicket_{appid}.yaml` (a `SavedTicket` = steamId + ticket bytes).
- On a **denied** encrypted-ticket response, loads the cached entry
  (`getCachedEncryptedTicket` → `msg->ParseFromString(ticket.ticket)`) and **serves
  the cached ticket to the game**.

So SLSsteam forges ownership AND replays the encrypted app ticket. What it does
**not** do is *import*: the cache is only ever populated from a real successful
server response, so it replays *your own* previously-obtained ticket
(offline / resilience / a brief server denial) — not a ticket for a game you never had.

### Where the actual gap vs OST is — narrow, and it doesn't need OST's machinery

Only the **capture + import** step. OST captures from an *owner* and injects; SLSsteam
has no injection path. But SLSsteam's replay just reads a plain YAML file, so closing
the gap needs **none** of OST's IPC-serving code. It's:
1. capture a valid `EncryptedAppTicket` from an owning machine,
2. write it into `{config}/cache/encryptedTicket_{appid}.yaml` in SLSsteam's
   `SavedTicket` format,
3. let SLSsteam's existing denied-response fallback serve it.

The heavy half (serving over the wire) is already SLSsteam's.

### Verdict — narrow, cheap, more viable than first assessed; hinges on the ticket-window question

- **Not a lumalinux change** and **not the big lift the prior draft implied** — the
  serving exists; only a LumaDeck-side importer (write the ticket into SLSsteam's
  `encryptedTicket_{appid}.yaml` in its `SavedTicket` format) would be new.
- **The capture-from-owner obstacle is largely solved externally:** in practice people
  generate these tickets from **purchased** games and **distribute** them, so a user
  doesn't need their own owning machine — a donor-source ecosystem already exists.
- **The open question that decides the value is the ~30-min window:** if it only gates
  Denuvo's **initial activation handshake** (after which Denuvo issues its own
  longer-lived offline token), a freshly-distributed ticket gets you *past* activation
  and the game then runs — genuinely useful. If Denuvo re-validates the EncryptedAppTicket
  continuously, a distributed ticket is stale within ~30 min and it's near-useless.
  **Confirm this before building** (not verified here).
- **Net:** SLSsteam already covers serving/replay; the only new work is a cheap importer,
  and a distribution ecosystem for the tickets exists. Whether it's worth doing comes
  down to the window-vs-offline-token behaviour — resolve that first.

### If ever pursued: LumaDeck capture tool (dump EncryptedAppTicket from an owner) + a writer into SLSsteam's `{config}/cache/encryptedTicket_{appid}.yaml`. SLSsteam serves it — no lumalinux hook, no OST-style IPC machinery.

---

## Finding 5 — Spacewar-480 redirect (already ours via SLSsteam FakeAppIds) + online-fix is a separate thing

### Correcting an earlier conflation

An earlier draft treated OST's Spacewar-480 redirect and LumaDeck's "online fix"
crack as two versions of the same thing. **They are not** — and the 480 redirect
is **not a gap**: we already have it.

- **OST's Spacewar-480 redirect** (`Hooks_Misc.cpp::BuildSpawnEnvBlock`): changes the
  launched game's primary `CGameID` to AppId **480 (Spacewar)** so unowned copies can
  matchmake over Steam's real lobby system, keeps the real AppId on the overlay CGameID /
  Steam Input, rewrites the reported name, one at a time.
- **SLSsteam already does exactly this** via the **`FakeAppIds`** config option:
  *"Change AppIds of games to enable networking features… keeps track of the proper
  AppIds via game launches, so please do not start multiple FakeAppId enabled games
  simultaneously."* Same mechanism (AppId spoof for networking), same real-AppId
  tracking, same one-at-a-time constraint. **LumaDeck already surfaces it** —
  `backend/slssteam_ops.py::add_fake_app_id(appid, fake_id=480)` (+ remove / check /
  list), defaulting to **480**. So the 480 redirect is **parity**, at the
  config/ownership layer — no lumalinux hook, no message layer for us.
- **LumaDeck's "online fix" is a DIFFERENT feature** (`backend/fixes.py`): it downloads
  the online-fix.me crack DLLs (`OnlineFix64.dll` + `winmm`/`dxgi`/`steam_api64`) from the
  luatools CDN and sets `WINEDLLOVERRIDES` so Proton loads them. That's for games needing
  the crack's **own emulated networking / DRM shim** — the case where the Steam-native
  FakeAppId spoof isn't enough. **Complementary** to FakeAppIds, not an alternative.

### Verdict — 480 redirect: parity (SLSsteam `FakeAppIds`, exposed by LumaDeck). Online-fix: separate, ours, fine.

Nothing to port. OST's 480 trick is already covered by SLSsteam's `FakeAppIds` (surfaced
in LumaDeck at default 480); the online-fix crack is an unrelated, complementary capability
we already have. The prior "not portable, keep LumaDeck's crack" framing was wrong on both
counts — it conflated the two features and missed that `FakeAppIds` *is* the 480 redirect.
(One minor OST detail not verified on the SLSsteam side: keeping the real AppId on the
overlay `CGameID` for overlay/Steam-Input binding — a possible small refinement, not a gap.)

---

## Finding 6 — small niceties & explicitly-not-porting

**Nicety — "purchased" render with a plausible date.** OST sets an unowned app's
`PurchasedTime` to the **`.lua` file's mtime** (`Hooks_SteamUI.cpp::FillInAppOverview`,
`g_purchaseTime`) so the library shows a believable purchase date. **Already covered
by SLSsteam:** the `SubscriptionTimestamps` config option ("Override purchase time
stamps") does exactly this at the ownership layer — no work to do, just populate it if
the "owned since" date ever looks wrong in Game Mode. Cosmetic, and already ours.

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
its **runtime, hash-keyed pattern/IPC database** (Finding 1 — which we explored,
implemented and then **reverted**; see that section). Its Denuvo/EncryptedAppTicket
path (Finding 4) is **not** the gap the first draft called it: SLSsteam already
caches and replays the encrypted app ticket, so only OST's donor-*capture* step is
missing, and the ~30-min window makes it low-value. On the gates we both
cover, we are at parity or ahead (GMRC cascade, shaders, package-0 robustness,
Workshop). Most of OST's remaining cleverness is message/IPC-layer or
Windows-specific and therefore **not portable** to a coexist-with-SLSsteam preload,
the same conclusion reached for slsteam-moon.

---

## Candidate shortlist (ranked)

1. ~~**Runtime steamclient-hash-keyed pattern DB** (Finding 1)~~ — **explored,
   implemented, then REVERTED.** With headcrab's Steam pin, encountering an
   unsupported Steam is rare and SafeMode already fail-closes it; the self-heal
   only saved ~1 release/month for pattern-move breaks, at the cost of publishing
   the byte patterns as fetchable data + a footgun. Kept the **hash** self-heal
   only (SLSsteam-style). Not worth the pattern half.
2. **`.lua` runtime ingestion → drop keys.txt generation** (Finding 2 /
   moon-findings §1) — **rejected**: it moves the parse into the LD_PRELOAD
   critical path (a `.lua` parse bug then breaks key serving) and needs the `.lua`
   persisted, while steamidra_lite still owns manifests/AdditionalApps/acf. The
   `keys.txt` artifact is fine.
3. **Denuvo EncryptedAppTicket donor-import** (Finding 4) — SLSsteam already
   caches+replays the ticket; only a cheap importer (write a distributed ticket into
   SLSsteam's cache yaml) is missing, and a distribution ecosystem for the tickets
   exists. Viability hinges on whether the ~30-min window gates only Denuvo's initial
   activation (→ useful) or continuous play (→ near-useless) — confirm first.
4. Cosmetic: purchase-time from `.lua` mtime (Finding 6) — trivial, only if the
   library date looks wrong.

Everything else: parity, we-lead, or not-portable.

---

## SLSsteam-side audit (added after the fact)

The first draft evaluated lumalinux + LumaDeck from source but leaned on assumptions
for **SLSsteam**, which under-counted what SLSsteam already does and turned several
non-gaps into "gaps." Re-checked against SLSsteam's `src/feats/` + `config_default.hpp`:

- **Corrected:** Finding 4 (EncryptedAppTicket — `feats/ticket.cpp` already caches +
  replays), Finding 5 (Spacewar-480 — the `FakeAppIds` config option, surfaced by
  LumaDeck), Finding 6 purchase-date (the `SubscriptionTimestamps` option).
- **Confirmed accurate:** ownership spoof + `PlayNotOwnedGames` + license injection
  (`feats/apps.cpp`), PICS access-token injection (`AppTokens` → `apps.cpp
  sendPICSInfoRequest`), DLC unlock/spoof (`feats/dlc.cpp`), family-share
  (`DisableFamilyShareLock`).
- **Clarified:** achievements — `feats/achievements.cpp` only forces offline-stat mode;
  it complements SLScheevo (LumaDeck), it does not generate achievements. Finding 3's
  "keep ours" stands.
- **SLSsteam config inventory** (for reference): `DisableFamilyShareLock`, `UseWhitelist`,
  `AutoFilterList`, `AppIds`, `PlayNotOwnedGames`, `AdditionalApps`, `DlcData`,
  `AppTokens`, `FakeOffline`, `FakeAppIds`, `IdleStatus`, `GameTitles`,
  `SubscriptionTimestamps`, `DenuvoGames`, `FakeEmail`, `FakeWalletBalance`,
  `DisableCloud`, `SafeMode`.

Net after audit: once SLSsteam is counted, the only OST capability we don't have is the
EncryptedAppTicket **donor-import** (Finding 4) — SLSsteam already serves/replays, so it's
just a cheap importer whose value hinges on the ~30-min window. The other "gaps" the first
draft listed were already covered by SLSsteam. The big delta was Finding 1's runtime pattern
DB, which we explored and rejected.

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
