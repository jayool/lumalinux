# Findings — LumaCore vs lumalinux + LumaDeck (research notes)

*Investigation date: 2026-08-13. Reference: [`Midrags/SFF`](https://github.com/Midrags/SFF)
@ `6d2fb30` (SteaMidra `6.6.4`), the `LumaCore/` subtree — the injected DLL, not
the Python launcher. C++20/CMake, MSVC, **x64-only**, Microsoft Detours + Lua 5.4
+ protobuf + toml++. Companion to [`opensteamtool-findings.md`](opensteamtool-findings.md),
[`slsteam-moon-findings.md`](slsteam-moon-findings.md) and
[`slssteam-analysis.md`](slssteam-analysis.md); the same coexistence lens applies.*

LumaCore is different from every other program analysed in this folder: **it is
the ancestor, not a sibling.** lumalinux's core seams — the
`LoadDepotDecryptionKey` hook, the `BuildDepotDependency` GID rewrite, the
Package-0 `AppIdVec` injection, the KeyValues-accessor contract — were
reverse-engineered *from LumaCore* and are already labelled "mirrors LumaCore"
in [`RESEARCH.md`](RESEARCH.md) §11. So this is not a hunt for tricks to port;
most of what LumaCore does on our shared gates we already do, by design, the same
way. Its value is in three things: catching where a mirror has **drifted**,
mapping the **coexistence boundary** (what LumaCore does in one DLL that our
stack splits across three projects), and the handful of genuine **deltas** —
things that changed since our last read, or that we do differently and better.

### How our stack is split (the mapping axis)

On Linux the work LumaCore does in one in-process DLL is split across four
components, and only one of them is mine:

- **lumalinux** *(mine)* — depot keys, manifest pinning, GMRC, package-0
  surfacing, shader-depot, license reconcile. **The focus of this doc.**
- **SLSsteam** *(AceSLS, separate dev)* — ownership, licensing, tickets, family
  sharing, the whole protobuf/wire layer. Interesting only as **research**;
  never a port target.
- **CloudRedirect** *(Selectively11, separate dev)* — cloud saves.
- **Windows-only** — techniques with no Linux/Proton analogue; **noted and
  discarded**.

LumaDeck is the Decky **frontend** that orchestrates these three, plus the
game-side fix tools (Steamless, Goldberg, netsock, Unsteam/OnlineFix); it is not
itself an in-client executor.

> **Headline delta:** LumaCore has re-grown a runtime manifest-request-code
> bridge (Finding 1). [`method.md`](method.md) §"Note on RESEARCH §11.5" and
> [`RESEARCH.md`](RESEARCH.md) §11.5 both currently say "the current LumaCore.dll
> in `Midrags/SFF` has **no** `GetManifestRequestCode` hook at all." As of `6.6.4`
> that is **no longer true.** Both notes are now stale and are corrected here.

---

## Context — what LumaCore is and how it loads

LumaCore is the **DLL half of SteaMidra**. SteaMidra's Python GUI does the
on-disk setup (writes `.lua` into `config/stplug-in/`, seeds `depotcache/`,
manages ACFs — our Linux equivalent is `tools/steamidra_lite.py`); LumaCore is
the in-process component that lives inside the running Steam client and makes it
behave as if not-owned games are owned. Four files drop next to `steam.exe`:

- `dwmapi.dll` / `xinput1_4.dll` — thin proxy DLLs (gated on the host being
  `steam.exe`) that `LoadLibraryA("LumaCore.dll")` on attach; the second is a
  backup load gate.
- `LumaCore.dll` — the hook library.
- `LumaCorePayload.dll` — injected into **game** processes for online-fix (EOS
  bridge, lobby redirection).

At attach `LumaCore.dll` (`core/entry.cpp` → `core/Orchestrator.cpp`):

1. Copies the live `steamclient64.dll` to `bin\lcoverlay.dll` ("diversion") and
   hooks the **copy** — never patching in-use code; the SteamUI hook intercepts
   `LoadModuleWithPath("steamclient64.dll")` to hand back the copy's handle.
2. Reads the Steam build id from `steam.exe!GetBootstrapperVersion`.
3. Primes the runtime pattern map (for `steamclient64.dll` **and** `steamui.dll`)
   and spawns a worker that installs **40+ Detours hooks plus VEH captures**,
   kicks the network pattern refresh, and starts a `config/stplug-in/` watcher.

This is a **superset that spans all of our components** — the same shape OST has,
but closer, because our hooks were modelled on this exact DLL.

### Architecture constraint (why most LumaCore tricks are not drop-in) — same ruling as OST and moon

LumaCore is **one in-process thing.** It owns the whole client, so it hooks
freely at the **message / wire / IPC / VEH layers**: its own IPC dispatch
(`IPCProcessMessage`), outbound/inbound protobuf frame rewrites
(`BBuildAndAsyncSendFrame` / `RecvPkt`), and `int3` VEH captures.

Our stack runs **vanilla SLSsteam *plus* lumalinux, and deliberately does not
modify SLSsteam** (`slssteam-analysis.md` §0). SLSsteam already owns the message
dispatch (`CProtoBufMsgBase`) and the IPC path. So **any LumaCore mechanism
implemented as a network / IPC / wire hook is not portable to lumalinux** — it
would double-wrap or reorder SLSsteam's own hooks and crash. lumalinux must use
distinct **function-layer** seams, exactly as it already does for DepotKey /
BuildDep / GMRC. This one ruling — identical to `slsteam-moon-findings.md` and
`opensteamtool-findings.md` — is *why* several things LumaCore does in one place
are split across three components on our side.

### LumaCore feature inventory (reference)

Grouped as reviewed; each item tagged with the component that owns the equivalent
on our side.

- **A · Bootstrap** — proxy-DLL injection + diversion copy `[Windows technique]`;
  build-id detection `[lumalinux]`; runtime SHA-256-keyed pattern DB for
  steamclient64 + steamui `[lumalinux]`; `config/stplug-in/` watcher `[lumalinux
  + steamidra_lite]`.
- **B · Depot keys / manifests `[lumalinux]`** — `DepotKeys`
  (`LoadDepotDecryptionKey`); `DecryptionKeyHook` (`ConfigStoreGetBinary` +
  `FlushToDisk`); `ManifestBind` (`BuildDepotDependency`); the new wire-level
  GMRC bridge (`NetPacket_Manifest` + `ManifestFetch`).
- **C · Ownership / licensing** — `PackagePatch` (`LoadPackage` `[lumalinux]`;
  `CheckAppOwnership` / `GetSubscribedApps` `[SLSsteam]`); `LicenseHooks`
  (`OptedInMask` `[Windows]`; `IsCloudEnabledForApp` `[CloudRedirect/SLSsteam]`;
  `RequiresLegacyCDKey` `[SLSsteam]`); pointer captures + `NotifyLicenseChanged`
  `[lumalinux]`.
- **D · Tickets / Denuvo / SteamStub** — `CmdUser`/`IPCBus`/`IpcDispatch`
  `[SLSsteam]`; `DenuvoAuthenticator` + `ProtectionProbe` `[SLSsteam / Windows]`;
  `SteamStubAuto` + `SteamStubTicket` `[Steamless / Windows]`; `EticketFetcher`
  `[Windows]`.
- **E · Wire / stats / presence `[SLSsteam layer]`** — `PacketRouter`/`NetPacket`
  (`BBuildAndAsyncSendFrame` + `RecvPkt`); donor-SteamID stats
  `[SLSsteam + lumalinux patch + LumaDeck]`; family-sharing; PICS access-token;
  rich presence `[Windows]`.
- **F · Online-fix** — `OnlineFixInject` (`CreateProcessW`) + `LumaCorePayload`
  (EOS) + 480 language sync `[LumaDeck Override + netsock / Windows]`.
- **G · Config / security** — Lua bindings `[steamidra_lite]`; Lua sandbox + HTTP
  allowlist `[N/A — see Finding 8]`; settings / per-module logging /
  `status.json` `[lumalinux]`.

---

## Where our stack already matches or leads (parity by lineage — so the deltas below are the real news)

The four items marked *(RESEARCH §11.x)* have the field-by-field comparison in
[`RESEARCH.md`](RESEARCH.md) §11; this is the summary.

| Capability | LumaCore | Our stack | Verdict |
|---|---|---|---|
| Depot keys | `LoadDepotDecryptionKey` KeyValues accessor (`DepotKeys.cpp`) | lumalinux `depot_key_hook` — same accessor, which on Linux is a **`CConfigStore` vtable slot** resolved by RTTI (§15) + pattern; `KeySize`-validated | **Parity, same seam** (RESEARCH §11.3) |
| Manifest pin | `BuildDepotDependency` (`ManifestBind.cpp`) | lumalinux `depot_dependency_hook` — same fn, `pDepotInfo`-only, gid-only, `ManifestSize` untouched, guards on `result` | **Parity, same seam** (RESEARCH §11.4) |
| Package-0 surfacing | in-hook `LoadPackage` + `oCUtlMemoryGrow` | lumalinux `package_zero_finder` walks `CPackageInfoCache` (poll-based, dedup, sanity cap) — fires even if `LoadPackage` never does | **lumalinux more robust** (RESEARCH §11.2) |
| No-restart license refresh | captured `g_pCUser` → `MarkLicenseAsChanged` + `ProcessPendingLicenseUpdates` | lumalinux `license_reconcile` → `CUser::NotifyLicensesUpdated`; default ON (v0.16.16), kill-switch `LUMA_NO_RECONCILE` | **Parity, different fn** (RESEARCH §17) |
| Config hot-reload | `DirWatch` on `config/stplug-in/` (`.lua`) | lumalinux `KeyStore::StartWatcher` inotify on `~/.config/lumalinux/` (`keys.txt`) | **Parity** — we watch the generated artifact |
| Install diagnostics | `status.json` (build id, hooks installed/missed) | lumalinux `status.cpp` `status.json` (per-hook INSTALLED/FAILED/DISABLED); LumaDeck reads it | **Parity** |
| Shader-cache depot | **nothing** | lumalinux `shader_depot_hook` (keyless shader pre-cache skip) | **lumalinux leads, no counterpart** |

The genuinely new material — the deltas — is below.

---

## Finding 1 — LumaCore re-grew a runtime manifest-request-code bridge (corrects our own docs) `[lumalinux, ★ headline]`

### The change

`method.md` and `RESEARCH.md` §11.5 record, from an earlier read, that SFF
LumaCore had **no** `GetManifestRequestCode` hook — that the code was pre-fetched
by SteaMidra's Python layer to pre-seed `depotcache/`, making lumalinux's runtime
GMRC hook *unique to our native-download path*. As of `6.6.4` that is wrong.
Verified in source, LumaCore now ships a **wire-level** request-code bridge:

- `hooks/client/NetPacket.cpp` registers the FNV-1a job hash for
  `"ContentServerDirectory.GetManifestRequestCode#1"`.
- `hooks/client/NetPacket_Manifest.cpp` (`Handlers::DepotFallback`) hooks both
  directions: on **send** it parses the outbound
  `CContentServerDirectory_GetManifestRequestCode_Request`, and if the depot is
  Lua-tracked (`LuaLoader::HasDepot`), records `jobid_source` and fires
  `ManifestFetch::Submit`. On **recv** of the matching `jobid_target` it sets
  `eresult = k_EResultOK` and rewrites the body with
  `set_manifest_request_code(*resolved)`.
- `runtime/ManifestFetch.cpp` returns `std::optional<uint64_t>` — the request
  **code**, not a manifest file — via Lua `fetch_manifest_code(_ex)` then a
  provider chain (`manifest.opensteamtool.com` → `manifest.steam.run` →
  `gmrc.wudrm.com`), with the `OpenSteamTool/1.0` UA only for the first host, an
  in-flight join and a per-gid cache.

(LumaCore's own `docs/LumaCore.md` describes this as writing "the response body …
as if the original call succeeded", which reads like a manifest-file fetch — the
source is narrower: it resolves the code. There's also a configured-but-unenforced
detail: `[manifest_fetch] trusted_hosts` is loaded and logged but `RunOnce` never
checks it, so the "trusted host" gate is surfaced, not applied.)

### Why it does not change our design — and stays not-portable

lumalinux does GMRC at the **function layer** (`gmrc_hook.cpp`): it hooks
`GetManifestRequestCode(this_, app_id, depot_id, branch, out_code)`,
`Gmrc::GetCode(gid)` → `*out_code = code; return 1`, gated on
`HasManifestGid(gid) || HasDepot(depot_id)` — the **OR covers the shader
pre-cache** case (depot==appid, gid from PICS not the `.lua`). The target is
resolved by the RVA feed → locator. The providers are the **same three**
LumaCore uses (`gmrc_store.hpp`: opensteamtool → wudrm → steamrun via libcurl,
`OpenSteamTool/1.0` UA to clear opensteamtool's Cloudflare WAF).

So the two are at parity on providers and caching; the only difference is the
**layer** — and that difference is exactly why LumaCore's version is the one we
cannot copy. Its bridge lives on `BBuildAndAsyncSendFrame`/`RecvPkt`, the layer
**SLSsteam owns**. A wire-frame rewrite on our side would collide with SLSsteam.
lumalinux's function-layer hook is SLSsteam-safe *and* covers the shader case
LumaCore's does not gate for.

### Verdict — **documentation fix, no code change.**

Correct `method.md` §"Note on RESEARCH §11.5" and `RESEARCH.md` §11.5: LumaCore
now has a **wire-layer** GMRC bridge (SFF `6.6.4`, `NetPacket_Manifest.cpp` +
`ManifestFetch.cpp`); lumalinux's edge is *mechanism* (function-layer,
SLSsteam-safe) + *coverage* (shader pre-cache), not *existence*. This is the
reason to re-read LumaCore on each SteaMidra bump — our divergence notes go stale
silently.

---

## Finding 2 — RVA feed vs pattern DB: both self-heal by build hash, different payload `[lumalinux]`

### The two mechanisms

**LumaCore** publishes **byte-pattern TOMLs** keyed by the DLL's lowercase-hex
SHA-256, fetched **cache-first** → user mirror →
`raw.githubusercontent.com/KoriaPolis/Steam-Auto-PT` → jsDelivr → gitflic.ru,
with atomic `.tmp`+rename writes, for both `steamclient64.dll` and `steamui.dll`
(`PatternFetcher.cpp`, `ByteScan.cpp`). There's a signature scaffold
(`PatternSig::Verify`, RSA-2048 PSS-SHA256) but the embedded modulus is an
**all-zero placeholder**, so with `patternRequireSigned=false` (default)
**unsigned bodies are accepted with a warning** — the signing story is stubbed.

**lumalinux** ships an **RVA feed** (`rva_feed.cpp`, `res/rvas/<sha>.yaml`): it
publishes **RVAs** (offsets) keyed by the steamclient.so build SHA, fetched from
`raw.githubusercontent.com/jayool/lumalinux/main/res/rvas` (hardcoded — the
`LUMA_RVAS_URL`/`LUMA_RVAS_DIR` overrides were removed, see rva-feed-design §15)
and disk-cached in `~/.cache/lumalinux/rvas/`.
Resolution is **RVA-first with byte-pattern fallback**, guarded by two runtime
sanity checks — the RVA must translate via `VaddrXlate` and land inside
steamclient.so's `r-x` mapping (`inSteamclientExec`). It is **wired into 5 hooks**
(DepotKey, GMRC, BuildDep, ShaderDepot, Reconcile) and shipped since v0.18.0.
Per `rva-feed-design.md` it is modelled on **OpenSteamTool's `PatternLoader`** —
the same ancestor as LumaCore's pattern system.

### The distinction

Both are the same idea (self-heal keyed by a per-build DLL hash), resolved
differently:

- LumaCore ships **byte patterns**, which break on a recompile that shifts a
  function prologue — the exact failure the feed is meant to survive.
- lumalinux ships **RVAs**, which are robust to prologue shifts (the offset is
  still valid), and keeps the byte pattern only as a fallback + a post-resolve
  byte check.
- **Neither signs.** LumaCore has the scaffold but it's a placeholder; lumalinux
  has no scaffold. Both currently trust whatever the mirror returns. (My earlier
  draft claimed lumalinux signs the feed — it does not.)

This **refines** OST Finding 1 ("explored, implemented, then reverted"): we did
not simply revert the self-heal — we ship an RVA feed, just a safer payload than
the byte-pattern DB, and single-sourced from our own repo rather than a
multi-mirror crowd feed.

### Verdict — **parity of concept; lumalinux's payload is safer. No change.**

Keep the RVA feed. If the signing gap ever matters (feed tamper), it's a shared
gap with LumaCore, not a lumalinux regression. Worth a one-line note in
`rva-feed-design.md` that the upstream ancestor's byte-pattern variant is more
fragile.

---

## Finding 3 — the second (ConfigStore) depot-key seam: an evidence-gated hardening candidate `[lumalinux]`

### What LumaCore has that we don't

LumaCore hooks the depot key **twice**: the KeyValues accessor
(`LoadDepotDecryptionKey`, = our B1) **and** `ConfigStoreGetBinary`
(`DecryptionKeyHook.cpp`), a second `CConfigStore` method that reads the
`...\DecryptionKey` blob. On Windows it's belt-and-suspenders, and the same hook
doubles as the app-ticket cache for the Denuvo forge pipeline.

The reframe: on Linux our B1 accessor is **already a `CConfigStore` vtable slot**
(RESEARCH §15, RTTI-derived). So B2 would be hooking a *second* CConfigStore
method — catching any flow where Steam Linux reads a depot key as a binary blob
via `GetBinary` instead of via the accessor slot B1 already covers.

### The evaluation

- **Evidence it's needed on Linux: none observed.** B1 has served keys across all
  tested titles; no "Missing decryption key" has traced to a ConfigStore-binary
  bypass. LumaCore's ConfigStore hook exists partly for the Denuvo ticket cache,
  which we don't have.
- **Cost:** resolve a second CConfigStore slot, another hook to maintain per
  build, a new RVA-feed entry. Low but non-zero.

### Verdict — **low-priority issue candidate, evidence-gated. Do not implement blind.**

Acceptance test to decide it: instrument a `CConfigStore::GetBinary`-equivalent
to log (without serving) every call whose key name contains `\DecryptionKey`,
during installs of Lua-tracked unowned games across a sample of titles. Empty log
→ B2 is confirmed redundant on Linux, document and close. Any hit for a depot B1
didn't serve → B2 becomes a real issue. File with that test as the criterion.

---

## Finding 4 — SafeMode is advisory, not a gate `[lumalinux]`

LumaCore uses the build id to **pick the best signature** — an adaptive
posture. lumalinux's hash check (`Updater::verifySafeModeHash`, `update.cpp`)
compares steamclient.so's SHA-256 against `res/updates.yaml` `SafeModeHashes`,
but it is **advisory** (`main.cpp:136-156`): on a miss it only `Log::Notify`s and
proceeds — "the pattern scan below is the real gate … abort only if the scan
fails." The hash just **labels** whether this is a verified build (for logs /
LumaDeck); it never blocks. Mirrors SLSsteam's `SafeMode=no`.

So the real contrast is not "adaptive vs fail-closed" — **neither fails closed.**
LumaCore picks a signature by build; lumalinux labels the build and lets the
pattern scan (now RVA-first, Finding 2) be the gate. (Correcting my earlier draft,
which wrongly called our SafeMode a hard gate.)

**Verdict — parity of posture, no change.** Noted so the doc doesn't repeat the
fail-closed error.

---

## Finding 5 — tickets / Denuvo / SteamStub: LumaCore forges in-client; we replay or strip `[SLSsteam research / Steamless / Windows]`

The cleanest summary of the whole D block: **LumaCore forges DRM state inside the
client; our stack either replays captured state (SLSsteam) or unwraps the binary
(Steamless).** None of it is a lumalinux port target.

- **Tickets** — LumaCore serves `GetSteamID` (spoof) and
  `GetAppOwnershipTicketExtendedData` with cached-or-**forged** AppTicket/ETicket
  over IPC (`CmdUser.cpp`/`IPCBus.cpp`). SLSsteam instead **replays captured**
  tickets: `ticket.cpp:253` caches ownership + encrypted tickets to
  `~/.config/SLSsteam/cache/*.yaml` (base64), replays them offline, spoofs the
  ticket `steamId`. Same outcome for the replay case; the only piece SLSsteam
  lacks is donor-*capture* (OST Finding 4). `[SLSsteam research]`
- **Denuvo** — LumaCore *forges*: `DenuvoAuthenticator.cpp` + `ProtectionProbe.cpp`
  detect Denuvo (OEP / W+X entropy 7.0+ / section-string) and open a 2-handshake
  auth window that feeds the forged owner SteamID. SLSsteam does the **opposite** —
  `apps.cpp:61-69` + `DenuvoGames` config is a *don't-touch* safety (avoid bans).
  Real Denuvo bypass in our ecosystem is a **LuaTools/SFF crack download**
  (LumaDeck), not an in-client hook. `[SLSsteam defensive research / Windows]`
- **SteamStub** — LumaCore reroutes in-client: `SteamStubAuto.cpp` /
  `SteamStubTicket.cpp` rewrite `pGameID` real→480 and prefer an **app-7 forged**
  ticket. Our side **strips** the SteamStub shell from the `.exe` with
  **Steamless** (`FIXES_MAP.md` problem B) — a game-side unwrap, not a client
  reroute. `[Steamless / Windows technique]`

**Verdict — not portable; the forge-vs-replay-vs-strip contrast is the takeaway.**
One sentence in the doc; the details stay research.

---

## Finding 6 — achievements: donor-SteamID wire substitution vs our three-part offline path `[SLSsteam + lumalinux patch + LumaDeck]`

LumaCore rewrites outbound `Player.GetUserStats` / `ClientGetUserStats` /
`StoreUserStats2` frames (`PacketRouter.cpp` + `NetPacket_UserStats.cpp`) to
substitute a **donor SteamID** that owns the game, walking a built-in pool and
remembering the first that returns useful data.

Our stack reaches the same outcome without touching the wire, across three
pieces:

- **SLSsteam** borrows the schema natively
  (`Achievements::sendAndRecvGetUserStats` / `…GetPlayerStats`).
- **lumalinux** scopes that borrow: `sls_achievement_unblock.cpp` (RESEARCH §17)
  applies one surgical in-memory patch to `SLSsteam.so`, repointing the guard's
  `isSubscribed(appId)` call so `subscribed && !added → skip borrow` (real
  purchases untouched), `subscribed && added → borrow` (LumaDeck game → native
  achievements). Fail-closed, opt-out `LUMA_NO_SLS_ACH_UNBLOCK`.
- **LumaDeck** generates the schema **offline** (`backend/achievements.py`,
  `UserGameStatsSchema_*.bin` via the Steam Web API; replaced the unmaintained
  SLScheevo).

**Verdict — parity of outcome, ours is friendlier on the Deck** (offline, no
runtime donor lookups) and the donor-substitution is a wire hook we can't take
anyway. The only lumalinux code here is the scoping patch, which already exists.
No action.

---

## Finding 7 — online-fix: self-contained EOS engine vs Proton orchestration `[LumaDeck Override + netsock / Windows]`

LumaCore ships its **own** online-fix engine: `OnlineFixInject.cpp` hooks
`CreateProcessW`/`CreateProcessAsUserW`, creates the game suspended, injects
`LumaCorePayload.dll`, and resumes; the payload bridges EOS (`EOS_Connect_Login`
forced to a Device-ID anonymous login using the Steam persona name) and redirects
lobbies, self-propagating into child processes. It also syncs the real game's
language into the Spacewar-480 ACF and guards it with the `FlushToDisk` hook.

Our stack is a **Proton orchestrator**, not an injector. LumaDeck's "Override"
(`FIXES_MAP.md`) recomputes launch options and writes them via
`SteamClient.Apps.SetAppLaunchOptions`, composing two independent pieces:
`WINEDLLOVERRIDES="OnlineFix64=n,b"` (make Wine load the fix's Windows DLL) or a
launcher redirect, and `LD_AUDIT="…netsock.so"` for the **native**, crack-free
480 route. The EOS/lobby work is done by the **third-party fix** (Unsteam /
OnlineFix.me) that Proton loads, or by netsock natively; the FakeAppId is
registered in SLSsteam.

**Verdict — parity of outcome, opposite machine, not portable.** LumaCore =
self-contained payload + EOS; ours = orchestrate external fixes over Proton +
a native `LD_AUDIT` route. netsock is our closest thing to a native online
bridge, but by loader injection, not a CreateProcess hook. Nothing to port; the
philosophy contrast is the note.

---

## Finding 8 — the Lua sandbox / HTTP allowlist does not apply to us (we don't execute the `.lua`) `[N/A — corrects an earlier claim]`

LumaCore runs a **real Lua VM in-client** for the `.lua` config, and hardens it:
no `luaL_openlibs` (only `_G`/`table`/`string`/`math`), `dofile`/`load`/`require`
stripped (`LuaState.cpp`), and `lcHttpGet`/`lcHttpPost` **host-gated to a
hardcoded allowlist** (`manifesthub1.filegear-sg.me`, `raw.githubusercontent.com`,
`cdn.jsdelivr.net`, `gitflic.ru`, `api.github.com`) against a malicious script
exfiltrating via HTTP.

On our side the `.lua` is parsed by **`steamidra_lite.py` with four regexes**
(`_DEPOT_NO_KEY_REGEX`, `_DEPOT_DEC_KEY_REGEX`, `_SETMANIFESTID_REGEX`, …) — it is
**never executed.** A malicious `.lua` cannot run code or make HTTP calls because
there is no interpreter and no `lcHttpGet`. **The entire threat model LumaCore
mitigates does not exist for us**, and lumalinux consumes the generated `keys.txt`
rather than parsing `.lua` in the `LD_PRELOAD` critical path (OST Finding 2 / moon
§1 — keep `keys.txt`). Nice lineage detail: steamidra_lite's `setManifestid` regex
mirrors LumaCore, capturing depot+gid and ignoring the size arg.

**Verdict — not applicable today.** *(This corrects the earlier draft, which
called the allowlist "the one portable takeaway." It is only relevant if
steamidra_lite ever grows a real Lua interpreter or a script-driven HTTP
primitive — latent, not active.)*

---

## Small niceties & explicitly-not-porting

- **Injection technique (A1/A2)** — LumaCore's dual proxy DLLs + diversion copy
  are the Windows analogue of our dual-load (`LD_AUDIT` for SLSsteam, `LD_PRELOAD`
  for lumalinux, split on purpose — `slssteam-analysis.md` §0.1) and live
  steamclient.so hooking (no copy; libmem). Windows-only; noted, discarded.
- **VEH `int3` captures** — LumaCore's `docs/LumaCore.md` lists four; the current
  source uses int3 for only **`GetAppDataFromAppInfo`** and the `SpawnProcess`
  launch router — the other three pointer captures migrated to Detours. Another
  case of the upstream feature doc lagging the code.
- **SteamStub app-7 route, RichPresence 480↔real rewrite, the 480 language-ACF
  sync** — all Windows-client / online-fix specific. Discarded.

---

## Per-gate summary (LumaCore vs our stack)

| Gate (`method.md`) | LumaCore | SLSsteam + lumalinux + LumaDeck | Who leads |
|---|---|---|---|
| 1 Ownership | `CheckAppOwnership` + `NotifyLicenseChanged` | SLSsteam spoof + lumalinux `package_zero_finder` | parity (finder more robust) |
| 2 PICS appinfo | ownership spoof + access-token wire forge | SLSsteam AppTokens (optional; LumaDeck writes none) | parity |
| 3 Depot surfacing | in-hook `LoadPackage` Package-0 append | lumalinux `package_zero_finder` (poll, dedup, cap) | **lumalinux** |
| 4 Manifest pin | `BuildDepotDependency` | lumalinux `depot_dependency_hook` (same fn) | parity |
| 5 Depot key | `LoadDepotDecryptionKey` (+ `ConfigStoreGetBinary`) | lumalinux `depot_key_hook` (same accessor; 2nd seam = Finding 3) | parity |
| 6 Manifest request code | **wire-layer** bridge, 3 providers (Finding 1) | **function-layer** hook, same 3 providers + shader coverage | **lumalinux** (layer-safe + coverage) |
| (shaders) | none | `shader_depot_hook` | **lumalinux** |

**Net:** LumaCore is the parent our shared gates were copied from, so gates 1–5
are parity-by-lineage, and on 3, 6 and shaders lumalinux has pulled ahead. The one
thing that **changed** since our last read is the runtime GMRC bridge (Finding 1),
at the wire layer — still not portable, but it makes two of our docs stale.
Everything else — Denuvo, tickets, online-fix, achievements, the wire rewrites —
is either a layer we delegate by architecture (SLSsteam / CloudRedirect) or a
Windows technique we solve differently (Steamless, Proton Override, netsock).

---

## Candidate shortlist (ranked)

1. **Fix the stale GMRC docs (Finding 1)** — update `method.md`
   §"Note on RESEARCH §11.5" and `RESEARCH.md` §11.5: LumaCore now has a
   **wire-layer** `GetManifestRequestCode` bridge (SFF `6.6.4`,
   `NetPacket_Manifest.cpp` + `ManifestFetch.cpp`); reframe lumalinux's edge as
   mechanism (function-layer, SLSsteam-safe) + coverage (shader), not existence.
   **Cheap, do it.**
2. **File the second-seam depot-key issue (Finding 3)** — evidence-gated, with the
   instrumentation test as its acceptance criterion. Don't implement blind.
3. **Add a boundary note to `FIXES_MAP.md` (Findings 5, 7)** — "why lumalinux has
   no DenuvoAuth / no online-fix payload": captures the coexistence boundary so a
   contributor doesn't reimplement LumaCore's IPC/CreateProcess machinery on
   Proton. Documentation only.
4. **One-liners in `rva-feed-design.md` (Finding 2)** — note the upstream
   byte-pattern ancestor is more fragile than our RVA payload, and that neither
   side signs the feed yet.

Everything else: parity-by-lineage, we-lead, or not-portable.

---

## Re-sweep 2026-08-17 — `6d2fb30` → `a26a359` (SteaMidra 6.6.5)

*Método: mismo clon de `Midrags/SFF`, leyendo todo lo posterior al commit de
referencia de este doc (`6d2fb30`, SteaMidra 6.6.4, 2026-08-11). **Un solo commit
nuevo**: `a26a359` "Release SteaMidra v6.6.5" (2026-08-16). SFF aplasta cada
release en un commit único, así que ese commit son 43.213 inserciones y 14.615
borrados repartidos por 37 ficheros.*

**Lo primero, y es lo que decide el resto: `LumaCore/` NO se ha tocado.**
`git show a26a359 --stat -- LumaCore` no devuelve nada. Todo el release está en el
launcher Python (`sff/`) y la web UI. **El análisis de este documento sigue
vigente sobre la capa C++ que analiza**; no hay que re-leer nada del hook.

### Dos pistas que parecían prestables y murieron al verificarlas

**1. `prewarm_pattern_cache_if_missing` (`sff/lumacore/lumacore_setup.py`).** SFF
ahora precalienta la caché de TOMLs de patrones al arrancar la app, desde Python,
para que la DLL no tenga que descargar. Lo perseguí porque `RvaFeed::obtainYaml`
hace un `Curl::getString` con timeouts por defecto de 15 s de conexión y 30 s
totales, y si eso corriera en el camino del cargador sería un bloqueo de arranque
de hasta medio minuto en cada build de Steam sin feed.

**No aplica: ya está fuera del camino crítico.** `main.cpp:428` — el constructor
lanza un `std::thread(...).detach()` que sondea por `steamclient.so` y sólo
entonces llama a `InstallHooks()`. La descarga del feed ocurre en ese hilo, no en
el cargador. Precalentar desde LumaDeck no ganaría nada.

**2. Fallback de versión vía `status.json`.** SFF ha añadido leer
`<Steam>/lumacore/status.json` → `lumacore_version` cuando su registro de
instalación no sabe la versión (cubre instalaciones manuales).

**No aplica: LumaDeck ya hace lo mejor.** `backend/paths.py:756-769` usa
`read_lumalinux_status()` → `status.get("version")` como fuente **primaria**, no
como plan B. SFF está poniéndose al día con algo que nuestro lado ya resolvió.

### Dos observaciones que sí conviene tener escritas

**3. SteaMidra es también un agregador de claves — nosotros no.** `[contexto, no
accionable]`

`sff/lua/fallback_depotkeys.json` pasa a ser un fichero de **64 MB con 368.230
entradas**, de las que **188.789 (51 %) llevan clave real**; el resto son depots
conocidos sin clave. Va empaquetado, offline.

Y en `sff/lua/provider.py:378-410` está el otro extremo: `_entry_fingerprint()`
genera una huella `id:key` por entrada, `_mark_fingerprints_sent()` la registra en
`sent_items` de `contributor_state.json`, y `reset_contributor_state()` existe
para que el usuario **reenvíe todas sus claves**. Es decir, las claves de los
usuarios se recolectan y se suben, con deduplicación por huella.

Eso explica de dónde salen 368k claves, y es una **diferencia de capacidad real,
no de técnica**: pueden resolver claves que nosotros no, porque tienen una base
alimentada por su parque de usuarios. Nuestro stack no tiene equivalente y este
doc no propone construirlo — se anota para que la diferencia no se lea como un
fallo técnico nuestro cuando es un modelo distinto.

**4. Filtro de atribución ofuscado: rechazan `.lua` que acrediten a DepotBox.**
`[contexto de ecosistema, sin impacto para nosotros]`

`sff/lua/provider.py:412-424`, `_filter_attributed_source()` — *"Reject scripts
that carry a third-party attribution banner"*. La cadena que busca **no está en
claro**: se reconstruye en runtime desde cinco fragmentos con tres claves XOR
distintas, uno invertido, otro con resta de 17 y otro con resta alterna, de forma
que un `grep` sobre el repo no la encuentra.

Decodificada:

```
f0='Down'  f1='loade'  f2='d us'  f3='ing D'  f4='epotBox'
→ "Downloaded using DepotBox"
```

**No nos afecta**: LumaDeck no escribe banners de atribución en los `.lua` que
produce (comprobado en `backend/downloads.py` — sólo mensajes de log con prefijo
`LumaDeck:`), así que nada de lo nuestro dispara ese filtro. Queda anotado por lo
que dice del ecosistema: hay proyectos que se bloquean entre sí por atribución y
esconden a quién.

### Neto del re-barrido

**Cero accionables.** La capa que este documento analiza no ha cambiado, las dos
pistas técnicas mueren al contrastarlas con nuestro código, y las dos
observaciones restantes son contexto — una explica una asimetría de datos, la otra
una práctica del ecosistema.

## References

- LumaCore (`Midrags/SFF` @ `6d2fb30`, `LumaCore/`): `docs/LumaCore.md`;
  `source/core/{entry,Orchestrator}.cpp`; `source/proxy/{LcDwmProxy,LcXInputProxy}.cpp`;
  `source/patterns/{PatternFetcher,ByteScan,PatternSig}.cpp`;
  `source/hooks/client/{DepotKeys,DecryptionKeyHook,ManifestBind,PackagePatch,LicenseHooks,CmdUser,IPCBus,DenuvoAuthenticator,SteamStubAuto,SteamStubTicket,OnlineFixInject,PacketRouter}.cpp`;
  `source/hooks/client/NetPacket*.cpp` (esp. `NetPacket_Manifest.cpp`, `NetPacket_UserStats.cpp`);
  `source/hooks/capture/RuntimeCapture.cpp`;
  `source/runtime/{ManifestFetch,EticketFetcher,ProtectionProbe,DirWatch}.cpp`;
  `source/config/{LuaBindings,LuaState,LuaLoader,Settings}.cpp`; `lumacore.toml.example`.
- lumalinux: `src/hooks/{depot_key_hook,depot_dependency_hook,gmrc_hook,shader_depot_hook,package_zero_finder,load_package_hook}.cpp`,
  `src/{patterns,rva_feed,vaddr_xlate,update,sha256,key_store,license_reconcile,sls_achievement_unblock,status,gmrc_store}.*`,
  `src/main.cpp`, `tools/steamidra_lite.py`; `docs/method.md`, `docs/RESEARCH.md` §11 + §15 + §17,
  `docs/rva-feed-design.md`, `docs/opensteamtool-findings.md`, `docs/slsteam-moon-findings.md`,
  `docs/slssteam-analysis.md`.
- SLSsteam (`AceSLS/SLSsteam`): `ticket.cpp`, `apps.cpp`, `Achievements::sendAndRecvGetUserStats`.
- CloudRedirect (`Selectively11/CloudRedirect`): cloud-save layer.
- LumaDeck: `backend/achievements.py`, `FIXES_MAP.md` (Steamless / Goldberg / netsock / Override).
