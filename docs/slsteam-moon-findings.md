# Findings — slsteam-moon vs lumalinux (research notes)

*Investigation date: 2026-06-23. Reference: [`unplausible/slsteam-moon`](https://codeberg.org/unplausible/slsteam-moon)
v2.6 (AGPL-3.0). These are notes for a possible future port of selected ideas
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

## The one thing worth recording — moon's licence reconcile

lumalinux does **not** do a licence reconcile (verified — no `LicensesUpdated_t`
broadcast anywhere). lumalinux most likely doesn't need it: its finder injects
**after login** (warm-ish cache), whereas moon's hook injects **early/cold** and
therefore hits the re-request hang.

But it's a documented fix worth knowing: if lumalinux ever hits "injected into
package 0, then Steam re-requests PICS forever", the fix is to **broadcast a
`LicensesUpdated_t` once on the local `CUser`** after the first injection. It is
reasonably portable (a one-shot callback broadcast, **not** a message-dispatch
hook, so no SLSsteam overlap), though it depends on resolving the `CUser` /
broadcast pattern.

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
a message hook. Recorded as: the token-locked-app gap is now solved *in moon*; for
us it would be a `steamidra_lite`-side offline synthesis, and only worth it for the
handful of token-gated titles.

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
- moon source reviewed: [`unplausible/slsteam-moon`](https://codeberg.org/unplausible/slsteam-moon)
  v2.6 (AGPL-3.0).
- Part 2 — reviewed: [`swwayps/slsteam-moon`](https://github.com/swwayps/slsteam-moon)
  `main`, commits 2026-06-23 → 2026-07-05. Key files: `src/patterns.cpp`,
  `src/patterns.hpp`, `src/feats/ipcframe.hpp` (M1); `src/config.cpp`,
  `res/config.yaml` (M2); `setup.sh` (M3, M7); `src/utils/ManifestFetch.cpp`,
  `src/feats/pics.cpp` (M4); `src/feats/appinfo_provision.cpp`,
  `src/feats/manifestsynth.hpp`, `src/feats/apps.cpp` (M5); `src/feats/cefport.hpp`,
  `src/main.cpp` (M6). Compared against lumalinux `src/patterns.hpp`,
  `src/hooks/gmrc_hook.cpp`, `src/gmrc_store.hpp`, `src/update.cpp`,
  `.github/workflows/watch-steam.yml`, `tools/steamidra_lite.py`.
