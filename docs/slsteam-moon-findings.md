# Findings — slsteam-moon vs lumalinux (research notes)

*Investigation date: 2026-06-23. Reference: [`swwayps/slsteam-moon`](https://github.com/swwayps/slsteam-moon)
v2.6 (AGPL-3.0) (the project moved off Codeberg `unplausible/slsteam-moon` to GitHub mid-2026). These are notes for a possible future port of selected ideas
into lumalinux / LumaDeck — nothing here is implemented yet.*

## Context

slsteam-moon is a **fork of SLSsteam** that adds, inside the SLSsteam `.so`
itself: extra protocol handlers, a **Lua manifest importer**, manifest
pinning, an **encrypted-app-ticket / SteamStub (Denuvo) path**, and an updater.
Functionally its install/download core (block "B" below) is a **superset of
lumalinux**: it makes Steam natively download not-owned games on Linux i386,
reading the **same community `.lua` format** (`addappid` / `setManifestid`)
that we consume.

### Architecture constraint (why moon's tricks are not drop-in)

- **moon is one `.so`**: it *is* SLSsteam, so it can hook anything (e.g. the
  protobuf message dispatch `CProtoBufMsgBase`) without coexistence problems.
- **We run two `.so`s**: vanilla SLSsteam **plus** lumalinux, and we
  deliberately **do not modify SLSsteam**. So lumalinux must hook points that
  SLSsteam does **not** touch, or the two collide (double-wrapping, ordering,
  crashes). This is why lumalinux hooks the *local KeyValues accessor*
  `LoadDepotDecryptionKey` rather than the depot-key network message — SLSsteam
  already hooks the message layer.

Everything moon does that we might learn from has to fit **alongside** vanilla
SLSsteam, not inside it.

### slsteam-moon feature inventory (reference)

- **Block A — ownership/licensing**: PlayNotOwnedGames, AdditionalApps, family
  share unlock, FakeAppIds, DLC unlock, AppTokens, FakeOffline, DenuvoGames,
  cosmetic spoofs, SafeMode, control socket. *This is the SLSsteam base we
  already ship in LumaDeck — not moon-specific.*
- **Block B — native install/download core (overlaps lumalinux)**: DepotKey,
  PICS handler, native anonymous CM client, appinfo provision, appinfo.vdf
  splice, packagepatch (package-0 inject), manifestbind (GID rewrite fallback).
- **Block C — manifests/pinning/updates**: manifestid (`setManifestid` pin),
  ManifestStore (purge-proof archive), prewarm, manifestcode (request codes),
  Updater (`updates.yaml`).
- **Block D — Denuvo/DRM**: ticket (AppOwnership + **EncryptedAppTicket**
  cache/replay), steamstub (auto-Steamless before Proton launch).

---

## Finding 1 — lumalinux could ingest the `.lua` directly

### The idea

moon reads the community `.lua` **itself, at startup** and serves keys from it,
instead of a pre-processing step. In moon (`src/feats/depotkey.cpp`):

- `importLuaScripts()` (≈L200) walks `<Steam>/config/stplug-in/*.lua`, regexes
  `addappid(<depot>, <n>, "<64hex>")` (skips commented `--` lines) and loads
  each key into its catalog.
- `provisionManifests()` (≈L261) copies `<Steam>/config/depotcache/*` →
  `<Steam>/depotcache/*`.
- Both run once from `onStartup()` (≈L352), guarded by `g_startupDone`.

Today lumalinux instead relies on **`tools/steamidra_lite.py`** (external
Python) to translate the `.lua` into on-disk artifacts, and then lumalinux only
reads `keys.txt`.

> **Important:** the `.lua` *delivery* is **not** the differentiator. Both
> stacks need something to drop the `.lua` into `stplug-in/` — LumaDeck's
> download flow on our side, a Millennium plugin on moon's side. The
> difference is what each does **after** the file is there.

### Audit — what `steamidra_lite.py` writes today

| # | Artifact written | Code (`tools/steamidra_lite.py`) |
|---|---|---|
| 1 | `.manifest` files → `Steam/depotcache/` **and** `Steam/config/depotcache/` | `extract_zip` / `_write_manifest_both` |
| 2 | Main AppID → `AdditionalApps` in `~/.config/SLSsteam/config.yaml` | `add_ids` (≈L350) |
| 3 | `~/.config/lumalinux/keys.txt` (depot;parent;gid;size;key) | `write_lumalinux_keys` (≈L396) |
| 4 | Depot `DecryptionKeys` → `Steam/config/config.vdf` | `inject_decryption_keys` (≈L511) |
| 5 | *(optional)* AppToken → `AppTokens` in SLSsteam config.yaml | `--token` path |
| 6 | `.acf` error-state patch / clean stub | `_patch_acf_error_state` (≈L581) |
| 7 | *(optional)* ACCELA `.depot` marker | `write_accela_depot_marker` (≈L895) |

### Cross-reference — what lumalinux's runtime hooks already cover

| steamidra_lite output | Covered by a lumalinux hook? | Verdict |
|---|---|---|
| **keys.txt** (#3) | `depot_key_hook` **reads** it (it is the source for the served key) | The *generation* of keys.txt could be replaced by reading the `.lua` directly → **this is the finding** |
| **config.vdf DecryptionKeys** (#4) | `depot_key_hook` serves the key at runtime via the `LoadDepotDecryptionKey` accessor | **Possibly redundant** — if the accessor hook already answers the key query, writing the same key into config.vdf may be unnecessary. *Needs an empirical test: drop the config.vdf write and confirm installs still decrypt.* |
| **AdditionalApps** (#2) | No — that is **SLSsteam's** ownership layer; lumalinux's `load_package_hook` + `package_zero_finder` inject appids into Steam's **package 0** (a different layer: appinfo eligibility, not ownership) | **Still needed** (SLSsteam side) |
| **AppToken** (#5) | No (SLSsteam) | **Still needed** |
| **.acf** (#6) | No lumalinux hook writes `.acf` | **Still needed** on disk (or left to Steam on Install) |
| **manifests in depotcache** (#1) | `gmrc_hook` fetches manifest *request codes*, but Steam still reads the actual `.manifest` **files** from `depotcache/` | **Still needed** on disk |
| **ACCELA marker** (#7) | No | Optional, ecosystem-specific |

### Verdict

- **Real but bounded.** Reading the `.lua` directly would let lumalinux drop the
  **keys.txt generation** (artifact #3) and *possibly* the **config.vdf key
  write** (#4, pending the test above). lumalinux already serves keys at runtime
  (`depot_key_hook`) and already injects appids into package 0
  (`load_package_hook` + `package_zero_finder`), so those parts of the pipeline
  are runtime-covered.
- **steamidra_lite cannot be deleted.** It still owns the artifacts no lumalinux
  hook covers: SLSsteam `AdditionalApps`/`AppTokens` (ownership), the `.acf`,
  and staging the `.manifest` files into `depotcache/`. So this is a
  **simplification** (read `.lua`, drop keys.txt + maybe config.vdf-keys), not
  an elimination of the script.
- **No SLSsteam changes required** — the ingest lives entirely in lumalinux.

### Implementation sketch (if pursued)

1. Add a `.lua` parser to `KeyStore` (or a sibling) that reads
   `<Steam>/config/stplug-in/*.lua` for `addappid(d, n, "hex")` and
   `setManifestid(d, "gid", size)` and builds the same `DepotInfo` lumalinux
   currently loads from `keys.txt`. Skip `--` commented lines.
2. Prefer the `.lua` when present; keep `keys.txt` as a fallback for back-compat.
3. Re-test: depot keys served, package-0 injection, BuildDep, manifests on disk.
4. In LumaDeck, drop the keys.txt generation from the steamidra_lite call (keep
   the SLSsteam/.acf/manifest-staging parts) once verified.

---

## Finding 2 — capture the real depot keys Steam returns

### The idea

moon does **not** only inject keys it already has; it also **records the real
keys Steam hands back** and replays them later. In `src/feats/depotkey.cpp`:

- `recvDepotKey(resp)` (≈L362): on a `CMsgClientGetDepotDecryptionKeyResponse`,
  if Steam returned a key (`eresult` OK) it calls `saveKeyToCache(appId, depot,
  key)` and **persists** it (`<SLSsteam config>/cache/depotkey_<id>.yaml`,
  base64). If Steam later returns an **error** for that depot and a cached key
  exists, moon **fabricates a response** carrying the cached key (≈L393–411).

So moon builds a key library from genuine traffic, as a fallback for depots
that one day succeed and another day fail (revoked / rotated / offline).

### What this would add to lumalinux

- A **self-built key catalog from real traffic**, as a safety net for depots we
  legitimately received once (owned / free / family-shared / a key that later
  rotates).
- It **fits our existing hook**: in `depot_key_hook`, on the *passthrough* path
  where the original `LoadDepotDecryptionKey` returns 32 real bytes, capture and
  persist them. No new hook point, no SLSsteam overlap.

### Caveat

For our **primary** case (not-owned games via Hubcap) the keys come from the
`.lua` anyway, so capture mainly helps **edge cases** (family/shared depots,
rotated keys). Cheap to add, modest payoff — a robustness nicety, not core.

### Implementation sketch (if pursued)

1. In `depot_key_hook`'s passthrough branch, when `g_origFn` returns 32 bytes,
   `KeyStore::Capture(depot_id, key)` → persist (e.g.
   `~/.config/lumalinux/captured_keys/<depot>.bin` or fold into key_store).
2. On a `Lookup` miss, fall back to a captured key before passthrough.
3. Keep it strictly additive (never override a `.lua`/keys.txt entry).

---

## Finding 3 (considered and rejected) — message-layer hook

moon's DepotKey is more resilient to Steam updates because it hooks the
**protobuf message** (`CMsgClientGetDepotDecryptionKey`), whose wire format
barely changes, instead of a byte-pattern-scanned local function.

**Rejected for lumalinux.** To hook that message ourselves we would have to
detour Steam's message dispatch — the **exact point SLSsteam already hooks**
(`CProtoBufMsgBase`). With two `.so`s loaded that is a coexistence hazard
(double-wrapping / ordering / crashes), which is the very reason lumalinux hooks
the local accessor instead. moon gets the message-level approach "for free"
only because it *is* SLSsteam. Recorded here so the trade-off isn't
re-discovered later: **do not** move lumalinux's DepotKey to the message layer
while we run alongside vanilla SLSsteam.

---

# Block B, function 2 — PICS appinfo (gate 2)

*(Findings 1–3 above are about block B's DepotKey. This is a separate function.)*

## What PICS is

Before downloading, Steam asks Valve's CM for an app's **product info** (the
depot list + manifest ids). That list **is** gate 2: without it Steam doesn't
know what to download. For an app the account holds no licence for, the CM
returns a **stripped** buffer — public metadata only, with the
depots/manifests/branches blocks removed.

## Who clears gate 2 (corrected — see the matching fix in RESEARCH §3.2)

**Not the AppToken.** The **ownership spoof (`CheckAppOwnership`) + the package-0
injection** is what makes Steam fetch and treat the appinfo as owned. This is the
LumaCore design, confirmed in moon's `apps.cpp`. The appinfo comes back stripped
when it does because the ownership spoof is **client-side**: Valve's server still
strips based on the account's *real* licences (the content log's `failed to
update ownership ticket (Access Denied)` is that server-side refusal).

The PICS access token (`Apps::sendPICSInfoRequest`) only **un-strips the server
response** for apps already in the request, and is **optional** — the
lumalinux/LumaDeck flow writes **none**. moon attaches it only to apps Steam is
**already** querying, because adding new appids to the outbound request (or
forcing `meta_data_only=false`) makes the client chase product-info buffers it
never asked for and hang at *"Loading user data"*.

## The gap, and moon's plan B

lumalinux **cannot fabricate the depot list**: injecting depots into BuildDep
SIGSEGVs Steam (see §6), so it can only *patch* the GIDs of depots Steam already
surfaced. It therefore **depends on the appinfo carrying the depot list**. If the
CM ever returns a stripped buffer despite the spoof (moon's "AdditionalApp that
isn't in any owned package" case), lumalinux has no depot list and fails.

moon covers this with **anonymous appinfo provisioning**: product info is
**public to an anonymous CM session** (`feats/cmclient.cpp`), so moon opens its
own anonymous session, mines the depots/gids, and splices synthetic entries into
`appinfo.vdf` for the next start (`feats/appinfo_vdf.cpp`, `feats/appinfo_provision.cpp`).

## Portable to lumalinux? The plan B, yes — the hook, no

- **Portable (no SLSsteam overlap):** the anonymous product-info fetch (an
  independent outbound CM/SteamCMD connection) plus the `appinfo.vdf` splice (an
  offline file write Steam reads on boot). Neither is a message hook. This would
  make lumalinux robust to the stripped-appinfo case it currently cannot survive.
- **Not portable:** moon's hook on the PICS *response* message
  (`recvProductInfoResponse`) — that rides the message dispatch SLSsteam already
  hooks; lumalinux must not collide there. It isn't needed anyway: the anonymous
  fetch replaces it.
- **Value:** optional robustness only. lumalinux's current flow works for games
  where the spoof yields a full appinfo (the common case); this earns its keep
  only for games Valve strips even with ownership.

Everything else about gate 1–2 (the token, the request side) is SLSsteam's
territory; lumalinux stays at gates 3–6.

---

# Block B, function 3 — depot surfacing (gate 3, package-0)

Both put the app's depot ids into **package 0** (the implicit "free apps everyone
owns" package) so the depots survive Steam's per-depot **licence filter** —
without it Steam reports "0 target depots" and marks the app installed with 0 B.

## How each does it

- **moon — a `LoadPackage` HOOK** (`feats/packagepatch.cpp`). When Steam loads
  `PackageId 0` (once, very early at boot) it appends the AdditionalApps to
  `pInfo->AppIdVec`. Because that hook can fire before moon's config is ready, it
  also has a manual `injectIntoPackage0`. And it found that **injecting on a cold
  cache hangs the client** (Steam re-requests PICS forever, "Loading user data"),
  which it fixes with a one-shot **licence reconcile** (`reconcileLicensesOnce`
  broadcasts `LicensesUpdated_t` on the local `CUser`).
- **lumalinux — an active FINDER** (`src/hooks/package_zero_finder.cpp`). It does
  **not** rely on the `LoadPackage` hook (diagnostic-only since v0.13.0); a worker
  thread **walks the `CPackageInfoCache` BST and polls forever**, locates
  `PackageId 0`, and injects. Reason: the hook **misses package 0 when it is
  already cached** (loaded before the hook, kept from a previous session, or a
  slow login — the old 5-minute cap broke exactly that). It re-injects
  idempotently if Steam rebuilds the package.

## Verdict — lumalinux is ahead here

| | moon | lumalinux |
|---|---|---|
| Approach | `LoadPackage` hook | **Active finder (polls the cache BST)** |
| Timing | Fragile (early/cold/late) → needs a manual re-inject | **Robust** — catches package 0 whenever it appears |
| Slow login | Can miss the event | Waits for it (polls forever) |

Both discovered the same cold-cache / late-load problem and solved it
differently: moon patches *around* the hook (manual re-inject + reconcile);
lumalinux drops the hook and polls. lumalinux's approach is cleaner and
timing-robust. **Nothing to port for the mechanism itself.**

## Ported: moon's licence reconcile (no-restart Add Game)

> **UPDATE (2026-07-20): lumalinux now DOES a licence reconcile.** The note below
> used to say it didn't and probably didn't need to — that was for the
> *login-time* injection (warm cache, no hang). It turned out to be exactly the
> missing piece for a **different** goal: **no-restart Add Game**. Ported in
> v0.16.15 as `src/license_reconcile.cpp` (see `docs/method.md` §"license
> reconcile" and RESEARCH §"no-restart"). **Promoted to default in v0.16.16**;
> kill-switch `LUMA_NO_RECONCILE` forces the old restart-based behaviour.

The problem it solves: a game added while Steam is *running* injects fine into
package 0 and its keys are served, but Steam still shows **"0 target depots"** /
"Fully Installed, files missing" — because Steam's **appinfo** for the app has no
depot list until it re-fetches it, which only happens on **restart** (observed:
`config changed: added depots …` fires only post-restart). The DepotIdVec angle
was a dead end (tested — it's downstream of appinfo; see RESEARCH). The fix is
the reconcile: **broadcast `LicensesUpdated_t` on the local `CUser`** so Steam
re-reads ownership+appinfo without a restart.

How lumalinux does it (mirrors moon, adapted to our warm-cache/finder model):
- **`CUser::NotifyLicensesUpdated`** resolved by moon's Linux byte-pattern
  (`Patterns::FindNotifyLicensesUpdatedFunction`), **unique-match-or-no-op** — a
  wrong-build pattern degrades to a no-op, never a crash.
- **`CUser*`** captured from the SLSsteam achievement guard (a Steam thread with a
  valid pipe-0 user) — no new hook, no `CSteamEngine::getUser` resolution needed.
- **Trigger**: the re-enabled `keys.txt` watcher reloads the store and *arms* the
  reconcile (it never calls Steam itself); the **package-0 finder** fires it on
  its own thread **after** re-injecting the new depots (correct ordering).
- **No hang** observed — consistent with moon's warm-cache note (the re-request
  hang is a cold-cache/early-inject problem; our finder injects after login).

Verified live 2026-07-20 (v0.16.15): a game added mid-session went
`Reconcile: broadcast LicensesUpdated_t` → Steam preallocated + downloaded +
mounted + **App Running**, all before any restart.

It is reasonably portable (a one-shot callback broadcast, **not** a
message-dispatch hook, so no SLSsteam overlap). Re-derive the pattern via the
RTTI anchor if a Steam update breaks it — see `docs/maintenance.md`.

---

# Block B, function 4 — manifest pinning (gate 4)

Both decide **which exact version (manifest GID)** of each depot Steam fetches —
the one we hold keys/manifests for, not whatever PICS happens to ship.

## How each does it

- **lumalinux — BuildDep** (`src/hooks/depot_dependency_hook.cpp`). Hooks
  `CUserAppManager::BuildDepotDependency`, then **patches** the `ManifestGid`/size
  of the keys.txt depots already in `pDepotInfo`. **Patch-only, never inject**
  (injection crashed in v0.5.4); **only `pDepotInfo`, never `pSharedDepotInfo`**
  (overwriting shared depots heap-corrupted Formula Legends — RESEARCH §11.4).
  This is the **download-plan / function layer**.
- **moon — two pieces.**
  - **manifestid** (`feats/manifestid.cpp`): ingests `setManifestid(depot, gid)`
    from the `.lua` and **rewrites the manifests block of the PICS app buffer**
    inside `PICS::recvProductInfoResponse`. This is the **PICS response / message
    layer**.
  - **manifestbind** (`feats/manifestbind.cpp`): a separate resilience fallback —
    two detours in `CDepotDownloadMgr` (`PrepareDepotDownload` + the leaf path).
    When the public manifest isn't on disk (GMRC providers down) but the zip's is,
    it rewrites the gid so Steam uses the on-disk zip build instead of failing.

## Notes

1. **Coexistence, again.** moon pins in the **PICS response** (message layer) →
   **not portable** to lumalinux (would collide with SLSsteam's message hooks).
   lumalinux pins in **BuildDep** (its own function-layer point) → no overlap.
   Same recurring pattern as DepotKey and PICS.
2. **Shared LumaCore lineage (convergence, not anyone's edge).** lumalinux's
   BuildDep cites *"LumaCore-style … ManifestBind.cpp:55"* — its pin logic derives
   from **LumaCore**, the same source moon follows. Both arrived at the same
   guards (check `result` first, don't touch shared depots, never inject). Neither
   copied the other; both descend from LumaCore.
3. **Default matches.** lumalinux no-pin (`gid=0`) follows Valve / auto-updates;
   moon without `setManifestid` ships `public`. Same philosophy.

## Portable? Only `manifestbind`, and it's low value

`manifestbind` (providers-down → fall back to the on-disk zip manifest) hooks
`CDepotDownloadMgr` functions distinct from BuildDep, so it **wouldn't overlap** —
but lumalinux mostly doesn't need it:

- **Pinned**: lumalinux already pins to the zip's GID, which is **pre-seeded**, so
  no GMRC is needed for that manifest.
- **No-pin**: lumalinux wants the **latest**, so falling back to the stale zip
  manifest would *defeat* the auto-update.

So gate 4 is mostly **confirmation that lumalinux's design is sound** — function
layer, surgical patch-only-primary — with nothing meaningful to port.

# Block B, function 5 — manifest request code / GMRC (gate 6)

The **only** of the six gates Valve validates **server-side**. The request code
can't be forged or derived locally — it has to be fetched from a third-party
service backed by accounts that actually own the content. This is the
load-bearing piece (method.md).

## The source of the code is identical

Both fetch from **`gmrc.wudrm.com`** (the same endpoint SteaMidra uses). moon
additionally falls back to `manifest.steam.run`. On the datum that matters —
where the validated code comes from — there is **nothing to learn**; lumalinux
already does the same thing.

## How each does it

- **lumalinux — function hook** (`src/hooks/gmrc_hook.cpp` + `src/gmrc_store.hpp`).
  Hooks the local `GetManifestRequestCode(this, app, depot, manifest_lo,
  manifest_hi, branch, out_code)` in steamclient. For a manifest of one of our
  depots (gid in keys.txt, or depot in KeyStore) it calls `Gmrc::GetCode(gid)`,
  writes `*out_code`, and returns `1` (success); everything else falls through to
  Steam's owned path. One clean point **SLSsteam does not touch**.
- **moon — message/network layer** (`src/feats/manifestcode.cpp`), three hooks:
  1. `hkBBuildAndAsyncSendFrame` — intercepts the **outgoing WebSocket frame**,
     spots the `ContentServerDirectory.GetManifestRequestCode#1` service call,
     records the `jobid`, and kicks off the async fetch.
  2. `hkRecvPkt` + `hkBRouteMsgToJob` — catches the response by `jobid_target`
     and **rewrites the packet body in-place** to inject the code and set
     `eresult = OK` (two variants because newer Steam routes via
     `CJobMgr::BRouteMsgToJob`).
  3. `hkCDepotDownloadMgr_BYldRequestDepotManifest` — a fallback that
     synchronously stages the manifest blob to disk if pre-staging missed.

## Notes

1. **Coexistence, again.** moon's hooks (`BBuildAndAsyncSendFrame`, `RecvPkt`,
   `BRouteMsgToJob`) are exactly the message/network interception SLSsteam itself
   uses for its dispatch → **not portable**, would collide. lumalinux's
   `GetManifestRequestCode` function hook is a distinct, non-overlapping point.
   Same recurring pattern as DepotKey, PICS and BuildDep.
2. **moon's `BYldRequestDepotManifest` fallback isn't needed by lumalinux.**
   steamidra_lite already pre-bakes the manifests into `depotcache` before Steam
   starts, so there's no runtime blob to stage.

## Portable? One concrete, worthwhile improvement

The **second GMRC endpoint**. Today `gmrc_store.hpp` queries only
`gmrc.wudrm.com`; if it's down, `GetCode()` returns `nullopt` and the download
fails with Access Denied. Adding `manifest.steam.run` as a fallback source is
**100% lumalinux's own code** (no SLSsteam overlap), low effort, and buys real
resilience for the single most load-bearing gate.

> **Action:** add a fallback GMRC endpoint (`manifest.steam.run`) to
> `src/gmrc_store.hpp` — try `gmrc.wudrm.com` first, then the fallback, then
> give up.

Otherwise gate 6 is, like gate 4, **confirmation that lumalinux's design is
sound**: same validated source, the correct non-overlapping hook layer.

---

# Part 2 — swwayps/slsteam-moon changes since 2026-06-23

*Investigation date: 2026-07-05. Reference:
[`swwayps/slsteam-moon`](https://github.com/swwayps/slsteam-moon) `main`
(the GitHub continuation of the Codeberg `unplausible/slsteam-moon` reviewed in
Part 1). Commits 2026-06-23 → 2026-07-05, read as real diffs. Same project as
Part 1 — the SLSsteam `.so` — so these compare against **lumalinux** (and
LumaDeck where the change touches Decky/Deck coexistence).*

The 2026-06-23 Steam client update broke moon's load, and the fallout drove most
of this window: signature self-heal, dropping the hash-whitelist SafeMode for a
crash-recovery fail-safe, a GMRC circuit breaker, appinfo synthesis for
token-locked apps, and Decky coexistence.

## Finding M1 — structural signature self-heal (9f419a5) ★ portable, and NOT our reverted approach

moon made its byte-pattern signatures survive a class of Steam-update drift
**with no network and no new patterns**, two ways:

1. **Mask the volatile layout-constant bytes that are only part of the
   *location* signature.** `CUser::NotifyLicensesUpdated` embeds a `mov
   edi,[eax+0x1bXX]` CUser member offset that drifted `0x1b18 → 0x1b14`;
   `IClientUser::RequiresLegacyCDKey` embeds a `sub eax,<off>` interface→impl
   thunk offset that drifted `0x18d8 → 0x18d4`. moon only needs to **find** these
   functions (it calls them by pointer / hooks them; it never reads the offset
   value), so it **wildcards** those bytes. The long surrounding tail keeps the
   match unique — verified exactly 1 hit in both the pre- and post-update
   `steamclient.so`. Result: that constant-only drift no longer needs a code
   change.
2. **Structurally re-derive the `IClient*::RunIPCFrame` dispatch-tree root at load
   time from the live binary** (`autoResolveIpcFrameRoots` + `feats/ipcframe.hpp`).
   It scans the executable segments *inside steamclient's address range*, collects
   candidate roots, and accepts one **only if exactly one lies within a tight band
   (`kMaxRootDrift`) of the stored seed**; otherwise it keeps the embedded value,
   so an uncertain match **degrades to the prior loud failure rather than binding
   the wrong function**. `Pattern_t::pattern` was made non-`const` so the trailing
   root byte can be rewritten in place before `find()`.

**Why this matters for lumalinux.** This is **not** the thing we reverted. We
reverted a runtime **pattern-override fetched over the network** (a supply-chain
surface, made redundant by the headcrab pin + the cron). This is **self-contained
structural self-heal**: no server, fail-safe (uncertain → loud fail, never
mis-hook). And technique **#1 is directly portable to `src/patterns.hpp`** —
lumalinux uses the same libmem byte-scan, and several of our signatures surely
embed layout-constant bytes we only need for *location*. Masking those would turn
more Steam updates into **CLEAN hash-bumps** (the cron just appends the hash)
instead of **ShaderDepot-moved re-derives** (which need a human + a release). It
directly reduces how often `check_patterns.py` returns "pattern moved". The
RunIPCFrame structural re-derive (#2) is heavier and lumalinux may not need it,
but the **byte-masking discipline is a cheap, high-value hardening** for our
pattern set — audit which of our sig bytes are load-bearing vs merely-present.

## Finding M2 — moon DROPPED its hash-whitelist SafeMode (643a92c) — our incident, their opposite call

moon **force-disabled** SafeMode (`safeMode = false;`, the key still parsed so old
configs don't error). Its stated reasons are, verbatim, our production incident:
*"its hash whitelist cannot be kept current: it goes stale on every Steam client
update and would disable an otherwise-working client, and it is sourced from an
upstream mirror we do not control."* They replaced the guard with the Steam
wrapper's crash-loop fail-safe (M3).

**Contrast with lumalinux — do NOT copy the removal, but note the trade-off.**
This is the exact failure mode that SafeMode-blocked the user's Deck. But moon's
SafeMode and lumalinux's are not the same instrument:
- moon's only *disabled SLSsteam* on an unknown hash — losing it costs little
  because its other resilience (self-heal, crash fail-safe) covers the brick case.
- lumalinux's SafeMode is **fail-closed to prevent mis-hooking a changed binary**
  (a wrong hook can corrupt), and we've **already mitigated the staleness** moon
  complains about: the cron **auto-whitelists CLEAN hashes** and the whitelist is
  **our own repo, not a mirror**. So the two big objections (stale, not-yours)
  are largely answered on our side.
- The residual lesson: moon pairs "don't hard-block" with a **crash-recovery
  belt** (M3). lumalinux is fail-closed with a *fast auto-un-block* (cron) rather
  than fail-open with recovery. Both are defensible; ours keeps the anti-mis-hook
  guarantee. Worth recording that the whole ecosystem hit this and split on it.

## Finding M3 — crash-loop fail-safe with a client-change fast-path (cdfd116, a9cf037, b103474, fc9d839)

moon's replacement for SafeMode lives in the **Steam wrapper** (`setup.sh`), not
the `.so`. It records `steamclient.so` identity (`size:mtime`) across boots and,
on a **startup crash** — keyed off an actual **crash dump**, not a short session,
so Game-Mode↔Desktop switches / self-update restarts / manual quits don't count,
and non-fatal assert dumps are ignored — it latches "stay vanilla" recovery. The
smart bit: if the crashed client **differs from the last cleanly-booted one**, a
fresh update is the near-certain cause, so it latches on the **first** crash
instead of making the user loop `MAX_FAILS` times; a crash on an **unchanged**
client keeps the conservative threshold. Auto-clears once the payload is updated.

**Relevance.** This is the fail-*open*-with-recovery end of the spectrum lumalinux
deliberately isn't at — but the **client-change signal** (`size:mtime` of the
32-bit `steamclient.so`) is a cheap, useful primitive. If LumaDeck ever wants a
"SLSsteam/lumalinux just started crash-looping after a Steam update" detector, this
is the pattern: key off a real crash **and** a client-fingerprint change, not a
static gate. Note it dovetails with our existing `check_slssteam_hash_status`
(which reads the SLSsteam log for the abort line) — complementary signals.

**M2 and M3 are one trade — and we are on the other side of it.** M3 is the
*recovery* half of what M2 *removed*: moon dropped the preemptive hash gate (M2)
and, because it now hooks an unverified binary that can crash Steam, added M3 to
recover from that crash. lumalinux **kept** the gate, so the crash-loop brick M3
rescues from **essentially cannot happen to us** — an unrecognised Steam build
makes us **block cleanly** (or SLSsteam self-aborts with "Unknown steamclient.so
hash! Aborting…"), never crash-loop. Adopting M3 on top of M2 would be
belt-and-suspenders where the belt already prevents the exact fall.

**Decision (2026-07-05): context, not an issue.** No lumalinux work. The only
residual gap M3 would cover — a crash from a *whitelisted* build with a buggy hook
— is rare (caught before whitelisting) and not the failure we actually hit. The
sole borrowable slice is a **LumaDeck-side UX nicety**: feed "client fingerprint
changed since last known-good + a real failure signal (SLSsteam abort line /
crash)" into the compatibility nudge it already shows (`headcrab_compat`), to
sharpen the message and avoid false alarms on benign short sessions. Optional,
low priority; not filed.

## Finding M4 — GMRC network circuit breaker + local-store-first (516eeb4, d263ba4, b3a9de7) ★ portable to gmrc_store

Two changes to the load-bearing manifest-code path:
1. **Local ManifestStore before the network** (`pics.cpp`): during PICS staging,
   `ManifestStore::restoreToDepotcache` is tried first; only on a miss does it
   `submitManifestBlob` (the HTTP fetch). ManifestStore is a **persistent archive
   that survives Steam's depotcache purges**.
2. **A network circuit breaker** in `ManifestFetch::runOnce`: when a round trips
   all providers with only **network/5xx** errors it sets `g_providersOffline`;
   subsequent calls **return `nullopt` immediately without any HTTP**, so a dead
   provider never blocks the **main Steam UI thread**. A **detached background
   thread** re-probes the chain every **10 min** and resets the breaker when a
   provider answers `<500`. Provider chain is `gmrc.wudrm.com` + `manifest.steam.run`
   — the exact fallback the Part-1 gate-6 finding told us to add.

**Relevance to lumalinux — real.** Our `gmrc_hook` calls `Gmrc::GetCode` on the
**hot path** (`GetManifestRequestCode`); if the provider is down, repeated calls
can stall the client. Part 1 already flagged "add the second endpoint"; **M4 adds
the other half — a circuit breaker so a provider outage degrades to a clean
Access-Denied instead of a UI hang**, plus a background re-probe. Both are 100%
lumalinux's own code (no SLSsteam overlap). Upgrade the Part-1 gate-6 action to:
*second endpoint **+** breaker + periodic re-probe.*

## Finding M5 — appinfo depot/launch synthesis for token-locked apps (998ff6f)

Part 1's gate-2 finding was moon's **anonymous appinfo provisioning** for apps
whose PICS buffer comes back stripped. M5 extends it to the case where **even the
anonymous fetch (and the steamcmd fallback) return no depots** — some titles gate
product-info behind an app access token Valve denies anonymous sessions (their
example: Risk of Rain 2, 632360), which showed **0 B** and failed launch with
*"invalid configuration"*. moon now **reconstructs the `depots` block from data it
already holds on disk** — `DepotKey::managedDepotsForApp` × `ManifestStore`'s best
archived gid — deriving `oslist`/size from the parsed manifest, and synthesizes an
`installdir` and per-OS `launch` entries. It **marks the app synthetic** so the
outgoing-PICS hook strips it from Steam's product-info refresh (else Steam's
`RequestAppInfoUpdate` returns an empty buffer and clobbers the synthesized depots
mid-session → back to 0 B).

**Relevance.** This closes exactly the hole Part 1 said lumalinux **cannot**
survive (it can't fabricate a depot list — BuildDep inject SIGSEGVs). But the same
Part-1 caveat holds: moon does it by **owning `appinfo.vdf` + hooking the PICS
response**, which is SLSsteam's territory. For lumalinux it's portable **only** as
the offline `appinfo.vdf` synthesis + a "don't refresh this app" marker, **not** as
a message hook.

**Correction (2026-07-05, verified against vanilla SLSsteam source
`AceSLS/SLSsteam@ebfb079`).** An earlier draft said vanilla SLSsteam "has nothing"
for token-locked apps — that was wrong. SLSsteam has a real mechanism, and OUR
stack already wires it:
- `Apps::sendPICSInfoRequest` (`src/feats/apps.cpp:230`) attaches a
  **user-supplied access token** (`g_config.appTokens`, an `AppTokens: {appid:
  token}` map in config.yaml) to the outgoing PICS request. A valid token makes
  Valve return the FULL appinfo — depots present, install works. That is its whole
  appinfo handling; it has **no** depot synthesis and **no** anti-clobber (the
  `meta_data_only` / `depot_id` hits in the tree are auto-generated protobuf
  boilerplate, not SLSsteam logic).
- **lumalinux/LumaDeck already plumb `AppTokens`**: `steamidra_lite --token` and
  LumaDeck `add_game_token` write exactly that map (Part-1 audit artifact #5). So a
  token-gated app whose token the source (Hubcap/community) can provide is
  **already handled** end-to-end on our side — no synthesis needed.

So M5 is much narrower than first stated. The two cases split:
1. **Token gated, token obtainable** → **already covered** by our existing
   `--token` / `add_game_token` → SLSsteam `AppTokens` path. Nothing to do.
2. **Token gated, NO token obtainable anywhere** → the only residual, and the only
   thing moon's synthesis buys. That is the non-portable half (fabricating depots +
   blocking the refresh clobber lives in SLSsteam's message layer).

**Verdict:** not an implementation issue. Token-gated apps use the existing
`--token` flow; the no-token-obtainable sub-case is moon-synthesis territory we
can't do cleanly (SLSsteam-side). At most a doc/UX note so LumaDeck can point a
"downloads but shows 0 B / invalid config" title at the token flow (or a fix), same
family as the Denuvo-ticket path (LumaDeck #27).

## Finding M6 — Decky coexistence: keep CEF debug port on 8080 (a880c9c, ee99e74) ★ LumaDeck-relevant

Lumen (moon's sidecar) normally **moves Steam's CEF remote-debug port off 8080**
to an ephemeral one. But **Decky Loader's injector is hard-coded to
`localhost:8080`** and runs as a persistent root daemon in both Desktop and Game
Mode, so it can't follow. moon now **detects Decky** (`$HOME/homebrew/services/PluginLoader`)
and, when present, **leaves CEF on 8080 and deletes any stale port contract** so
Lumen falls back to 8080 too — multiple CDP clients coexist on one CEF target
(verified on Bazzite), so Decky and Lumen share it.

**Relevance.** The user's stack **is** Decky (LumaDeck). Older moon would have
**stolen 8080 and broken Decky/LumaDeck** on a box running both; current moon is
now Decky-aware and defends it. Good to know: **moon + LumaDeck can now coexist on
one Deck** without a CEF-port fight. If we ever add our own CDP client, the same
rule applies — never move Steam off 8080 when `~/homebrew/services/PluginLoader`
exists.

## Finding M7 — the steam.sh shim dead-end, again (d829fb2 → reverted dc89501)

moon tried "cover every Steam launch path via a `steam.sh` shim", then **reverted
it** — same reason luatools hit (Part-2-luatools L6): **Steam re-extracts
`steam.sh` when its size differs from the manifest**, so a shim there doesn't
survive. moon's surviving approach is **desktop-entry coverage**: scan & patch all
`steam*.desktop` entries, rewrite the `Exec` token to the wrapper, keep the entry
a `0644` regular file with a backup, re-assert coverage from the wrapper on each
launch, skip the *system* entry on immutable distros, and restore everything on
uninstall (`a9ea362`, `e7b3070`, `0ed3037`, `69c2190`, `414e048`, `6a577aa`,
`c799210`, `a913fc7`, `f51ec13`, `d1720d9`). **Two independent tools converged on
the same "don't inject at `steam.sh`, patch the `.desktop` Exec" conclusion** —
relevant to however headcrab/LumaDeck ensures the LD_PRELOAD wrapper covers all of
Game Mode, Desktop, and the SteamOS launcher drop-in.

**This one touches our stack directly (verified 2026-07-05).** Our injection lives
in `steam.sh`: lumalinux is `LD_PRELOAD`ed *by* `steam.sh` (`src/main.cpp:58`,
`src/sha256.cpp:5`), and headcrab patches an `LD_PRELOAD=liblumalinux.so` block
into it. LumaDeck already sees the failure symptom — `backend/paths.py` has an
`injection_missing` state ("steam.sh lost the lumalinux block → reinstall") — but
attributes it to *headcrab regenerating `steam.sh`*. M7 adds a **second cause for
the same symptom**: Steam itself **re-extracts `steam.sh` when its size differs
from the manifest** (i.e. after our block changed its size), which wipes the
injection on a Steam update with no headcrab run involved. So we are on exactly
the fragile method moon and luatools both abandoned.

- **Not a lumalinux change.** lumalinux is only the payload `steam.sh` preloads;
  it doesn't own the wrapper. The durable fix (patch the `.desktop` `Exec` instead
  of `steam.sh`) is **headcrab's** layer — upstream, not ours.
- **What is ours (LumaDeck):** the detect-and-nudge already exists; M7's value is
  (a) documenting the *second* cause of `injection_missing` (Steam re-extract, not
  only headcrab rewrite) so the diagnosis is complete, and (b) an upstream note to
  headcrab that `.desktop Exec` patching survives Steam updates where a `steam.sh`
  block does not. Filed as a LumaDeck doc/issue candidate, not lumalinux work.

## Finding M8 — config hardening (013b5a9, c90bd47, 131c4e8, 22c1393)

Parse scalars and manifest pins **without throwing**, contain out-of-range scalar
parse errors, clarify the startup-disabled notice. Ordinary robustness — the kind
of "a malformed config line must never abort the load" hygiene lumalinux's
`update.cpp` / config reader should also hold. No action, just parity to keep in
mind.

## Part 2 verdict

The two worth acting on for lumalinux:
- **M1 byte-masking** — audit `src/patterns.hpp` and wildcard the layout-constant
  bytes we only need for *location*, so more Steam updates stay CLEAN hash-bumps
  instead of pattern-moved re-derives. Cheap, directly cuts release churn.
- **M4 circuit breaker** — extend the Part-1 gate-6 action from "add
  `manifest.steam.run`" to "add the fallback **and** a circuit breaker + 10-min
  re-probe" so a GMRC outage degrades cleanly instead of stalling the client.

Context to keep, not port: **M2/M3** (the ecosystem split on fail-closed-whitelist
vs fail-open-recovery — ours is defensible and already mitigates moon's
objections), **M5** (token-locked-app appinfo synthesis — solved in moon, a
`steamidra_lite`-side offline job for us if ever needed), **M6** (moon is now
Decky-aware; moon + LumaDeck can share 8080), **M7** (don't shim `steam.sh`).

---

## Delta — 2026-07-10 (commits since Part 2)

Weekly pass over `swwayps/slsteam-moon` since Part 2's 2026-07-05 cutoff. Five
substantive commits, all refining findings already here; two of them surfaced
latent crash risks on our side.

- **`53ab675` — alt-GID fallback gated on the circuit breaker** (refines M4).
  moon only substitutes a stale local manifest when the fetch circuit breaker
  reports providers offline; if online it lets Steam pull the real request-code.
  Design confirmation for M4; not portable (we pin via BuildDep, never substitute).
- **`362476d` — size-0 depot crash + per-GID 404 fallback** (refines M4). Drops
  managed depots with size 0 in `hkBuildDepot` because Steam asserts/crashes on
  degenerate manifests, and makes the breaker per-GID (404) instead of global.
  For us: BuildDep passes through on no-pin (gid=size=0) so we never inject a
  size-0 entry, but "a size-0 managed depot crashes Steam" is worth remembering.
- **`b1da6b6` — drop the appinfo build-pin** (refines M5). moon reverted pinning
  gid+buildid into appinfo: it made metadata lag the live build, stalled the
  update job, hung the client at startup, and contaminated the download planner
  (active==target==pin gives zero delta, so real content never downloads). It now
  pins at the reconcile instead. Confirms our side is safer: we pin ManifestGid in
  `pDepotInfo` via BuildDep, not in appinfo.
- **`bbae0fc` — autofix script** (parallels M3/M7). A `curl | bash` that reinstalls
  the release and repairs every steam `.desktop` when moon detects it did not
  inject, served from the raw branch so fixes ship without a release. Our
  equivalent is LumaDeck's "Install / Reapply lumalinux" + `install.sh`; parity.
- **`30605f4` — config.yaml self-heal + exception-landing-pad pin** surfaced the
  two risks below.

### Two crash risks it surfaced for us

1. **Exception landing pad in `.cold` (lumalinux) — ACTIONED.** A malformed
   config.yaml made moon's yaml-cpp throw, and at -O2+ block partitioning stranded
   the landing pad in `.cold` so even `catch(...)` was bypassed and the client
   aborted; moon pinned the pads with `-fno-reorder-blocks-and-partition`.
   lumalinux is -O3 and parses `updates.yaml` with `YAML::Load` inside try/catch
   (`src/update.cpp`), the same structural setup. Probability is low (updates.yaml
   is our CI-checked file; only a truncated download/cache would throw, and we
   build without LTO so the miscompile may not even fire), but the failure mode is
   a startup abort, i.e. the OOBE class. **Fix: added
   `-fno-reorder-blocks-and-partition` to the GCC build in v0.16.9** (layout-only
   flag, zero behaviour change).
2. **config.yaml writer indentation (LumaDeck) — NOT actioned.** moon's abort came
   from a writer (LuaTools) leaving inconsistent list indentation under
   `AdditionalApps`. Our two writers (`steamidra_lite` `  - {n}`, `slssteam_ops`
   `  - {appid}`) both use a consistent 2-space indent, so we do not self-inflict
   it. The only trigger is a file already mixed from outside (manual edit / another
   tool), and whether the real AceSLS SLSsteam aborts on it is unknown and not ours
   to fix. Logged as known, low relevance, not self-caused.

---

## Delta — 2026-07-22 (v2.8 + the 2026-07-21 Steam client update)

Pass over `swwayps/slsteam-moon` since the 2026-07-10 Delta. Read via the public
compare `.patch` (`30605f4…v2.8`) plus the two commits past the `v2.8` tag on the
default branch (`slsteam-moon`) — ~14 commits, 2026-07-14 → 2026-07-22. Grouped by
theme; the two that touch **lumalinux** are flagged ★, and the Steam-client
pattern drift (D8) is the operational signal to act on.

### D1 — GMRC "suppress work when providers offline" (`9abd0de`, 07-17) ★ upgrades M4

moon now gates its **update checks** on the fetch circuit breaker
(`ManifestFetch::areProvidersOffline()`): when a round trips all providers with
only network/5xx errors it sets a global-offline flag and later calls return
immediately without any HTTP; a detached thread re-probes every ~10 min. Pairs
with the earlier per-GID 404 tracking (`362476d`) and alt-GID-gated fallback
(`53ab675`) from the 07-10 Delta.

**For lumalinux — this is the missing half of M4.** Verified 2026-07-22:
`src/gmrc_store.hpp` already has the 3-provider cascade **with
`manifest.steam.run` present** (the Part-1 gate-6 "add the endpoint" action is
DONE) and per-call timeouts (15s/30s, first-good-answer short-circuit), but **no
breaker**: every `GetCode` re-tries the whole cascade, so a dead provider can
stall the hot path (`GetManifestRequestCode`) once per manifest. Action (upgraded
from the Part-1 gate-6 note): add a **circuit breaker** (all-providers-network-fail
→ fast `nullopt`, no HTTP) **+ a periodic re-probe**. 100% our own code, no
SLSsteam overlap. Clearest single port from v2.8.

### D2 — drop DLC without usable keys (`a72deb9`, 07-16) ★ verify on Deck

moon filters `extended.listofdlc` in the provisioned appinfo to **drop DLC whose
content depots have no usable key**, so Steam never schedules encrypted content it
will reject (`appinfo_provision.cpp`, new `dlcids.hpp`).

**For lumalinux/LumaDeck — the one case that may hit us.** We DO advertise DLC:
`add_game_dlcs` (LumaDeck `slssteam_ops.py`) pulls the DLC list from the Store API
and unlocks it (≤64 handled natively by SLSsteam, no config; >64 written to
`DlcData:`), **without filtering by key**. `steamidra_lite` already *identifies*
`dlcs_no_key` (L1407) but only for reporting. Likely structural protection: a
depot only downloads if its key is in `keys.txt` and it is surfaced, so a keyless
DLC usually never downloads. **Not traced end-to-end** — unverified whether a DLC
that SLSsteam marks owned but whose depot lacks a key can still trigger a
`missing decryption key` / 0-byte state. **Action: test on Deck** (a game with a
content-DLC we ship no key for); if it misbehaves, add our own equivalent — don't
advertise a DLC whose depot has no key.

### D3 — appinfo-synthesis hardening (`0ce1910`, `803cd75`, `a25e1d1` = v2.8)

Three commits tighten moon's appinfo synthesis (M5): don't synthesize an app
without at least one **installable content depot** (`hasUsableContentDepot`),
require that depot carry a **concrete manifest gid ≠ 0**, and **strip synthetic
apps from the PICS changelist** so Steam's refresh doesn't re-query and clobber
them mid-session (`usabledepot.hpp` new, `pics.cpp`, `CProtoBufMsgBase.hpp`).

**Not portable — M5 territory.** lumalinux cannot synthesize appinfo (injecting
depots SIGSEGVs; it only patches depots Steam already surfaced), and the synthesis
+ changelist filter live in SLSsteam's message/appinfo layer. Our path for
stripped apps stays the `--token`/`AppTokens` flow. No action.

### D4 — new feature: local parental-controls unlock (`c34fcea`, `e1cc9f3`, 07-16/18)

New Block-A spoof: rewrite the parental-settings protobuf to strip Family-View
restriction flags before Steam applies them; plus lifecycle hardening (mutex,
invalid-mask guard). Brings **new byte patterns** for the settings receiver.

**Not ours.** A client unlock = SLSsteam layer; vanilla SLSsteam doesn't ship it;
the protobuf-rewrite point is the message layer lumalinux must not collide with.
lumalinux touches nothing parental (verified). Product note only: a niche feature
moon offers and LumaDeck doesn't.

### D5 — manifest staging redesign: on-demand + BoundedExecutor (`bdcf1fa` + bound-staging, 07-16)

moon moved manifest retrieval **off the PICS callback to on-demand** (triggered by
the install planner), **disabled periodic prewarm by default**, added a
`ManifestSelection` policy (exact / preferred-local / legacy, timeout-budgeted),
and a `BoundedExecutor` thread pool so mass staging can't exhaust Steam's thread
pool.

**Not applicable — runtime vs offline model.** lumalinux has no runtime manifest
staging or prewarm to bound: `steamidra_lite` pre-seeds the zip's `.manifest` files
into `depotcache/` **before** Steam starts (sequential Python); the only hot-path
manifest work is a single `GetCode` per manifest. Verified: no prewarm/stage/
selection concept in `src/`. Nothing to port.

### D6 — desktop-coverage refinement: backups out of XDG paths (07-14)

Moves `.desktop` backups from an adjacent suffix to a central mirror dir so KDE's
XDG autostart generator doesn't treat a backup as a second Steam launcher. Refines
M7 (moon patches `.desktop`, not `steam.sh`).

**Not ours.** We inject via `steam.sh` (headcrab's layer), touch no `.desktop`; our
`.bak` files are config (`config.vdf/.yaml/.acf/.lua/keys.txt`) in config dirs,
never in autostart paths, so the failure can't occur. headcrab note only.

### D7 — notify: "re-add", not "restart" (`069c031`, 07-21) — convergence

moon changed its incomplete-install error message from "restart Steam" to "re-add
the game via LuaTools" (restarting doesn't fix half-written install data).
**Convergence with our own change**: we just dropped "Restart Steam to see the
game" → **"Done!"** on `doneRestartSteam` (the no-restart reconcile flow). We have
no dedicated "incomplete install → re-add" message; worth one if we ever detect
that state (0-byte / invalid config).

### D8 — patterns adapted to the 2026-07-21 Steam client (`9d4380f`, 07-22) ★ Steam-update signal

**A Steam client update landed 2026-07-21 and moved locators.** moon re-adapted
(`patterns.cpp`, `ipcframe.hpp`): `RequestInternetServerList` alloc-size const
`0x350→0x354` (masked out), `IClientAppManager::RunIPCFrame` pattern replaced, and
the `IClientRemoteStorage` dispatch-tree median root drifted ~1.9M (outside the
tolerance band) — now matched by three comparison-immediate fingerprints
(`0x5DB4729A`, `0x7F3F5645`, `0x84692E78`) instead of root proximity.

**Directly relevant to lumalinux** (same byte-pattern method over `steamclient.so`).
None of the three moon fixed are lumalinux's own patterns, but the same client
update can have moved ours (reconcile `NotifyLicensesUpdated`, DepotKey, GMRC,
package-0, ShaderDepot). **Operational action (Steam-update workstream):** confirm
the cron (`watch-steam.yml`) registered the 07-21 build and `check_patterns.py`
still resolves all criticals; re-derive any that moved. Also reinforces **M1**
(mask layout-constant bytes — exactly moon's `0x350→0x354` fix): masking those turns
this drift class into a clean hash-bump instead of a re-derive.

### Delta verdict

Two ports worth doing, both 100% ours (no SLSsteam overlap):
- **M4 circuit breaker + re-probe** (D1) — clearest win; the endpoint half is done.
- **DLC-without-key handling** (D2) — pending a Deck test to confirm it's a real gap.

Everything else is SLSsteam-layer (D3, D4), runtime-model-specific (D5),
headcrab/`.desktop` (D6), or convergence (D7). D8 is not a moon port but the
**Steam-update alarm**: audit our patterns against the 2026-07-21 build.

---

## Delta — 2026-08-18 (desde el delta del 2026-07-22)

*53 commits, 2026-07-23 → 2026-08-14. El grueso es suyo y no nos toca: cobertura
de lanzadores en el escritorio (`557ec42`, `04d8e56`, `5eb9711`, `a4b5a5e`,
`2984163`), presencia, tickets, y su capa `appinfo`/`provision`. Aquí sólo lo
que cruza con nuestras costuras.*

### ★ D9 — cuarentena de depots: distinguen "clave que no descifra" de "sin clave"

`1022c9f` (07-27) añade un subsistema nuevo de ~730 líneas
(`depotquarantine.cpp` 437, `.hpp` 142, `depotquarantine_store.hpp` 152). Hookea
**`OnChunkUnpacked`** —en dos convenciones, stack y `regparm(3)`— y detecta
cuando una clave que ellos suministraron **falla al descifrar un chunk**. Cada
chunk fallido se identifica por su SHA de 20 bytes en `chunk+0x0c`; el depot pasa
a cuarentena y `ManifestBind` deja de ofrecerlo (`shouldDropManagedDlc`). El
ámbito está acotado a depots gestionados por ellos y con script `stplug-in` vivo
(`inManagedScope`).

**Es una capacidad que no tenemos, y el hueco es real.** Nuestro hook de claves
no puede saberlo por construcción: valida forma (`hex.size() != 64` en
`key_store.cpp:45`, `KeySize >= 32` en el hook) y nada más. Una clave buena y una
mala son 32 bytes indistinguibles hasta que un chunk se descifra.

Lo que LumaDeck tiene son **dos preguntas distintas, y ninguna es ésta**:

| | Pregunta | Cuándo |
|---|---|---|
| `downloads.py:1042` `_count_app_depot_keys` | ¿ha llegado **alguna** clave? | al instalar el `.lua` |
| `steam_utils.py:307` `check_stuck_updates` | ¿Steam se quejó (`UpdateResult=8`)? | después, y con falso positivo conocido |
| **moon** | ¿la clave **descifra**? | durante la descarga |

**Recomendación: NO portar el hook.** Hay una vía mucho más barata que conviene
descartar primero — `depot_key_hook.cpp:80` ya documenta que un fallo de
descifrado sale como **`"Invalid content configuration"`** por el lado de Steam,
así que probablemente Steam lo registre en `~/.local/share/Steam/logs/
content_log.txt` con el depot id. Si es así, son ~50 líneas en `steam_utils.py`
en vez de 730 de C++ y un hook nuevo en el camino de descarga. **Sin verificar:
hace falta mirar ese log durante una instalación que falle de verdad.**

Y antes de nada, la pregunta de frecuencia: **no hay ni un incidente registrado**
de "clave presente pero incorrecta" en nuestros repos. El único fallo de
descifrado documentado es el depot de shaders **sin** clave, ya resuelto por otra
vía (ShaderDepot skip, RESEARCH §13.9). Es plausible que sea un problema que moon
tiene por inyectar entradas desde manifiestos Lua propios — algo que nosotros no
hacemos.

### D10 — su feed de patrones firmado nace en esta ventana

`be35ab5` (08-01) crea `tools/pattern-refresh/` (~2.400 líneas con tests):
descarga un catálogo por hash de módulo, lo **verifica con Ed25519** y lo cachea.
`85ccd0c` lo consume desde catálogos locales exactos, `1668847` (08-09) mueve el
refresco **al wrapper de Steam**, y `ef36141` lo hace no bloqueante.

Comparación con nuestro feed de RVAs, sin acción:
- **Firma**: ellos verifican Ed25519 con clave pública horneada en compilación;
  `rva_feed.cpp` es HTTPS sin firmar. Declinado deliberadamente y por buenas
  razones — ver `rva-feed-design.md` §14 (la raíz de confianza sigue siendo
  GitHub) y §15 (por qué los overrides por entorno se quitaron).
- **Momento**: ellos refrescan desde el wrapper, antes de Steam; nosotros
  descargamos perezosamente en el primer `Resolve()`. **No es un problema**:
  `main.cpp:428` instala los hooks en un `std::thread(...).detach()`, así que esa
  descarga nunca estuvo en el camino del cargador.
- **Dónde vive la criptografía**: en su binario aparte, no en el `.so` inyectado
  (`LDFLAGS := -shared -Wl,--no-undefined -lpthread -ldl`). Ése es el patrón
  correcto si algún día firmamos, y la razón está en `curl.cpp`: una dependencia
  NEEDED nueva reproduce el fallo de carga en `reaper` de 0.13.6.

`643a163` + `9e0f3b6` (07-30) añaden además "runtime hook attestation" y un
"generic locator probe" (~430 líneas): validan en runtime que los hooks quedaron
donde tocaba. Nuestro equivalente es `status.json` con el resultado por hook, más
las comprobaciones cruzadas de DepotKey (RTTI) y GMRC (xref) — que, ojo, **avisan
pero no bloquean**: si discrepan, se usa el patrón y se loguea.

### Verificado y NO aplicable

- **`774acc2` (07-29) "run watcher-originated client calls on the owner thread"**
  — disciplina de hilos para llamadas nacidas en el watcher. Ya lo hacemos: el
  hilo de inotify sólo toca la primitiva de despertar (`NotifyKeysChanged`),
  nunca Steam.
- **`362476d` size-0** — ya cubierto en el delta del 2026-07-10 (ver arriba), con
  la misma conclusión. Se anota aquí sólo para no volver a perseguirlo: nuestro
  BuildDep **sólo parchea entradas existentes**, nunca añade, así que no puede
  crear una entrada de tamaño 0 (`depot_dependency_hook.cpp:60-81`, bucle de `0`
  a `m_Size` sin tocar `m_Size`), y además el hook está **apagado por defecto**
  (`main.cpp:198`).

### Neto del delta

**Un hueco real (D9) y ningún accionable inmediato.** La cuarentena de claves es
una capacidad que no tenemos, pero antes de portar nada toca (a) comprobar si
`content_log.txt` ya trae el dato y (b) confirmar que el problema nos ocurre. Lo
demás es su infraestructura, o cosas que ya resolvemos de otra forma.

## References

- moon: `src/feats/depotkey.cpp` (`importLuaScripts`, `provisionManifests`,
  `onStartup`, `recvDepotKey`, `saveKeyToCache`), `src/feats/depotkey.hpp`;
  `src/feats/apps.cpp` (`sendPICSInfoRequest`), `src/feats/pics.cpp`,
  `src/feats/cmclient.cpp`, `src/feats/appinfo_vdf.cpp`,
  `src/feats/appinfo_provision.cpp`, `src/feats/packagepatch.cpp`,
  `src/feats/manifestid.cpp`, `src/feats/manifestbind.cpp`,
  `src/feats/manifestcode.cpp`, `src/utils/ManifestFetch.cpp` (the GMRC fetch —
  `gmrc.wudrm.com` + `manifest.steam.run` fallback).
- vanilla SLSsteam (`AceSLS/SLSsteam`): `src/feats/apps.cpp:sendPICSInfoRequest`
  (the 15-line token-attach — the whole of its PICS handling).
- lumalinux: `src/hooks/depot_key_hook.cpp`, `src/key_store.{hpp,cpp}`,
  `src/hooks/load_package_hook.cpp`, `src/hooks/package_zero_finder.cpp`,
  `src/hooks/depot_dependency_hook.cpp`, `src/hooks/gmrc_hook.cpp`,
  `src/gmrc_store.hpp`, `tools/steamidra_lite.py`; `docs/method.md` (the six gates), `docs/RESEARCH.md`
  §2–3, §6.
- moon source reviewed: [`swwayps/slsteam-moon`](https://github.com/swwayps/slsteam-moon)
  v2.6 (AGPL-3.0) (formerly Codeberg `unplausible/slsteam-moon`; the project moved to GitHub).
- Part 2 — reviewed: [`swwayps/slsteam-moon`](https://github.com/swwayps/slsteam-moon)
  `main`, commits 2026-06-23 → 2026-07-05. Key files: `src/patterns.cpp`,
  `src/patterns.hpp`, `src/feats/ipcframe.hpp` (M1); `src/config.cpp`,
  `res/config.yaml` (M2); `setup.sh` (M3, M7); `src/utils/ManifestFetch.cpp`,
  `src/feats/pics.cpp` (M4); `src/feats/appinfo_provision.cpp`,
  `src/feats/manifestsynth.hpp`, `src/feats/apps.cpp` (M5); `src/feats/cefport.hpp`,
  `src/main.cpp` (M6). Compared against lumalinux `src/patterns.hpp`,
  `src/hooks/gmrc_hook.cpp`, `src/gmrc_store.hpp`, `src/update.cpp`,
  `.github/workflows/watch-steam.yml`, `tools/steamidra_lite.py`.

---

## Delta — 2026-09-01 (desde el delta del 2026-08-18)

*24 commits, 2026-08-15 → 2026-08-29, rama `slsteam-moon` (la principal), hasta
`997a1a3`. Sin tag nuevo: el último sigue siendo v2.8. El grueso vuelve a ser suyo
—recarga en caliente, provisión de `appinfo`, y una tanda de rendimiento con olor a
biblioteca gigante— pero esta ventana trae **la primera aportación de método** desde
que seguimos el proyecto: cinco commits sobre disciplina de patrones que documentan un
ciclo completo (endurecer → romperse → afinar) que nosotros nos podemos ahorrar.*

**Profundidad de lectura, para que se sepa qué respalda cada afirmación:** leídos a
fondo los cinco de `patterns` (`memhlp.cpp`, `pattern_scan.hpp`) y los dos de
`runtime` (incluido `launcher-shim.lib.sh` entero), y contrastados contra nuestro
`src/patterns.cpp`, `tools/derive_patterns.py`, `tools/check_patterns.py` y
`setup.sh`. Los de `hotreload` / `manifests` / `stats` / `dlc`, sólo mensaje,
diffstat y ficheros tocados. Los de `perf` / `config` / `ui` / `library`, sólo el
mensaje. Nada compilado.

### ★ D11 — su cliente se pone al día con su propio auditor, y de paso enseña el caso que el nuestro no modela

Cinco commits (23 y 26 de agosto) que cuentan una historia en dos actos.

**Acto 1 — `d3402a1` (23-08): prohibir la firma ambigua.** Su `patternScan` **paraba
en la primera coincidencia**; sólo contaba duplicados si `extendedLogging` estaba
puesto, o sea nunca en producción. Pasa a barrer el módulo entero y exigir
exactamente una. El comentario que dejan en `pattern_scan.hpp` es la mejor
formulación del riesgo que he leído en ningún sitio:

> *"Keeping the first hit out of several silently binds whichever site happens to sit
> at the lower address: a locator can then be wildcarded just enough to also match a
> neighbouring function or field, and the wrong one gets hooked with no diagnostic at
> all. The offline auditor already refuses that; this is the same rule inside the
> client."*

Nótese la última frase: **su auditor offline ya lo rechazaba.** Esto no era un agujero
de diseño, era el cliente yendo por detrás de su propia herramienta.

**Acto 2 — `a2cef85` (26-08): y entonces la regla estricta les rompe la carga.** Un
localizador alcanzado desde **veinte sitios de llamada** tenía veinte coincidencias
legítimas, y exigir una sola abortaba el arranque. La afinan a lo que llaman
*convergencia*: si hay varias coincidencias, se sigue cada una y se exige que **todas
resuelvan al mismo destino**. Eso es una función con muchos llamadores, no una firma
infra-especificada. Destinos distintos, o nada que seguir, siguen siendo ambigüedad.
Con tope (`kMaxConvergenceCandidates = 256`) para que un patrón patológico no haga
caminar al resolutor sin límite durante la carga.

**Lo nuestro, comprobado antes de escribir nada — y sale mejor de lo que parecía.**
La unicidad **ya es nuestra invariante**, y en dos sitios:

| Herramienta | Qué garantiza |
|---|---|
| `derive_patterns.py:317,400` | Sólo acepta un patrón derivado si casa **exactamente una vez**. *"Auto-derivation is only trustworthy when there's exactly ONE referencing function and its fresh prologue matches UNIQUELY"* |
| `check_patterns.py` | `classify_hit_count()` marca `AMBIGUOUS` cualquier n>1. En un hook **CRITICAL** eso es **exit 3 = BLOCKING**: abre issue y **no mete el hash en la whitelist** |
| `.github/workflows/watch-steam.yml` | Cron diario (`17 7 * * *`) que lo corre contra el `steamclient.so` nuevo |

Así que la pregunta "¿nuestros patrones enganchan una única función?" tiene respuesta
**sí, verificada por build y de forma automática**, y el equivalente del acto 1 lo
tenemos cubierto desde antes. Una versión anterior de esta sección lo daba por
agujero abierto; era una lectura de `FindInSteamclient` sin mirar el tooling que lo
respalda.

Quedan **dos deltas reales**, los dos pequeños, y conviene no inflarlos:

**a) La garantía es offline y no viaja a la máquina del usuario. [BAJA]**
`FindInSteamclient` (`patterns.cpp:95`) sigue cogiendo la primera coincidencia sin
contar; los tres hooks críticos la usan. En la práctica está respaldada por el cron,
pero la ventana existe: un build de Steam nuevo que llega a la Deck de alguien **antes**
de que el cron lo bendiga o lo bloquee. Y en esa ventana nuestro gate de hash es
**advisory** (`main.cpp:150` sólo lanza un toast; `SafeMode: no` es el default), así
que los hooks se instalan igual. Añadir el conteo en el runtime sería defensa en
profundidad, no tapar un agujero. Coste bajo; valor bajo. **No accionable hoy**, se
anota para cuando se toque `patterns.cpp` por otro motivo.

Relacionado, y esto sí conviene tenerlo presente: el cruce de GMRC **no bloquea**.
Cuando el patrón y la xref discrepan, `patterns.cpp:212` avisa y **usa el patrón
igual** (*"(disagree) — using pattern; investigate the anchor"*). El cruce por RTTI de
DepotKey sí falla cerrado (`rtti.cpp:276`), pero es el camino alternativo.

**b) El caso de convergencia NO lo modela nuestro auditor. [BAJA, pero es el que puede
morder]** `classify_hit_count()` devuelve `AMBIGUOUS` para **cualquier** n>1, sin
distinguir "veinte llamadores del mismo destino" de "dos funciones distintas". Si un
día un patrón crítico nuestro cae en el primer caso —exactamente lo que le pasó a moon
el 26-08— `check_patterns.py` daría **exit 3 BLOCKING** sobre un build perfectamente
sano, y nos bloquearía la whitelist hasta que alguien lo investigara a mano.

Ya tenemos un precedente que lo confirma, y lo resolvimos por otra vía: **LoadPackage**
está clasificado como `DIAGNOSTIC` precisamente porque su multi-match es *esperado*
(*"3 candidates, runtime picks index 0"*). O sea que el caso ya nos apareció una vez y
lo tratamos degradando el tier del hook, no analizando la convergencia. Eso vale para
un hook opcional; para un crítico no habría esa salida.

**Accionable, barato y preventivo:** enseñar a `classify_hit_count()` (o a su llamador)
a seguir cada coincidencia cuando el patrón es una llamada/salto relativo y devolver
`CONVERGENT` en vez de `AMBIGUOUS` si todas apuntan al mismo destino. Son unas decenas
de líneas en el auditor, no tocan el `.so`, y nos ahorran el falso bloqueo el día que
pase. Moon ya se comió ese ciclo entero; el valor de este delta es no repetirlo.

### D12 — sus dos commits de patrones "menores" son la lección de `verify_mask.py`

`a8029de` (23-08) relaja con comodines el desplazamiento relativo al GOT y el offset
del campo de propiedad *"para que ambos sigan resolviendo en el cliente actual"*.
`0fb590f`, **el mismo día**, lo revierte a medias: *"the neighbouring field shares the
emitter shape, so relaxing the displacement resolved to the wrong offset on the newer
client"*. Acaban fijando el campo y poniendo el comodín sólo en el registro base, y
añadiendo un test contra el cliente anterior además del nuevo.

Es exactamente el terreno de `tools/verify_mask.py` (*"¿sigue siendo segura esta
máscara?"*) y `tools/experiment_framesize_mask.py`. La lección, en una línea:
**relajar de más y no relajar fallan de formas distintas, y la primera falla en
silencio.** Un patrón que deja de casar sale en el log y en `maintenance.md`; un
patrón que casa en el campo vecino no lo ve nadie.

Sin accionable — nuestras herramientas ya existen para esto. Se anota como munición
para la nota de cabecera de `verify_mask.py`: hay un caso real, fechado y con commit,
de un comodín de más resolviendo al vecino.

### D13 — endurecen el shim del lanzador contra escalada, y nosotros salimos limpios por diseño

`74b6b1b` y `821e8cb` (27-08). El problema, en sus palabras:

> *"This script is root-owned and sits on the system PATH, but the wrapper it delegates
> to lives in a user's home directory and is writable by that user. That inversion is
> only safe while the two identities agree."*

Una invocación elevada (`sudo -E`, `sudoers env_keep += HOME`, un helper que hereda el
entorno) hacía que su shim de root ejecutara un script escribible por un usuario sin
privilegios. Lo tapan exigiendo que el wrapper sea fichero normal (no symlink),
propiedad del UID efectivo, y sin bit de escritura de grupo ni de otros. El segundo
commit extiende la misma comprobación al `steam.sh` de último recurso, porque si no el
guardia era decorativo: *"refusing a wrapper the effective account does not own and
then executing a steam.sh from the same directory tree is the identical escalation by
a different path."*

**Verificado: no nos aplica, y no por suerte.** Nuestro modelo de wrapper viene de
moon, así que había que mirarlo. La diferencia es que **no instalamos nada propiedad
de root en el PATH del sistema** — `setup.sh:29` lo dice como propiedad de diseño
(*"No root required"*). Los tres caminos hasta nuestro wrapper son de usuario:
`.desktop` parcheados, un drop-in de PATH en el rc del shell, y un drop-in systemd
`--user` sobre `steam-launcher.service`. Ni el autostart del sistema se toca:
`setup.sh:732` hace sombra en el directorio del usuario en vez de editar
`/etc/xdg/autostart`.

**Valor de anotarlo:** el día que alguien proponga un shim en `/usr/local/bin` para
simplificar la cobertura —que es una idea que se le ocurre a cualquiera—, aquí está el
motivo fechado para no hacerlo, con el commit ajeno que tuvo que arreglarlo.

### Verificado y NO aplicable (o suyo y no nuestro)

- **Recarga en caliente (`a138c0b`, `d748367`, `997a1a3`)** — su equivalente del Add
  Game sin reiniciar, construido como una máquina de estados con generaciones,
  "preparación en vivo" y publicación diferida: refrescan `appinfo` tras actualizarse
  los paquetes y no publican una generación hasta confirmar los mapeos de
  compatibilidad. Es mucho más complejo que nuestro reconcile, y la explicación
  probable es que resuelven un problema más difícil: ellos **sintetizan** entradas de
  biblioteca desde manifiestos Lua propios, nosotros dejamos que Steam escriba las
  suyas. Nada que portar. *Lectura superficial — es la parte de esta ventana con menos
  respaldo, y si algún día el reconcile da guerra, es el primer sitio donde volver.*
- **`348eed5` descompresión en proceso** — dejan de lanzar un hijo para descomprimir el
  zip *"para evitar carreras al recoger procesos hijos dentro de Steam"*. Nosotros
  lanzamos `steamidra_lite.py` como subproceso, pero **desde Decky, no desde dentro del
  proceso de Steam**, así que la carrera que describen no se da. Anotado por si algún
  día algo nuestro corre dentro de Steam.
- **`b43b317` caídas del proveedor** — reintentos vivos y un estado de "se puede
  instalar" que falla en abierto. Nuestra cascada de GMRC (`opensteamtool` → `wudrm` →
  `steamrun`, RESEARCH §7) ya cubre la indisponibilidad por otra vía.
- **`ce71798` enrutado de logros** — condicionan el manejo local de estadísticas a
  evidencia de licencia nativa y correlacionan respuestas por petición. Contiene una
  línea que merece seguimiento aparte si alguna vez tocamos esa zona: *"Share
  eligibility with CloudRedirect"* — se están coordinando explícitamente con CR.
- **Rendimiento y topes (`b26737e`, `efa77b8`, `29097d8`, `1729c6a`)** — escaneos
  cuadráticos al buscar depots, un tope `MaxManagedApps` porque soltar decenas de miles
  de scripts *"stalled the client for minutes"*, 199 recargas de config en una sola
  copia de directorio (lo agrupan en una ventana de silencio), y 133 notificaciones en
  una pasada. Todo consecuencia de su modelo de importación masiva, que no tenemos.
  Curiosidad: su arreglo de agrupar eventos del vigilante es el mismo problema que
  SLSsteam tocó en `29097d8`-equivalente; nuestro watcher de `keys.txt` no lo sufre
  porque escribimos un fichero, no volcamos directorios.
- **`e459276` fechas de inclusión, `08c5af8` DLC configurados, `a3fdf1b` CDN** — suyos.
- **`465a420`** — recortan el README de 48 líneas a 4 para dejar claro que son un fork
  de SLSsteam.

### Balance del delta

| # | Qué | Prioridad | Estado |
|---|---|---|---|
| 1 | `classify_hit_count()` no distingue convergencia de ambigüedad → falso BLOCKING posible | Baja | **Abierto**, preventivo y barato |
| 2 | El conteo de coincidencias no existe en runtime (la garantía es sólo offline) | Baja | Anotado; defensa en profundidad, no agujero |
| 3 | El cruce de GMRC avisa y usa el patrón igual al discrepar | Baja | Anotado, va con el 2 |
| 4 | Caso real de comodín de más resolviendo al campo vecino | — | Munición para `verify_mask.py` |
| 5 | Escalada por wrapper en `$HOME` ejecutado con privilegios | — | **Verificado y no aplicable**: no ponemos nada de root en el PATH |

Ninguno urge. El único que produce trabajo es el 1, y es preventivo: nos ahorra el
falso bloqueo del cron el día que un patrón crítico caiga en el caso de convergencia,
que a moon le costó abortar la carga y dos commits arreglarlo.

*Fuentes de esta ventana: `swwayps/slsteam-moon` @ `997a1a3`, ficheros
`src/memhlp.cpp`, `src/memhlp.hpp`, `src/pattern_scan.hpp`, `src/patterns.cpp`,
`tools/launcher-shim.lib.sh`. Contrastado contra lumalinux `src/patterns.cpp`,
`src/rtti.cpp`, `tools/derive_patterns.py`, `tools/check_patterns.py`,
`.github/workflows/watch-steam.yml` y `setup.sh`.*
