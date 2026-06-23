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

## References

- moon: `src/feats/depotkey.cpp` (`importLuaScripts`, `provisionManifests`,
  `onStartup`, `recvDepotKey`, `saveKeyToCache`), `src/feats/depotkey.hpp`;
  `src/feats/apps.cpp` (`sendPICSInfoRequest`), `src/feats/pics.cpp`,
  `src/feats/cmclient.cpp`, `src/feats/appinfo_vdf.cpp`,
  `src/feats/appinfo_provision.cpp`.
- vanilla SLSsteam (`AceSLS/SLSsteam`): `src/feats/apps.cpp:sendPICSInfoRequest`
  (the 15-line token-attach — the whole of its PICS handling).
- lumalinux: `src/hooks/depot_key_hook.cpp`, `src/key_store.{hpp,cpp}`,
  `src/hooks/load_package_hook.cpp`, `src/hooks/package_zero_finder.cpp`,
  `src/hooks/depot_dependency_hook.cpp`, `src/hooks/gmrc_hook.cpp`,
  `tools/steamidra_lite.py`; `docs/method.md` (the six gates), `docs/RESEARCH.md`
  §2–3, §6.
- moon source mirror used for this review: branch `tmp/slsteam-moon`
  (delete after use).
