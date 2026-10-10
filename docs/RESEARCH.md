# lumalinux — Research log & internals

This document has two parts.

- **Part I (§1-§10) — how lumalinux works today**, at reverse-engineering depth:
  what each hook hooks, its signature and the structs it touches, how its target
  is located on the current Steam build, how Steam's install flow runs and where
  each piece acts. These sections are kept true to the current code; code is
  cited as `file:line`.
- **Part II (§11-§21) — the dated log**: how each piece was found, what broke and
  what was measured. Each of those sections describes the code **as it was on its
  date**; where it disagrees with Part I, Part I is current.

For the stack-wide description (lumalinux + LumaDeck + SLSsteam/CloudRedirect,
from the code, in Spanish) see [`nosotros.md`](nosotros.md); for the operational
runbook (Steam updates, re-derivation, triage) see
[`maintenance.md`](maintenance.md).

Provenance: the first build studied was
`~/.local/share/Steam/linux32/steamclient.so`, **ELF 32-bit i386**, ~48 MB,
BuildID `f92deb5ee064a2cf28977bd86a6ed43f420cfcba` (SteamOS, ~May 2026). Addresses
quoted below are labelled with the build they belong to. Nothing in the shipped
library depends on one build's addresses: every target is resolved at runtime by
the chain **RVA feed → byte pattern → rescue locator** (§4), and the per-build
constants live in `res/rvas/<sha256>.yaml` (published by CI) and
`src/patterns.hpp` (the compiled-in fallback).

## Index by topic

Where each piece is described as it works today (Part I), and where its history
is (Part II).

| Topic | Today (Part I) | History (Part II) |
|---|---|---|
| Goal, components, who provides what | §1, §2 | — |
| Steam's install flow and where each hook acts | §3 | §14 (auto-update, end to end) |
| Resolution chain: RVA feed → pattern → rescue | §4 (intro) | §13.5.a, §15 |
| DepotKey hook | §4 DepotKey | §11.3, §12 (crash and fix), §15 (RTTI rescue) |
| BuildDep hook (off by default) | §4 BuildDep | §11.4, §19.3-§19.6 (pinned-manifest model) |
| LoadPackage hook and the package-0 finder | §3, §4 LoadPackage | §11.2, §13.1-§13.7, §13.5.c (layout table) |
| GMRC hook | §4 GMRC, §7 | §11.5, §19, §20 |
| Request-code providers and the CDN check | §7 | §19 (providers die), §20 (they come back) |
| ShaderDepot hook (per-game shader skip) | §4 ShaderDepot | §13.8-§13.11, §19.1 |
| No-restart Add Game (license reconcile) | §4 Reconcile | §18 |
| Loading: wrapper, `LD_PRELOAD` vs `LD_AUDIT` | §5 | — |
| Patches over SLSsteam | §2, §4 Reconcile | §16 (update unblock, removed), §17 (achievement guard, `WriteRel32`) |
| CloudRedirect coexistence | §2, §5 | — (see [`cloudredirect.md`](cloudredirect.md)) |
| Re-deriving on a new Steam build | §8, §8.1 | §13.5.b, §15.2 |
| Per-game data (`tools/steamidra_lite.py`) | §9 | §19.4, §20.6 |
| DLC of an owned game | §9 | §21 |
| Dead-ends | §6 | §13.2, §13.9, §19.2b |
| Open items | §10 | §11.6 |

---

# Part I — How lumalinux works today

## 1. The goal

Make Steam's **native Install button** download and run a game you don't own
(first test case: Balatro, AppID 2379780), on a Steam Deck, **coexisting with
SLSsteam** (no fork, no patch of SLSsteam's files — lumalinux only adjusts it in
memory, see §2).

## 2. Division of labour (what each component provides)

| Concern | Provided by |
|---|---|
| Ownership spoof (app shows as owned, license checks pass) | **SLSsteam** (`CUser::CheckAppOwnership`, `GetSubscribedApps`, cached tickets). Which apps it spoofs comes from its `config.yaml` `AdditionalApps`, written by `steamidra_lite` / LumaDeck. lumalinux has no ownership hook (`main.cpp:21-22`) |
| Appinfo for unowned apps (so Steam sees the depot/manifest list) | **SLSsteam** ownership spoof (`CheckAppOwnership`) + lumalinux package-0 injection — these open it. SLSsteam's `AppTokens` (attached by `Apps::sendPICSInfoRequest`, eMsg 8903) are a **secondary** helper, needed only for the subset of games whose appinfo Valve gates behind a token; LumaDeck writes one only when the game's lua carries an `addtoken()` line |
| Family-share / offline bits | **SLSsteam** |
| Surface the content depots into the download plan | **lumalinux** package-0 finder (§3 step 3). The LoadPackage hook is an opt-in diagnostic, not installed by default (§4) |
| Pin each depot to the right manifest (gid) | **SLSsteam** `ManifestIds` (its own `BuildDepotDependency` hook; written by `steamidra_lite --pin-installed/--set-pin`). lumalinux's BuildDep hook is **off by default** (`LUMA_FORCE_BUILDDEP` turns it on, `main.cpp:175-186`) |
| Provide the depot AES decryption keys | **lumalinux** DepotKey — the one **critical** hook (`main.cpp:192-201`) |
| Provide the **manifest request code** (CDN download authorization) | **lumalinux** GMRC — on by default, not critical: a manifest pre-seeded in `depotcache/` needs no code |
| Skip the shader pre-cache when it cannot succeed (per game, not global) | **lumalinux** ShaderDepot |
| Refresh licenses after a game is added, without a Steam restart | **lumalinux** Reconcile (`CUser::NotifyLicensesUpdated`, called, not hooked) + the `keys.txt` watcher |
| Coexistence fixes outside the install path | **lumalinux** in-memory patches `sls_achievement_unblock` (scopes SLSsteam's native-achievement guard, §17) and `cr_stats_fix` (CloudRedirect stats sync, [`cloudredirect.md`](cloudredirect.md)) (`main.cpp:303-340`) |

SLSsteam gets you ownership + appinfo. lumalinux gets you the actual bytes.

## 3. The Steam content-install flow (and where each hook sits)

(For the high-level, cross-tool version of this — the "six gates", the two
download models, and a side-by-side with LumaCore/SteaMidra and
SteamTools/OpenSteamTool — see [`nosotros.md`](nosotros.md). This section is the
lumalinux-specific, function-level detail.)

Clicking Install kicks off, roughly:

1. **Ownership ticket** (eMsg 858, `CMsgClientGetAppOwnershipTicketResponse`).
   SLSsteam fakes ownership. The content log still prints
   `failed to update ownership ticket (Access Denied)` — this is **non-fatal**;
   the install proceeds.
2. **PICS appinfo** — Steam fetches the app's product info (depot list, manifest
   ids). What opens this for an unowned app is SLSsteam's **ownership spoof**
   (`CheckAppOwnership`) plus the **package-0 injection** — Steam then queries
   the appinfo through its normal handshake. An access token (SLSsteam
   `AppTokens`) is needed only for the games Valve gates behind one; for the
   rest it is not the load-bearing piece. (LumaCore's source states the same:
   ownership is established by the package-0 `AppIdVec` injection +
   `CheckAppOwnership`.) The depot **list** itself has to come from this
   appinfo: lumalinux **cannot inject depots** (doing so SIGSEGVs Steam — see
   §6); everything it adds (key, request code, shader decision) is
   **downstream** of Steam already knowing the depot list.
3. **`PackageId == 0`** — the implicit "free apps everyone owns" package, which
   Steam's per-depot license filter consults. Our depot ids must be in its
   `AppIdVec` or the content depots are dropped (→ "0 target depots" → instant
   "Fully Installed"/Play). → **package-0 finder**
   (`src/hooks/package_zero_finder.cpp`), not a hook but a thread started at
   install time (`main.cpp:284-291`), and only if DepotKey installed — without
   keys a forced download cannot complete, so nothing is injected
   (`main.cpp:267-283`). It:
   - resolves the global that holds the `CPackageInfoCache*`:
     `cache_global = GOT + X`. The GOT base comes from the feed
     (`finder.got_rva`) or, failing that, from the GMRC prologue tail
     `05 <imm32>; 55 89 E5 57 56 53 81 EC ?? ?? 00 00 8B 7D 08 8B 4D 20`
     (`GOT = addr(05) + imm32`, all sites must agree,
     `package_zero_finder.cpp:196-279`); `X` comes from the feed
     (`finder.cache_global_disp`) or from scanning for the cache-access idiom
     `lea r1,[GOT+X]; mov r2,[r1]; mov r3,[r2+<root_off>]`, unique or nothing
     (`:385-437`);
   - walks the index-based search tree (root index at `cache+0xc58`, node array
     at `cache+0xc6c` on the stable layout, `0xf90/0xfa4` on the 9cf4720f beta;
     nodes 0x18 bytes: left `+0x00`, right `+0x04`, packageId `+0x10`,
     `PackageInfo*` `+0x14`; `:24-76`) down to key 0, and refuses the object
     unless its own `PackageId` (`+0x00`) is also 0 (`:486-560`);
   - each pass first **retires** the ids it injected that are no longer in
     `keys.txt`, then **appends** `KeyStore::GetAllDepotIds()` to `AppIdVec`
     (`+0x38`) in place (`:806-807`; mechanics in §4 LoadPackage);
   - polls every 2 s until package 0 first appears, then every 15 s forever
     (re-injects if Steam rebuilt the package), and wakes at once when
     `keys.txt` changes (`:139-140, 827`).

   How the passive hook was replaced by this walker: §13.

   **No-restart Add Game.** When `keys.txt` changes while Steam runs, the
   watcher reloads the keys and flags the change; the finder, right after
   re-injecting, calls **`CUser::NotifyLicensesUpdated`** (→ **Reconcile**, §4)
   so Steam re-reads licenses and appinfo for the newly-owned depots without a
   restart (`package_zero_finder.cpp:814-817`).
4. **`CUserAppManager::BuildDepotDependency(...)`** — builds the depot list for
   the app (`pDepotInfo`, `pSharedDepotInfo`, each a `CUtlVector<DepotEntry>`),
   applying the per-depot license filter that step 3 satisfies. Version pinning
   happens here, by **SLSsteam**: its own detour on this function applies
   `ManifestIds`. lumalinux's **BuildDep hook** (PATCH the `ManifestGid` of our
   depots, never inject — dead-end §6) is off by default (§4).
5. **`LoadDepotDecryptionKey`** — Steam asks for each depot's AES key; the
   dispatcher builds the KeyValues path
   `Software\Valve\Steam\Depots\<depot>\DecryptionKey` and reads it through an
   inner accessor. Valve has none for unowned depots. → **DepotKey hook** on that
   accessor: serve the 32-byte key from `keys.txt`. (config.vdf can't hold these
   — see §6.)
6. **`…BYieldingGetManifestRequestCode(...)`** — when the manifest is **not**
   already in `depotcache/`, Steam asks Valve for a per-manifest **request code**
   that authorizes the manifest download from the CDN. Valve denies for unowned
   (`Failed to get manifest request code, 'Access Denied'` → surfaced to the UI
   as "No connection"). → **GMRC hook**: fetch the code from the provider
   cascade and return it. Order (`src/gmrc_store.hpp:154-159`): 20770407 →
   manifestdex → wudrm → steamrun; a code reaches Steam only after Valve's CDN
   accepted it (`CdnAcceptsCode`, `:296-323`). With the manifest pre-seeded by
   LumaDeck/`steamidra_lite` Steam never asks, so a missing GMRC hook or a dead
   cascade costs the native manifest fetch and the keyed shader pre-cache, not
   the install (`main.cpp:192-200`). Provider history: §7, §19, §20.
7. Manifest downloads from the CDN (authorized by the code, or read from
   `depotcache/`), chunks download by SHA, are decrypted with the depot key,
   committed to `steamapps/common/<game>`. Done.
8. **Shader pre-cache**, a separate job (`CGetShaderDepotManifestJob`) that asks
   `GetShaderCacheDepot(appinfo)` for the shader depot id (== app id) and, if
   non-zero, downloads and decrypts that depot's manifest — which needs both its
   key and a request code. → **ShaderDepot hook**: return 0 (Steam's own clean
   "skipping because shader depot ID is invalid") when the pre-cache cannot
   succeed.

## 4. The hooks (functions, signatures, how found)

**Install mechanics.** Every detour goes through `LmHook::Install`
(`src/lmhook.cpp:129-162`): libmem `LM_HookCode`, then one of two trampoline
fixups, chosen by the target's **original** first byte:

- PIC prologue (the normal case): `FixPicThunk` (`:29-80`, port of SLSsteam's
  `MemHlp::fixPICThunkCall`) finds the relocated `call __i686.get_pc_thunk.REG`
  in the trampoline and replaces it with `mov REG, <return address in the
  original>`, any register.
- Prologue already starting with `E9` (another library detoured it first, e.g.
  SLSsteam): `RelocateChainedJmp` (`:97-123`) re-computes the copied `jmp
  rel32` so the trampoline still lands on the other hooker's target.

Hooks install from `InstallHooks()` (`main.cpp:87-365`), reached from the
`LD_PRELOAD` constructor once `steamclient.so` is mapped (§5). The SafeMode hash
check against `res/updates.yaml` is **advisory** — a build not in the list only
raises a toast (`main.cpp:120-127`); the real gate is per hook.

| Piece | Installed | Kill-switch | If it fails |
|---|---|---|---|
| DepotKey | always | `LUMA_NO_DEPOTKEY` | **critical**: the finder is not started, nothing is injected, Steam behaves vanilla (`main.cpp:267-283`) |
| ShaderDepot | always | `LUMA_NO_SHADERSKIP` | keyless shader pre-cache runs and fails (§13.8) |
| GMRC | always (since v0.21.0) | `LUMA_NO_GMRC` | no native manifest fetch; installs from pre-seeded manifests |
| BuildDep | only with `LUMA_FORCE_BUILDDEP` | `LUMA_NO_BUILDDEP` | reported `DISABLED` when not forced (`main.cpp:229-234`) |
| LoadPackage | only with `LUMA_LOADPKG_DEBUG=1` | `LUMA_NO_LOADPKG` | diagnostic only |
| Reconcile | resolved, not hooked | `LUMA_NO_RECONCILE` or `~/.config/lumalinux/no_reconcile` | add-without-restart needs a restart |
| package-0 finder | thread, if DepotKey is active | `LUMA_NO_PKG0_FINDER` | no depot surfacing |

Each outcome (`INSTALLED`/`DISABLED`/`FAILED`) goes to
`$XDG_RUNTIME_DIR/lumalinux/status.json` for LumaDeck (`status.cpp:33-51`), and
to the log as a machine-readable line
`Hook install: name=… method=rva|pattern|byname(rescue)|xref(rescue) … outcome=installed|miss|hook_install_failed`
(`Finder resolve: … outcome=resolved|miss` for the finder).

**How each target is located today.** Each target has a chain of resolvers;
each step runs **only** if the previous one returned 0 (resolution runs while
Steam starts, so nothing is computed that cannot change the outcome —
`depot_key_hook.cpp:134-137`), and every step is unique-or-nothing except where
noted:

| Target | 1. RVA feed key | 2. Byte pattern (`src/patterns.hpp`) | 3. Rescue |
|---|---|---|---|
| DepotKey | `DepotKey` | `kDepotKeyFnPattern` (`:40-41`), unique | RTTI by name: `21IClientConfigStoreMap` / `GetBinary` → `12CConfigStore` |
| GMRC | `GMRC` | `kGmrcFunctionPattern` (`:150-151`), unique | string xref `ContentServerDirectory.GetManifestRequestCode#1` + `.eh_frame_hdr` (walk-back fallback) |
| ShaderDepot | `ShaderDepot` | `kShaderCacheDepotPattern` (`:196-197`), unique | string xref `shadercachedepot` + `.eh_frame_hdr` |
| BuildDep | `BuildDep` (CI never publishes it) | `kBuildDepotDependencyPattern` (`:62-63`), **first match** (`patterns.cpp:210-212`) | string xref `BuildDepotDependency` + `.eh_frame_hdr` |
| Reconcile | `Reconcile` | `kNotifyLicensesUpdatedPattern` (`:227-228`), unique | callback-125 anchor |
| LoadPackage | — | `kLoadPackagePattern` (`:115-116`), candidate by index | — |
| finder | `finder.got_rva`, `finder.cache_global_disp`, `finder.cache_root_off/cache_nodes_off` | GMRC prologue tail (GOT, consensus) + cache-access idiom scan | — |

- **RVA feed** (`src/rva_feed.cpp`). At first use the library SHA-256-hashes
  the on-disk `steamclient.so`, fetches
  `https://raw.githubusercontent.com/jayool/lumalinux/main/res/rvas/<sha256>.yaml`
  (accepted only if the body contains `hooks:`), caches it in
  `~/.cache/lumalinux/rvas/` and falls back to that cache offline
  (`:62-72`). The URL is hardcoded; there is deliberately no env override, since
  the feed decides where detours land (`:52-61`). Each RVA is a file vaddr
  (image base 0), translated by `VaddrXlate` (ELF `PT_LOAD` → file offset →
  `/proc/self/maps` offset column, correct under split mappings) and rejected
  unless it lands in an `r-x` mapping of `steamclient.so` (`:229-250`). The
  finder's values are read independently of `hooks:`; `got_rva` is translated and
  checked against any mapping (`.got` is not executable), `cache_global_disp`
  and the layout offsets are used as plain numbers (`:186-227`). The files are
  written by CI (`tools/check_patterns.py --emit-rvas` in
  `.github/workflows/watch-steam.yml`) only for non-BLOCKING builds and only for
  targets it resolved uniquely; a commit there needs no release. Example,
  build `bc54101b…`: DepotKey `0x11a4500`, ShaderDepot `0x1048840`, Reconcile
  `0x188c950`, GMRC `0x1371ac0`, finder disp `0x3b7d4`, layout `0xc58/0xc6c`,
  GOT `0x2f4a34c`. Except for Reconcile (below), there is no runtime cross-check
  of the feed against the pattern; that belongs to CI.
- **Byte pattern** (`src/patterns.cpp`). Scanned over the aggregated `r-x`
  span of `steamclient.so`. `FindUniqueInSteamclient` (`:132-166`) refuses 0 or
  ≥2 matches, so an ambiguous pattern falls to the next resolver instead of
  detouring a plausible wrong address. BuildDep alone still takes the first
  match (`FindInSteamclient`, `:94-118`); LoadPackage enumerates all candidates.
- **String xref** (`src/gmrc_xref.cpp:81-158`, core in
  `src/gmrc_xref_core.hpp`): (1) find the anchor string in a readable mapping
  (with its NUL terminator for `FindFunctionByString`, so a longer string cannot
  match first); (2) derive the module GOT base by consensus over PIC preambles
  (`call get_pc_thunk; add reg,imm32`); (3) find the **unique**
  `lea reg,[base + (S − GOT)]` in `.text`; (4) map that site to its function
  entry with `.eh_frame_hdr`'s sorted function-start table (`EhFrame::FindFunction`,
  `src/eh_frame.hpp`) — exact, and it rejects an address inside no function.
  Only GMRC keeps a fallback for a build with no usable table, the backward
  `WalkBackToPrologue` scan, whose known failure modes are documented at
  `gmrc_xref.hpp:32-51`; ShaderDepot and BuildDep refuse instead.
- **RTTI by name** (`Rtti::ResolveVtableSlotByName`, `src/rtti.cpp:289`): find
  the method-name string, find which slot of the interface-map class's vtable
  references it (on overloads, the **lowest** slot — `GetBinary` sits in map
  slots 6 and 7, and 7 is a different function), then read that slot from the
  concrete class's vtable, located by its RTTI type name. Reads no byte of the
  target, so it survives a prologue change that defeats feed and pattern
  together. On `bc54101b`: slot 6 → `0x11a4500`. How this was adopted: §15,
  [`slssteam-plugins.md`](slssteam-plugins.md).
- **Callback-125 anchor** (`src/reconcile_anchor_core.hpp:101-138`): see
  Reconcile below.

Re-deriving a pattern or feed value on a new build is CI's job (`check_patterns.py`,
`tools/derive_python_first.sh` with Ghidra as second opinion); the technique is
§8, the procedure is `maintenance.md`.

### DepotKey — `LoadDepotDecryptionKey` (inner KeyValues accessor, v1.0)
- `int32_t LoadDepotDecryptionKey(void* pObject, uint32 foo, char* KeyName, char* Key, uint32 KeySize)`
  — cdecl, 5 stack args (`depot_key_hook.cpp:36-38`). `foo` is the KeyValues
  value-type selector (1 for binary keys); returns bytes written (32) or 0.
- This is the INNER KeyValues accessor — `CConfigStore`'s `GetBinary` (vtable
  slot 6 on the builds checked), the function LumaCore hooks — NOT the outer
  dispatcher. `KeyName` = `"Software\Valve\Steam\Depots\<depot>\DecryptionKey"`.
- Located by: feed `DepotKey` → `kDepotKeyFnPattern`
  (`55 57 56 53 E8 ?? ?? ?? ?? 81 C3 ?? ?? ?? ?? 83 EC 24 8B 44 24 44 8B 6C 24 38 8B 7C 24 3C 8B 74 24 40 89 44 24 10 8B 44 24 48 89 44 24 14`:
  push run, `get_pc_thunk.bx`, `sub esp,0x24`, the five-argument load sequence),
  unique → RTTI by name (`depot_key_hook.cpp:138-179`). Current address:
  `res/rvas/<sha>.yaml` (`0x11a4500` on `bc54101b`).
- Hook (`depot_key_hook.cpp:74-106`): parse the depot id out of `KeyName`
  (digits between the backslash and `\DecryptionKey`); if `KeyStore::Lookup`
  has a key and `KeySize >= 32`, `memcpy(Key, key, 32); return 32`; otherwise
  call the original. Presence-only (keyless) `keys.txt` entries deliberately
  fall through — a zero key would fail to decrypt the shader depot's manifest;
  ShaderDepot handles those games instead (`:89-98`).
- WHY this function and not the outer dispatcher: hooking the dispatcher and
  short-circuiting it corrupts Steam's heap on owned depots; answering the
  KeyValues query lets the dispatcher run its normal path for every depot, owned
  or not. How this was found: §12.

### BuildDep — `CUserAppManager::BuildDepotDependency`

> **Off by default.** It is installed only with `LUMA_FORCE_BUILDDEP`
> (`main.cpp:175-186`); otherwise `status.json` records it `DISABLED`. SLSsteam
> (≥ 20260714) detours `BuildDepotDependency` itself for `ManifestIds` /
> `DepotBlacklist` and is loaded first (`LD_AUDIT`, §5), so the prologue our
> pattern expects is SLSsteam's jump, and version pinning is SLSsteam's
> `ManifestIds`. The hook only patched gids and was inert in the no-pin flow, so
> nothing is lost. The rest of this subsection describes what it does when
> forced.

- 8 args, cdecl (`depot_dependency_hook.cpp:16-24`, `patterns.hpp:52-61`):
  `bool BuildDepotDependency(void* this [ebp+0x08], uint32 AppId [+0x0C], void* pUserConfig [+0x10], CUtlVector<DepotEntry>* pDepotInfo [+0x14], CUtlVector<DepotEntry>* pSharedDepotInfo [+0x18], void* pSteamApp [+0x1C], uint32* pBuildId [+0x20], bool* pbBetaFallback [+0x24])`.
- `DepotEntry` = 32 bytes: `{u32 DepotId; u32 AppId; u64 ManifestGid; u64 ManifestSize; u32 DlcAppId; u8 LcsRequired; u8 bNotNewTarget; u8 SharedInstall; u8 pad}` (`steam_types.hpp:28-39`, size pinned by `static_assert`).
- `CUtlVector<T>` = 16-byte header `{T* m_pMemory; int m_nAllocationCount; int m_nGrowSize; u32 m_Size}` (`steam_types.hpp:17-24`).
- Located by: feed `BuildDep` (CI never publishes it) → `kBuildDepotDependencyPattern`
  (`55 89 E5 57 56 E8 ?? ?? ?? ?? 81 C6 ?? ?? ?? ?? 53 81 EC 2C 02 00 00 8B 45 08 89 85`,
  ESI as PIC base), first match → string xref `"BuildDepotDependency"`
  (`depot_dependency_hook.cpp:151-169`). With SLSsteam loaded the pattern cannot
  match; if the xref resolves the entry, `LmHook` sees the `E9` and chains
  through SLSsteam's detour (`RelocateChainedJmp`).
- Hook (`:84-145`): call the original; return at once if it returned false
  (the vector may be half-built). For the depots `keys.txt` lists for `AppId`,
  overwrite `ManifestGid` in **`pDepotInfo` only** when the stored gid is
  non-zero and differs. `ManifestSize` is left as Steam computed it (LumaCore
  forces size 0 for the same reason), `pSharedDepotInfo` is never touched
  (shared redists belong to their own app; overwriting them corrupted the heap,
  §11.4), and nothing is ever injected (§6).

### LoadPackage — `CPackageInfoCache::LoadPackage`
- `bool LoadPackage(PackageInfo* pInfo, uint8 sha1[20], int32 cn, void* p4)` (`load_package_hook.cpp:16`).
- `PackageInfo` (Linux i386, from LumaCore's `Structs.h`; `load_package_hook.hpp:69-85`):
  `+0x00 PackageId`, `+0x04 ChangeNumber`, `+0x08 u64 PICS_token`,
  `+0x10 BillingType`, `+0x14 LicenseType`, `+0x18 Status`, `+0x1C SHA_1_Hash[20]`,
  `+0x30 pPackageInfoNodeBegin`, `+0x34 pExtendNodeBegin`,
  **`+0x38 AppIdVec`** (`CUtlVector<AppId_t>`), `+0x48 DepotIdVec`.
  Only `PackageId` and `AppIdVec` are used. No tool verifies these offsets on a
  new build; the finder's `PackageId` cross-check is what stops a write through
  a wrong one (`package_zero_finder.cpp:86-99, 546-560`).
- **Not installed by default.** Only `LUMA_LOADPKG_DEBUG=1` adds it
  (`main.cpp:187-189`), and then it only logs `PackageId` + `AppIdVec{mem,size,alloc}`
  for the first 400 calls (`load_package_hook.cpp:40-53`). It never injects:
  it cannot see a `PackageId=0` that Steam keeps cached and never reloads, which
  is why the finder exists (§13).
- Located by: `kLoadPackagePattern`
  (`55 89 E5 57 E8 ?? ?? ?? ?? 81 C7 ?? ?? ?? ?? 56 53 81 EC 1C 01 00 00`) only —
  no feed, no rescue. It matches several sites (three on the builds noted at
  `patterns.hpp:94-98`); all are logged and index 0 is used, overridable with
  `LUMA_LOADPKG_IDX` (`patterns.cpp:231-276`). The anchor strings near it belong
  to `CPackageInfoCache::GetPackage`, not to this function (§13.2).
- The injection the finder performs lives in this file and is shared code
  (`load_package_hook.cpp:77-251`):
  - `InjectDepots` appends `KeyStore::GetAllDepotIds()` (the depot ids,
    LumaCore's `GetAllDepotIds` equivalent — not the app id) to `AppIdVec`.
  - `AppendIdsToVec` bails if `m_Size > 4096` or if any of the first 8 existing
    entries is 0 or > 50 000 000 (offset wrong on this build; real entries look
    like `{5,7,8,90,…}`), filters out ids already present (idempotent), and grows
    with raw `std::realloc` — first allocation ≥ 32, then doubling — because
    `CUtlMemory` is malloc-backed on Steam Linux i386 (verification: §11.2).
  - `RetireDepots` compacts out of `AppIdVec` only the ids this process
    injected that are no longer in `keys.txt`; Steam's own entries are never
    touched.
  Only the finder thread writes the vector, so no lock is needed.

### GMRC — `…BYieldingGetManifestRequestCode` (the key find)
- On `f92deb5e`: Ghidra name `FUN_012d3bd0`, **file vaddr 0x12c3bd0** (Ghidra
  image base 0x10000 → Ghidra addr 0x12d3bd0). Current address:
  `res/rvas/<sha>.yaml` (`0x1371ac0` on `bc54101b`).
- Signature (cdecl, i386; `gmrc_hook.cpp:14-20`):
  `int32 GetManifestRequestCode(void* this, u32 app_id, u32 depot_id, u32 manifest_lo, u32 manifest_hi, char* branch, uint64* out_code)`
  — args at `[ebp+0x08 .. +0x20]`; `out_code` at `[ebp+0x20]`.
- Decompiled core (`f92deb5e`):
  ```c
  cVar3 = (**(code **)(*piVar6 + 0x10))(
            piVar6, "ContentServerDirectory.GetManifestRequestCode#1",
            &request, &response, 0);
  if (cVar3 == '\0') { return error; }          // server denied (Access Denied)
  else { *out_code = *(u64*)(response + 0x10);   // success: write the code
         return 1; }
  ```
  The job-name string in that call is the anchor of the xref rescue.
- Located by: feed `GMRC` → `kGmrcFunctionPattern`
  (`E8 ?? ?? ?? ?? 05 ?? ?? ?? ?? 55 89 E5 57 56 53 81 EC ?? ?? ?? ?? 8B 7D 08 8B 4D 20`),
  unique → job-name xref (`gmrc_hook.cpp:74-95`). The `sub esp,imm32` frame size
  is wildcarded: it was 0x110 up to `bc54101b` and 0x120 on the `9cf4720f` beta,
  locals only (`patterns.hpp:143-147`).
- Hook (`gmrc_hook.cpp:23-59`): `manifest_gid = manifest_lo | (manifest_hi << 32)`.
  It intervenes if the gid is registered in `keys.txt`
  (`KeyStore::HasManifestGid`) **or** the depot is ours (`KeyStore::HasDepot`
  — covers the shader depot, whose gid comes from PICS, not from the lua). Then
  `Gmrc::GetCode(depot, gid)` runs the provider cascade (§7, `gmrc_store.hpp:334-417`):
  per-gid cache, paced retries, dead providers skipped for a while, and a code
  returned only after `CdnAcceptsCode` got HTTP 200/206 for
  `/depot/<d>/manifest/<gid>/5/<code>`. On a code: write `*out_code`, return 1.
  Otherwise fall through to the original (a manifest already in `depotcache/`
  needs no code; otherwise the CDN denies it).
- Uses `get_pc_thunk.ax` (PIC, eax-based); `FixPicThunk` handles it. The same
  prologue (`05 <imm32>` after the thunk call) is where the finder derives the
  GOT when the feed has no `got_rva` (§3 step 3) — it scans the tail after the
  `05`, which survives our own 5-byte detour.

### ShaderDepot — `GetShaderCacheDepot` (per-game shader skip, v0.14)
- `uint32_t GetShaderCacheDepot(void* appinfo)` — cdecl, 1 stack arg
  (`CShaderCacheManager::GetShaderCacheDepot`). Reads the game's PICS appinfo
  KeyValues `appinfo.game.shadercachedepot` and returns that depot id (== app id
  by convention), or 0 if the app isn't a game / has no shader depot. File vaddr
  0xfd9ca0 on `7c4ac73e`; current address in `res/rvas/<sha>.yaml`
  (`0x1048840` on `bc54101b`).
- **Sole caller** is `CGetShaderDepotManifestJob::BYieldingRunClientJob`: when it
  gets 0 back it logs `skipping because shader depot ID is invalid` and ends the
  shader pre-cache job cleanly (no error, no install pause, no retry loop).
- Located by: feed `ShaderDepot` → `kShaderCacheDepotPattern`
  (`57 56 53 E8 ?? ?? ?? ?? 81 C3 ?? ?? ?? ?? 8B 83 ?? ?? ?? ?? 8B 40 44 85 C0 75 0D 5B 31 C0`),
  unique → string xref `"shadercachedepot"` (`shader_depot_hook.cpp:102-119`).
  The `mov eax,[ebx+disp32]` that loads the shader-manager global
  (`0x2b758` on `7c4ac73e`) is a GOT-relative offset that shifts per build and is
  **wildcarded**; the fixed anchors are the `57 56 53` prologue and the
  `8B 40 44 85 C0 75 0D 5B 31 C0` tail (`patterns.hpp:179-195`).
- Hook (`shader_depot_hook.cpp:57-96`): call the original, then decide per game:
  - original returned 0 → 0;
  - **keyless** (`KeyStore::IsPresenceOnly` — in `keys.txt` with no key) → 0:
    the pre-cache could never decrypt;
  - **not ours** (`!KeyStore::HasDepot`) → the real id; owned games keep their
    shaders;
  - **keyed, ours** → the job will need a request code for the shader manifest
    (Hubcap zips never carry it): the real id only if the GMRC hook is installed
    (`Hooks::Gmrc::Active()`) **and** a provider answers right now
    (`Gmrc::ProvidersReachable()`: one paced request for depot 1 / gid 1 with
    short timeouts, cached 15 s, `gmrc_store.hpp:433-472`); otherwise 0, which
    avoids Steam's "No internet connection" popup and 30 s stall.

  This replaced the global `DisableShaderCache`. History: §13.8 (the loop),
  §13.9 (path B), §13.10 (this hook), §13.11 (the provider gate).

### Reconcile — `CUser::NotifyLicensesUpdated` (called, not hooked)
- `void NotifyLicensesUpdated(CUser* this)` — cdecl, one arg
  (`license_reconcile.cpp:30`). Rebuilds the `LicensesUpdated_t` callback
  (callback 125 = `0x7d`, `k_iSteamUserCallbacks + 25` in the public SDK) from
  this user's license vector and posts it, so Steam re-reads ownership and
  appinfo. No detour is installed: lumalinux **calls** the resolved address.
- Located by (`license_reconcile.cpp:41-63`): feed `Reconcile` (if the pattern
  also resolves to a different address, a drift warning is logged and the feed
  wins) → `kNotifyLicensesUpdatedPattern`
  (`55 89 E5 57 56 53 E8 ?? ?? ?? ?? 81 C3 ?? ?? ?? ?? 81 EC ?? ?? ?? ?? 8B 45 08 8B ?? ?? ?? 00 00 89 9D ?? ?? FF FF 85 ??`;
  thunk, GOT imm, frame size, the `CUser` member offset, the spill offset and
  the registers of the member load/test are masked, `patterns.hpp:204-228`),
  unique → callback-125 anchor (`reconcile_anchor.cpp`): (c1) a
  `push 0x7d; push eax; call rel32` site (`6A 7D 50 E8`, ~11 functions per
  build), (c2) whose function, bounded by `.eh_frame_hdr`, first reads a field
  of `this` (`mov eax,[ebp+8]; mov r,[eax+disp32]`, disp free), (c3) then bails
  with `test r,r; jle` within 16 bytes. Exactly one function or 0. Addresses:
  `0x188c950` on `bc54101b`, `0x1a0d010` on `9cf4720f`.
- When it fires: from the finder thread, after an inject/retire pass, if the
  `keys.txt` watcher flagged a change (`package_zero_finder.cpp:814-817`). The
  `CUser*` is captured by `sls_ach_combined_guard`, the replacement that
  `sls_achievement_unblock` points SLSsteam's achievement-guard call sites at
  (`sls_achievement_unblock.cpp:60-64` → `LicenseReconcile::SetUser`); without
  SLSsteam (or that patch), or before the guard has run once, the call is
  skipped and a restart is still needed.
- Status: `Reconcile` = `INSTALLED` (resolved) / `FAILED` (unresolved; LumaDeck
  then holds back the premature library appearance) / `DISABLED` (kill-switch)
  (`main.cpp:349-356`). How it was ported from moon and validated: §18.

## 5. Loading: LD_PRELOAD, not LD_AUDIT (important)

- lumalinux is a **32-bit** lib hooking the 32-bit `steamclient.so`.
- Setting it in **LD_AUDIT** (where SLSsteam lives) loads it into a separate
  linker namespace and **corrupts the heap** — Steam dies on startup with
  `realloc(): invalid pointer`. (And at the 64-bit launcher level it's rejected
  with `wrong ELF class: ELFCLASS32` anyway.)
- Setting it in **LD_PRELOAD** loads it in the normal namespace → works. The
  `__attribute__((constructor))` (`main.cpp:429-469`) runs only in the `steam`
  and `steamwebhelper` processes (`proc_filter.cpp:25-31`, fail-open if
  `/proc/self/comm` can't be read), loads `keys.txt`, starts the `keys.txt`
  watcher (unless the Reconcile kill-switch is set), and polls `/proc/self/maps` once a second (up to 300 times) until
  `steamclient.so` is mapped, then calls `InstallHooks()`. The `la_version` /
  `la_preinit` / `la_objopen` audit entry points are still compiled
  (`main.cpp:393-419`) but are not the supported path.
- **Injection point: the `setup.sh` wrapper.** `setup.sh` writes
  `~/.local/share/SLSsteam/path/steam` (`setup.sh:1150-1202`), which resolves the
  real Steam launcher (`LUMA_STEAM_BIN`, else `PATH` skipping itself, else
  `/usr/bin/steam` and friends), runs the shared crash-loop guard, sets the loader
  environment and `exec`s Steam. The environment (`luma_set_injection_env`,
  `setup.sh:884-898`):
  - `LD_AUDIT=library-inject.so:SLSsteam.so`, or `SLSsteam.so` alone when
    `library-inject.so` is empty (as upstream ships it). SLSsteam therefore
    hooks before lumalinux's constructor runs — which is why
    `BuildDepotDependency` already starts with SLSsteam's jump (§4 BuildDep).
  - `LD_PRELOAD=liblumalinux.so:cloud_redirect.so` — lumalinux **first**, on
    purpose: `cr_stats_fix` interposes a CloudRedirect symbol and must precede it
    (`cr_stats_fix.hpp:28`). CloudRedirect never goes in `LD_AUDIT` (it corrupts
    the client heap, `setup.sh:1154-1156`). Under Wayland `libextest.so` is
    appended.

  The wrapper is reached from the rewritten `Exec=` of the Steam `.desktop`
  entries plus a `PATH` drop-in (Desktop), and from a systemd drop-in on
  `steam-launcher.service` (Game Mode); both go through the same guard, which
  launches Steam vanilla (`env -u LD_AUDIT -u LD_PRELOAD`) after repeated startup
  crashes. Details: `nosotros.md` §2.1.
- **`steam.sh` stays vanilla.** Steam re-extracts it whenever its size differs
  from its manifest, so an injection there does not survive Steam updates;
  `setup.sh` restores a vanilla `steam.sh` if Headcrab or the old installer had
  modified it (`neutralize_steam_sh`, `setup.sh:264-333`). `/usr/bin/steam` is
  not touched either.
- `install.sh` is the legacy installer (it inserted an `LD_PRELOAD` export into
  Headcrab's `~/.local/share/Steam/steam.sh`). It is still in the repository and
  the build artifact, but neither `setup.sh` nor LumaDeck uses it.
## 6. Dead-ends & lessons (so we don't repeat them)

- **config.vdf depot keys get pruned.** Writing `DecryptionKey` into
  `config.vdf` works for owned depots, but Steam **deletes** the entries for
  unowned depots on shutdown (`grep -c DecryptionKey` dropped from 3 to 1 across
  a restart). → keys must be served at runtime (DepotKey hook), not via config.
- **BuildDep injection crashes Steam.** Injecting depots into `pDepotInfo` that
  aren't in Steam's appinfo causes a SIGSEGV later (virtual call on a null/bad
  pointer in the appinfo path). → never inject in BuildDep; **surface** the
  depots via LoadPackage (PackageId=0) and only **patch** in BuildDep.
- **LoadPackage must inject DEPOT ids, not the app id.** Injecting the parent
  app id (2379780) into PackageId=0 → depots still dropped ("0 target"). The
  per-depot license filter checks the depot ids; inject those (228989, 2379781,
  2379782) and they survive. (This matches LumaCore's `GetAllDepotIds`.)
- **Manual `m_pMemory` swap on `AppIdVec` crashed** the appinfo-cache rebuild
  thread (null deref in `libvstdlib_s.so`) in ≤v0.7. Cause: a hand-rolled
  malloc-and-swap that didn't match Steam's allocator contract. Resolved in
  v0.8 once we disassembled the only 5 `realloc@plt` callers in
  `steamclient.so` and verified that Linux Steam's `CUtlMemory` uses **raw libc
  `realloc`** with no tier0 wrapper — so calling `std::realloc(m_pMemory, …)`
  from outside is safe and is now what we do (see §11.2). The "append in place
  only" claim from the early notes is obsolete; the finder grows the vector
  with realloc when needed.
- **`InitFromPacket` is templated per message type.** `CProtoBufMsgBase<T>::
  InitFromPacket` has one instantiation per protobuf message; hooking the one
  SLSsteam uses (for 858 etc.) does **not** see the GMRC service-method response
  (a heartbeat showed our hook ran <513 times total and never on GMRC). → don't
  try to intercept GMRC at the packet layer via InitFromPacket.
- **The convar `@bClientTryRequestManifestWithoutCode` is not enough.** There's a
  real ConVar (object RVA 0x2e7b1a0, registered in `entry.init216`,
  description *"If set, client will try to get a manifest even without a manifest
  request code"*). Setting it `1` (Steam console: `@bClientTryRequestManifest
  WithoutCode 1`, note the `@`) makes the client *attempt* the manifest without a
  code, but **the CDN still requires the code** → "No connection". It bypasses
  the client-side check, not the server-side authorization. Kept here as a
  documented alternative/diagnostic, not the solution.
- **858 eresult flip is redundant.** Flipping the ownership-ticket eresult in a
  packet hook is unnecessary (SLSsteam handles ownership) and risks loops. The
  "Access Denied" ownership-ticket line is non-fatal.
- **A missing shader-depot key cancels the whole install** even when every
  content depot is perfectly served. The shader pre-cache depot's id is
  typically the app id itself (e.g. `1942280` for Brotato, `2379780` for
  Balatro). If `keys.txt` has no key for it (`<appid>;` with empty key field),
  Steam asks for it, our DepotKey hook falls through to the original, Valve
  denies it, and Steam **cancels the entire app** with
  `Missing decryption key`. The Brotato/Balatro contrast confirmed this:
  Brotato shipped a real key for `1942280` and installed end-to-end; Balatro's
  zip had `2379780;` (no key) and aborted. → before blaming the hooks for a
  `Missing decryption key`, check that `keys.txt` has a key for EVERY depot
  Steam asks for, including the shader-cache depot at the app id (see §13.7).
  **Resolved since v0.14.0:** a keyless shader depot no longer cancels/loops —
  the ShaderDepot hook makes Steam skip that game's shader pre-cache cleanly
  (§13.10). This note stays as the historical diagnosis and still applies to a
  keyless *content* depot (only the shader depot is skippable).

- **`DepotIdVec` is NOT the no-restart lever (tested, v0.16.14).** Seeding the
  package's `DepotIdVec` (+0x48) in addition to `AppIdVec` changed nothing — a
  game added mid-session still showed `0 target depots` until a restart. The
  blocker is upstream (Steam's appinfo has no depot list until it re-fetches it);
  depot eligibility is never reached. The actual fix is the **license reconcile**
  (§18), not the vector. Don't re-try the `DepotIdVec` angle on its own.

## 7. The GMRC endpoint(s)

The endpoint returns a **request CODE** (a bare uint64 number), **not** the
manifest file. The code only *authorises* Steam to download the `.manifest` from
Valve's CDN; the file itself always comes from the CDN (or from `depotcache/` if
pre-seeded, in which case Steam never asks for a code at all). The providers below
are **code sources, not file mirrors** — don't confuse "the manifest" (file) with
"the manifest request code" (token).

**What a code is.** A bearer token: Valve signs it for one (depot, manifest) pair
and the CDN serves the manifest to whoever presents it. The CDN GET is anonymous,
so a code minted on one machine works from any other. Valve's check happens **at
issue time**: since 2026-09-09 it only mints a code for an account that holds a
licence for the app. So every live provider runs a pool of accounts that own
the games, and a game nobody in a pool owns gets no code from that pool
(`src/gmrc_store.hpp:8-20`). Codes rotate on Valve's side (~15–20 min measured,
`gmrc_store.hpp:86-87`), so fetch them fresh and never persist them. How the old
providers died on 09-09 and how the current ones came back on 09-15/16 is in
Part II, §19 and §20.

### The getter and the hook

- Target: `BYieldingGetManifestRequestCode`, the function that issues the
  `"ContentServerDirectory.GetManifestRequestCode#1"` IPC job. Signature (cdecl,
  i386), unchanged since it was found (`src/hooks/gmrc_hook.cpp:14-20`,
  `src/patterns.hpp:129-137`):
  `int32 GetManifestRequestCode(void* this, u32 app_id, u32 depot_id, u32 manifest_lo, u32 manifest_hi, char* branch, uint64* out_code)`.
  It writes the code to `*out_code` and returns 1. For content the account does
  not own, the server denies the request and the function returns 0. §4 has
  the decompiled core.
- Resolution, each step only if the previous one came up empty
  (`gmrc_hook.cpp:65-101`): the RVA feed (`RvaFeed::Resolve("GMRC")`), then the
  byte pattern `kGmrcFunctionPattern`
  (`E8 ?? ?? ?? ?? 05 ?? ?? ?? ?? 55 89 E5 57 56 53 81 EC ?? ?? ?? ?? 8B 7D 08 8B 4D 20`,
  `patterns.hpp:150-151`), then `xref(rescue)`. The rescue takes the job-name
  string, finds the unique GOT-relative `lea` that loads it, and resolves the
  entry from `.eh_frame_hdr`'s function table (`src/gmrc_xref.hpp:7-30, 62-68`).
  A backward walk to the PIC preamble remains, but only as the fallback for a
  build without a usable table. The `sub esp` frame size in the pattern has been
  wildcarded since v0.22.0: the frame grew 0x110 → 0x120 on the 9cf4720f beta
  and nothing else changed (`patterns.hpp:141-145`).
- Interface-map lookup by name does not apply to GMRC: the getter is not a
  virtual of any `*IClient…Map` (all 52 swept on bc54101b, 2026-09-07,
  `gmrc_xref.hpp:53-64`), so the string xref is its only prologue-free locator.
- Hook body (`gmrc_hook.cpp:23-59`): `gid = manifest_lo | manifest_hi << 32`. It
  acts only when `KeyStore::HasManifestGid(gid) || KeyStore::HasDepot(depot_id)`.
  The second test covers the shader depot, whose gid comes from PICS rather than
  from the `.lua`. In that case it calls `Gmrc::GetCode(depot, gid)`, writes
  `*out_code` and returns 1. With no code it falls through to the original, and
  Steam's own path either finds the manifest in `depotcache/` or gets
  `Access Denied` from the CDN. Everything else goes straight to the original.
- **On by default since v0.21.0**, off with `LUMA_NO_GMRC` (`src/main.cpp:158-173`).
  The old opt-in `LUMA_GMRC` is no longer read. GMRC is **non-critical**. It is
  not part of the critical-hook gate (`main.cpp:192-198, 261-266`), and in CI it is
  a NONCRITICAL constant with a `gmrc=ok|moved` capability token
  (`tools/check_patterns.py:155-168`). If it is missing, Steam cannot fetch a
  manifest natively and a keyed game loses its shader pre-cache. Installs still
  work from the manifests LumaDeck pre-seeds in `depotcache/`.

### The provider cascade (`src/gmrc_store.hpp`)

Four live providers in `kProviders` (`gmrc_store.hpp:154-159`), asked in this
order; the first code the CDN accepts wins. Behind them are **two pools** of
licensed accounts, not four: wudrm, steam.run and manifestdex often hand out the
same code for the same manifest, while 20770407 never matches them (42 depots
measured, 2026-09-15/16, `:132-138`).

| Order | Provider | URL template | Pool | User-Agent sent | Body |
|---|---|---|---|---|---|
| 1 | 20770407 | `https://20770407.xyz/manifest/{depot}/{gid}` | A | `lumalinux/<ver>` | plain uint64; `Unauthorized` when the pool lacks it or the gid is bad |
| 2 | manifestdex | `https://manifest.manifestdex.com/{gid}` | B | `ManifestDeX/1.0` (the only UA it answers) | plain uint64 — for **any** gid, a made-up one included |
| 3 | wudrm | `http://gmrc.wudrm.com/manifest/{gid}` | B | `lumalinux/<ver>` | plain uint64 (plain HTTP) |
| 4 | steamrun | `https://manifest.steam.run/api/manifest/{gid}` | B | `lumalinux/<ver>` | JSON `{"content":"<uint64>"}` |

`opensteamtool` (`manifest.opensteamtool.com`) is gone: Cloudflare returns 403
for every User-Agent.

- **User-Agents.** `kUserAgentLuma = "lumalinux/" LUMALINUX_VERSION_STRING` and
  `kUserAgentMdx = "ManifestDeX/1.0"` (`gmrc_store.hpp:66-67`). A non-`curl` UA
  is still required: Cloudflare-fronted endpoints JS-challenge the default
  `curl`/libcurl UA (wudrm does today; opensteamtool did in 2026-07). lumalinux
  identifies as itself, not as `OpenSteamTool/1.0`, so it does not share OST's
  bucket if an operator ever filters or rate-limits by client (`:56-64`). The
  OpenSteamTool UA survives only in an outdated comment in `src/curl.hpp:14-18`.
- **Transport.** `Curl::getString` (libcurl `dlopen`'d at runtime, `src/curl.cpp:73`)
  returns 0 on transport success **without** failing on HTTP status. The status
  comes back through an out-param (`curl.hpp:27-31`), and `Classify`
  (`gmrc_store.hpp:211-223`) sorts each answer:
  a parsed non-zero uint64 is CODE (`ParsePlainUint` / `ParseSteamRunJson`,
  `:99-120`); 429 or `error code: 1015` is RATE_LIMITED; ≥500 or `Service
  temporarily unavailable` is TRANSIENT; 401/403 or `Unauthorized` is DENIED;
  anything else is NO_CODE, and a curl error is TRANSPORT. Timeouts are 5 s
  connect and 10 s total (`:96-97`).
- **Pacing and retries** (`GetCode`, `:334-417`). At most one provider request
  per second across all threads (`kMinGapMs`, `Pace()`, `:73, 235-247`), which
  stays under 20770407's published 10 req/10 s. Each provider gets up to 3
  attempts: wait 10 s after RATE_LIMITED, 5 s after TRANSIENT. DENIED moves on
  to the next provider at once, and so do TRANSPORT and NO_CODE.
- **CDN check before Steam sees a code** (`CdnAcceptsCode`, `:282-323`). It
  issues a one-byte ranged GET (`Range: 0-0`) of
  `/depot/<depot>/manifest/<gid>/5/<code>` on `steampipe.akamaized.net`, falling
  back to `fastly.cdn.steampipe.steamcontent.com`. Only 200/206 counts as
  accepted. A rejected code counts as that provider's DENIED, is remembered as
  a denial, and the cascade moves on. This check matters because a wrong code
  is not a soft failure. If Steam injects a code the CDN 401s, it cancels the
  install with "Unspecified Error", parks it in Update Paused and never
  retries (measured 2026-09-15 with a bogus code via `LUMA_GMRC_URL`; the
  wudrm failure of 09-10). The check needs it because manifestdex answers a
  number for every gid.
- **Skip-on-down.** A provider whose last outcome was TRANSPORT, NO_CODE, or
  RATE_LIMITED/TRANSIENT after the third attempt is marked down for
  `kProviderDownSec = 60` s (`:85, 172-175, 404-406`). Without this, every
  depot of an install paid ~12 s on a dead first provider. A DENIED or a
  CDN-rejected code is an answer, not an outage, so it does not mark the
  provider down.
- **Cache.** Results are cached per gid for `kCacheTtlSec = 120` s (`:93`),
  denials included, so a job that retries does not spend the budget again. The
  TTL is short on purpose: a long-lived code is a stale code.
- **`gmrc.json`.** `Status::RecordGmrc` writes
  `{"providers": "up"|"down", "at": "<ISO-8601 UTC>"}` after every lookup, next
  to `status.json` (`src/status.cpp:87-95`, `status.hpp:29-37`). "up" means some
  provider answered with a code or an honest denial. "down" means every
  provider was dead or skipped, because a game no pool owns is not an outage.
  LumaDeck reads this file to freeze unpinned games while no provider can serve
  a code.
- **Liveness probe for ShaderDepot** (`ProvidersReachable`, `:433-472`). It sends
  one paced request for depot 1 / gid 1 with 3 s/5 s timeouts and caches the
  result for 15 s. Any answer except TRANSPORT/TRANSIENT means "up" (the
  throwaway request is DENIED by design). ShaderDepot lets a keyed game's
  shader pre-cache run only when the GMRC hook is installed **and** this probe
  says up (`src/hooks/shader_depot_hook.cpp:80-95`, §13.11).
- **`LUMA_GMRC_URL=<template>`** replaces the whole table with one provider whose
  template takes two `%llu` (depot, gid) (`:177-187`). Use it for testing: point
  it at a black hole to exercise the pre-seeded-manifest fallback, or at a
  local server that answers a bogus number to exercise the CDN check.

The probe script `tools/gmrc_probe.py` still lists five providers, opensteamtool
included, so it is not an exact mirror of the runtime table (§10).

Reference values used by `tools/gmrc_probe.py` (Balatro; the gids are stale by
now but still a fair "does the provider know this manifest" test):
depot 2379781 gid 3512319404653808464; depot 2379782 gid 1898957422191678575;
depot 228989 (Steamworks redist, owned by every account) gid 3514306556860204959.
The same gid returned different codes on different days (for 3512319404653808464:
15549905601718457808, later 3261884576850880630), which is the rotation noted
above.

## 8. Reverse-engineering workflow (how to find these on a new build)

**The binary.** The hooked library is the 32-bit `steamclient.so` from
`~/.local/share/Steam/ubuntu12_32/` (the `steamdeck_stable_ubuntu12` package),
**not** `linux32/`. That is a different binary with a different hash, and
lumalinux never loads or hashes it. You don't need the Deck to get it:
`tools/fetch_steamclient.py --output steamclient.so` downloads it from Valve's
public client CDN with no login. The default is the `steamdeck_stable` manifest;
set `LUMA_STEAM_MANIFEST` to pick another channel. It prints the SHA-256 that
keys `res/updates.yaml` and `res/rvas/`. The library is ~50 MB (49.8 MB stable,
53.5 MB on the 2026-09-19 beta). Internal C++ methods are not symbolized,
and string references are PIC (per-function `get_pc_thunk` base), so
`objdump`/`grep` and radare2 5.5 fail to resolve, or only partly resolve, the
string xrefs. Ghidra resolves them. So does the GOT-consensus + `lea` scan that
the Python tools and the runtime rescue share (§8.1). *History:* the 2026-05
workflow pulled `linux32/steamclient.so` (~48 MB, ~826 exported symbols, build
f92deb5e) off the Deck and moved it through a throwaway git branch. Both steps
are obsolete.

**How a hook is resolved at runtime**, which is what a re-derivation has to
keep working. Every hook tries three steps, each only if the previous one missed
(`docs/maintenance.md` A.2 table):

1. `rva`: the per-build RVA feed `res/rvas/<sha256>.yaml`, fetched from `main`
   on every boot (`src/rva_feed.hpp`).
2. `pattern`: the byte pattern in `src/patterns.hpp`, accepted only if it
   matches exactly once.
3. A rescue that reads no prologue byte:
   - DepotKey uses `byname(rescue)`: RTTI class name →
     `IClientConfigStoreMap` slot → `CConfigStore` vtable
     (`src/hooks/depot_key_hook.cpp:138-176`).
   - GMRC, ShaderDepot and BuildDep use `xref(rescue)` on their anchor string
     (`gmrc_hook.cpp:87-94`, `shader_depot_hook.cpp:110-119`,
     `depot_dependency_hook.cpp:160-165`).
   - Reconcile uses `anchor(rescue)`, its callback-125 anchor
     (`src/reconcile_anchor.cpp`).

So a Steam update that moves only a prologue keeps working on the Deck through
the rescue, and through the feed once CI publishes it. The fixes below refresh
the pattern and the feed. They are not what keeps the Deck alive in the
meantime.

**Anchors per hook** (the strings and facts every derivation path starts from):

| Hook | Anchor | Notes |
|---|---|---|
| GMRC | string `"ContentServerDirectory.GetManifestRequestCode#1"` | job name; exactly one `lea` loads it |
| BuildDep | string `"BuildDepotDependency"` | diagnostic since SLSsteam 20260714 hooks it first |
| ShaderDepot | string `"shadercachedepot"` | PICS key read inside `GetShaderCacheDepot` |
| DepotKey | `IClientConfigStoreMap` method `"GetBinary"` → map slot → `12CConfigStore` vtable | no usable string inside the function; the old route was the dispatcher's `CALL [reg+0x18]` (§12.5) |
| Reconcile | the function that does `push 0x7d ; push eax ; call`, reads a field of `this` and bails on `<= 0` | posts callback 125 by number, never touches `LicensesUpdated_t`'s type_info |
| LoadPackage | none | opt-in diagnostic (`LUMA_LOADPKG_DEBUG=1`); multi-match by design |
| package-0 finder | cache-access idiom per known `CPackageInfoCache` layout + the GMRC prologue tail | not in `patterns.hpp`; a miss is a code fix in `package_zero_finder.cpp` (§13.5) |

**By hand**, when CI cannot do it (§8.1 says when), the steps are:

1. Fetch the binary as above.
2. Run `tools/check_patterns.py steamclient.so`.
3. Run the derivation chain `tools/derive_python_first.sh steamclient.so <consts> derived.json`.
4. Run `tools/apply_derived_pattern.py --derived derived.json --only <consts>`.
5. Re-run `check_patterns.py` until it reports CLEAN.
6. Rebuild and tag a release (`docs/maintenance.md` A.2 steps 4–5).

If the Python locator itself fails, open the binary in Ghidra and work from the
anchor. For DepotKey, the manual vcall route is in `maintenance.md` A.3 and
§12.5. Whatever you derive by hand, check that it is unique, and if you
wildcarded anything, run `tools/verify_mask.py` too: a loosened pattern can be
unique at the *wrong* site.

### 8.1 Semi-automatic re-derivation — `tools/derive_patterns.py`

*Title kept for its citations.* Since 2026-09-22 the Ghidra postScript
`derive_patterns.py` is the **second opinion**, not the deriver. The chain that
runs is Python first, and on every build measured so far Ghidra never has to
start.

**The CI chain.**

1. **`watch-steam.yml`** runs daily at 07:17 UTC.
   - A cheap version gate (`fetch_steamclient.py --version-only` + an Actions
     cache key) stops the run when nothing changed.
   - On a new version it downloads the binary and skips it if the hash is
     already whitelisted.
   - Otherwise it runs
     `check_patterns.py steamclient.so --json result.json --emit-rvas res/rvas`.
2. **`check_patterns.py`** (pure Python, parses `src/patterns.hpp` at run time)
   sets the exit code (`tools/check_patterns.py:1125-1135`):
   - **0 CLEAN**: the critical (DepotKey) is UNIQUE, the finder anchors are
     UNIQUE and every non-critical is UNIQUE. The workflow opens the hash-bump
     PR (`res/updates.yaml` + `res/rvas/<sha>.yaml`) and auto-merges it.
   - **2 NONCRITICAL_MOVED**: ShaderDepot, Reconcile and/or GMRC moved
     (`NONCRITICAL`, `:155-159`). The same hash PR goes out, with
     `# caps: shader=… reconcile=… gmrc=…`. Then the moved constants
     (`noncritical_moved_consts`) go through the derivation chain. If that
     works, a `fix(patterns)` PR follows that needs a release; if not, an
     issue is opened.
   - **3 BLOCKING**: one of three things failed. (a) The DepotKey pattern. (b)
     DepotKey's RTTI checks: exactly one `CConfigStore` vtable slot must match
     the pattern (`:961-1000`), and the by-name resolver must not disagree with
     it (`:1002-1028`). (c) A finder anchor (`finder:cache_idiom`,
     `finder:gmrc_tail`, `:1074-1123`). The build is not whitelisted.
     `tools/blocking_constants.py` keeps the constants a re-derive can fix and
     prints nothing for a finder anchor, so that case goes straight to an
     issue. If the chain plus re-validation come back CLEAN, a
     `fix(patterns)` PR follows with a new SafeMode group.
   - **1**: usage or I/O error.
   - BuildDep and LoadPackage are DIAGNOSTIC and never block.

   The GMRC xref rescue is also derived here. If it disagrees with the pattern,
   the run is BLOCKING; if it fails to resolve, that is reported but does not
   block. The header comment at `:31-33` still calls GMRC diagnostic. The code
   below it is right.
3. **`tools/derive_python_first.sh <so> <consts> <out.json>`** is the one chain
   every leg runs (`tools/derive_python_first.sh:11-25`). Per constant:
   - `kDepotKeyFnPattern` → `derive_depotkey_byname.py`: RTTI name → vtable
     slot, the same resolver the Deck uses as its last resort.
   - `kNotifyLicensesUpdatedPattern` → `derive_reconcile_byanchor.py`: callback
     125 + `this` read + `jle`, which leaves exactly one function on bc54101b
     and 9cf4720f.
   - `kGmrcFunctionPattern`, `kBuildDepotDependencyPattern` and
     `kShaderCacheDepotPattern` → `derive_bytext.py`: string → GOT by consensus
     → unique `lea` → function from `.eh_frame_hdr`, the runtime rescue's own
     locator.

   All three locators hand the address to `derive_from_address.py`, the shared
   masking core: one mini-decoder, one rule set, one growth loop (rules below).
   Only a constant that has no Python result goes to
   `tools/run_ghidra_derive.sh`. That script runs Ghidra **11.2.1** headless:
   it is the last release bundling Jython, it runs with `MAXMEM=6G`, and it uses
   a fresh `-import` each run (`run_ghidra_derive.sh:22-29, 47-61`). Its UNIQUE
   results are merged in, and a Ghidra result never overwrites a Python one.
   Exit status is 0 when every requested constant has `matches == 1`, and 3
   otherwise.
4. **`tools/apply_derived_pattern.py --derived derived.json --only …`** rewrites
   only constants that already exist in `patterns.hpp` and only from UNIQUE
   entries. It exits 4 if a constant cannot be applied.
5. **Re-validation and feed emission.** `check_patterns.py --emit-rvas res/rvas`
   runs again on the patched header. Only a CLEAN result produces a PR, and only
   a non-BLOCKING verdict writes `res/rvas/<sha>.yaml` (`:1207-1210`), which
   contains three things:
   - hook RVAs for UNIQUE non-diagnostic hooks only;
   - `depotkey_rtti`;
   - the finder's `cache_global_disp`, `cache_root_off`, `cache_nodes_off` and
     `got_rva` (`emit_rvas_file`, `:48-130`).

   Merging the PR fixes the build on deployed `.so`s at next boot through the
   feed. The release is still needed for the patterns on every other build.

**Testing the chain without a real move.** `watch-steam-selftest.yml` corrupts
patterns on a live binary and asserts that the real chain gets back to CLEAN.
It has three targets:

- `shaderdepot` (default) checks the exit-2 path.
- `criticals` corrupts DepotKey and checks the exit-3 path through
  `blocking_constants.py`.
- `ghidra` runs the postScript alone and asserts that it agrees with
  `derive_bytext.py` on GMRC, BuildDep and ShaderDepot.

The selftest also runs on any PR that touches the derivation tools. Its header
still calls GMRC diagnostic. **`probe-steam.yml`** (manual) runs the same check
and chain against any client channel, such as a Deck or desktop beta, before
promotion. It works on the runner's copy only: no PR, no issue, no feed. Since
2026-10-01 it also prints the GNU build-id.

**What Ghidra still does, and what it cannot do.** `derive_patterns.py`
(`tools/derive_patterns.py:10-44`) derives the three string-anchored hooks and
**validates** the DepotKey and Reconcile literals. It reads them from
`patterns.hpp` and never keeps its own copy. It also verifies the two finder
anchors. Its old indirect walks never worked headless:

- For DepotKey, headless Ghidra **never resolves the dispatcher's
  `CALL [reg+0x18]`** on any build tried (`watch-steam-selftest.yml` run #7,
  2026-09-14). While the script still carried a stale DepotKey literal, that
  failed walk "validated" it as UNIQUE at RVA `0x189fca0`, a function that is
  not DepotKey (the accessor is `0x11a4500` on bc54101b).
- `NotifyLicensesUpdated` never references `LicensesUpdated_t`'s type_info
  (2026-09-22).

Run it by hand like this:

```sh
analyzeHeadless <proj> <name> -import steamclient.so \
    -scriptPath tools -postScript derive_patterns.py [--json out.json]
# or, on an already analysed project:
analyzeHeadless <proj> <name> -process steamclient.so \
    -noanalysis -scriptPath tools -postScript derive_patterns.py
```

Analysis takes roughly 5–30 min; CI allows ~20. `tools/ghidra_find_gmrc.py`,
the original GMRC-only finder, is kept for reference.

**Masking rules (still load-bearing).** `derive_from_address.wildcard_prologue`
(`tools/derive_from_address.py:166-232`) and Ghidra's `extract_pattern`
(`derive_patterns.py:210-262`) apply the same base rules:

- Always wildcard the PIC `call` rel32 and the GOT `add reg,imm32` after it.
  That `add` has two encodings, `0x05` (`add eax,imm32`, 5 bytes) and
  `0x81 /0` (`add r/m32,imm32`, 6 bytes), and both imm32s must be masked. GMRC
  uses the short form; BuildDep, LoadPackage, ShaderDepot and DepotKey use
  `0x81`.
- Always wildcard any `mov/lea r32,[picbase+disp32]`. These GOT-relative global
  offsets shift on almost every build: the package-0 cache global moved
  `0x3a1bc→0x3967c` (§13.5), and the shader-manager global moved the same way
  (§13.10).
- Python only: `mask_frame` wildcards the `sub esp,imm32` (`81 EC`) imm32.
  `derive_bytext.py` turns it on for its three hooks
  (`tools/derive_bytext.py:64-65`).
- Python only: `mask_disp32` also wildcards every other `[reg+disp32]` and the
  register choice of the load/`test`. Reconcile uses it, because its member
  offset drifted `0x1b14→0x1b10` and its temporary changed register on the
  9cf4720f beta.
- Every derivation then grows by whole instructions from 28 bytes until the
  pattern matches **exactly once**. The decoder refuses an opcode it does not
  know rather than guess.

**The frame-size rule, then and now.** On 2026-06-24 (§13.10 audit) the rule
was "do NOT wildcard `sub esp, imm32`". At a fixed 28-byte length, wildcarding
it left GMRC unique but made BuildDep and LoadPackage collide (6 matches each on
7c4ac73e). Issue #16 re-measured it on 2026-07-06 with
`tools/experiment_framesize_mask.py` and got 70 and 222. That is still true of
*fixed-length* patterns: BuildDep's and LoadPackage's shipped literals keep
their frame size, and DepotKey's `83 EC 24` (imm8) is never masked. What
changed is the growth loop. A frame-masked prologue that collides at 28 bytes
is extended until it is unique, so the frame is now masked where it actually
moved: GMRC since v0.22.0, and Reconcile. Any newly loosened literal still has
to pass `verify_mask.py` (same site, not just unique).

**Jython quirk, still worked around.** In Ghidra 11.2.1's Jython,
`Memory.findBytes` with full-byte wildcards in short patterns returned zero hits
even when the bytes existed (seen in the v0.13.1 verification pass). The
finder-anchor checks search for an unambiguous literal first and verify the
surrounding bytes with `mem.getByte()` (`derive_patterns.py:295-308, 364-381`).
Do the same if you add another anchor check to the postScript. The Python tools
do not have this problem.

**Robustness, most to least robust.**

1. The string-anchored trio (GMRC, ShaderDepot, BuildDep): a stable Steam
   string leads to the function, at runtime and in CI alike.
2. DepotKey by name: it reads no prologue byte, but it depends on the RTTI
   names and the `GetBinary` map slot.
3. Reconcile by behaviour anchor.
4. LoadPackage: diagnostic, with no anchor at all.

The package-0 finder's anchors are not patterns. When they move, the fix is
code in `src/hooks/package_zero_finder.cpp` (`maintenance.md` §C).

## 9. Per-game data (handled by tools/steamidra_lite.py)

What `tools/steamidra_lite.py` writes, its CLI, the three `keys.txt` line formats
and `retired_keys.txt` are documented in one place: `docs/manual-install.md`. The
facts that matter for reverse engineering, and that the hooks rely on:

- Manifests go to `<steam>/depotcache/` **only**. Steam does not read
  `config/depotcache/`. Measured 2026-09-11: with the manifest only there, Steam
  still asks Valve for a request code (§19.3, `steamidra_lite.py:10-11`).
- The manifest size in an EXTENDED `keys.txt` line is `cb_disk_original`. It is
  read from the `.manifest` binary, from the metadata block with protobuf magic
  `0x1F4812BE`, field 5 (varint) (`steamidra_lite.py:219-220, 273-298`). The
  gid comes from the `.lua`'s `setManifestid`. Those `gid`/`size` fields only
  fed the BuildDep hook, which is off by default. Pinning is SLSsteam
  `ManifestIds`.
- `config.vdf` `DecryptionKey`s are pruned by Steam on shutdown for unowned
  depots (§6), so `keys.txt` is the source the DepotKey hook serves from.
  `tools/vdf_inject_keys.py` is only a belt-and-suspenders helper.

The ACCELA/ASSella markers (`.DepotDownloader/`,
`~/.local/share/ACCELA/depots/<appid>.depot`) and the `--accela-mark` flag were
removed on 2026-10-06 (`steamidra_lite.py:820-822`).

## 10. Open items / ideas for extension

Open as of 2026-10-09. The project-wide list is `docs/nosotros.md` §4.1, and what
has been decided is in its §4.4. This section keeps the reverse-engineering ones.

- **Async finder ↔ Steam read race on `CPackageInfoCache`** (theoretical; no
  crash observed). The package-0 finder (§13) reads `CPackageInfoCache` and the
  `PackageInfo*` it returns from its own thread, while Steam can mutate them
  from its own threads.

  What is already protected:
  - Every dereference goes through a `/proc/self/maps` readability check
    (`IsReadable`, `src/hooks/package_zero_finder.cpp:463, 486-520`). A
    freed-and-unmapped address yields `nullptr` and a log line, not a SIGSEGV.
  - `AppendIdsToVec` rejects an implausible `AppIdVec`: `m_Size > 4096`, or
    entries that are `0` or `> 50M` (`src/hooks/load_package_hook.cpp:77`).

  What is not protected is a freed-but-still-mapped use-after-free. Steam
  destroys `PackageId=0` (re-login or licence refresh), the finder grabs the
  stale `PackageInfo*` in that gap, and the realloc on a dangling `m_pMemory`
  corrupts the heap. In practice the tree is built once at login and stays
  stable; rebuilds are rare and the window is microseconds.

  **Shipped 2026-09-08:** `FindPackage0` cross-checks the tree node's key
  against the `PackageInfo`'s own `PackageId` (+0x00) and refuses to return the
  object when they disagree (`package_zero_finder.cpp:525-565`). This does not
  narrow the window. It turns a walk that landed on the wrong object into a
  refusal and a log line instead of a write. It does NOT catch the dangerous
  case: a STALE `PackageInfo` for the real package 0 still reports id 0 and
  passes. The same goes for the plausibility checks above, which test
  plausibility, not identity.

  If a repro ever appears, the remaining options, cheapest first:
  - (a) Drop the post-hit re-inject watch, so only the first-hit window
    exists.
  - (b) Double-read the `PackageInfo*` ~10 ms apart and retry on change.
  - (c) An atomic CAS on `m_pMemory` (`__atomic_compare_exchange_n`). This bets
    that Steam keeps using libc realloc for `CUtlMemory` (§11.2).

  Leave it alone until there is a real repro: speculative concurrency fixes
  tend to introduce more bugs than they fix. Five of the finder's seven struct
  offsets are still verified by no tool (`package_zero_finder.cpp:86-97`); the
  identity check is the mitigation for that too.
- **The re-derivation chain has never run on a real move.** Since June, Valve
  has not moved a shipped pattern. The chain has only been exercised with
  deliberately corrupted patterns (§8.1). The cron watches only
  `steamdeck_stable`, so a beta recompile is invisible until promotion unless
  someone runs `probe-steam.yml`. A second cron job on `steamdeck_publicbeta`
  was noted and deferred (nosotros §4.1-36, §4.4 C2).
- **Hash is advisory; a hook can resolve UNIQUE at the wrong site and still
  install.** The defence is the crash-loop guard, not a block. `status.json`
  says `installed` but cannot tell "installed" from "installed and serving". A
  per-hook hit counter (~30 lines) is the noted candidate (nosotros §4.1-37,
  §4.4 C5b).
- **Masking left undecided:** the `0x44` in ShaderDepot's
  `mov eax,[eax+0x44]`. Mask it only if a build ever moves it, and validate with
  `verify_mask.py` (nosotros §4.4 C1).
- **Call-site anchors need a convergence rule first.** `classify_hit_count`
  (`tools/check_patterns.py:494`) marks any n>1 AMBIGUOUS. Before any hook is
  anchored on a call site, it has to accept N hits that converge on one target
  (nosotros §4.4).
- **GMRC xref walk-back retirement.** `WalkBackToPrologue` stays only for builds
  without a usable `.eh_frame_hdr`. Delete it if real logs never show
  `.eh_frame_hdr unavailable — falling back to the walk-back`
  (`src/gmrc_xref.hpp:72-77`, `gmrc_xref.cpp:152`).
- **`tools/gmrc_probe.py` probes five providers**, opensteamtool included, where
  the runtime has four. The dead one just reports DOWN (nosotros §4.1-22). The
  stale code comments this list used to carry were corrected on 2026-10-10
  (nosotros §4.1-39).
- **`RelocateChainedJmp`** (`src/lmhook.cpp:97-123`) is wired into
  `LmHook::Install` and runs whenever a target's first byte is already `0xE9`
  (another hooker's detour, `:138-156`). None of the default hooks targets a
  function another library detours. BuildDep, which SLSsteam hooks, is off. So
  the code path is untested on a real build.
- **Only depot 2379781 mounted for Balatro** (2026-06; 64 MB, the game runs).
  2379782 (81 MB) was not downloaded, most likely the other-OS depot that Steam
  doesn't need under Proton. Not re-examined since. If a multi-depot game has
  depots that don't mount, check whether Steam requests their manifests at all
  (OS filter, branch).
- Prefetching the keystore's codes at startup (background) instead of on
  demand: never built and never decided. Against it today: codes rotate, the
  cache is deliberately 120 s, every code costs a CDN check, and 20770407
  allows 10 req/10 s (`gmrc_store.hpp:69-93`), so a prefetch would spend the
  budget on codes that may go stale before Steam asks for them.

Closed since the previous version of this list:
- The Headcrab `steam.sh` re-patching problem is gone. `setup.sh` injects
  through a wrapper at `~/.local/share/SLSsteam/path/steam` and restores a
  vanilla `steam.sh` (`setup.sh:18-26, 244-261`; nosotros §2.1).
# Part II — Log

Each section below describes the code as it was on its date and is not rewritten afterwards.
Where something changed later, the section starts with a "> **Since then:** …" note that says what is true today and where it is documented.

## 11. lumalinux vs LumaCore — verified divergences

*(2026-05-31, rewritten 2026-06-05 against v0.8.1; §11.5 updated for v0.15.8, 2026-07-01, and on 2026-08-13)*

> **Since then:** this section is a snapshot of LumaCore and of lumalinux as of its date. The current reading of LumaCore is [`lumacore.md`](lumacore.md) (§2.4, §4.3), and today's lumalinux internals are in [`nosotros.md`](nosotros.md) §2.

Side-by-side comparison of the equivalent pieces, derived from reading both
codebases. Useful as a map when something breaks and we need to decide
whether to mirror LumaCore literally or assume our divergence is deliberate.

### 11.1 KeyStore vs `LuaLoader::DepotKeySet`

| | LumaCore | lumalinux |
|---|---|---|
| Source | `.lua` parsed with a real `lua_State` | `keys.txt` (custom format) |
| Structure | `unordered_map<DepotId, hex_string>` (**flat**) | `map<DepotId, {parent_app_id, gid, size, key}>` (rich) |
| `parent_app_id` | **DOES NOT EXIST** — all depots are global | Yes — a lumalinux invention; used by `GetDepotsForApp` |
| `manifest_size` | **Always 0** — verbatim comment: *"size is always forced to 0 to prevent incorrect size from breaking Steam"* (`LuaLoader.cpp:172`) | Stored (from the 3-arg `setManifestid` or the binary parse) but **not actually used** — the BuildDep hook deliberately ignores it. Dormant field. |
| `GetAllDepotIds()` | Lists every depot in the DepotKeySet | Same |
| `GetDepotsForApp(app_id)` | **DOES NOT EXIST** | Filters `parent_app_id==app_id && gid!=0` |

No real operational divergence: we store `manifest_size` but never project it
to Steam. The field could be removed in a future cleanup (we keep parsing it
out of the `.lua` for compatibility with the Hubcap format).

### 11.2 Package-0 injection (`PackagePatch::LoadPackage` equivalent)

Note: in lumalinux the LoadPackage **hook** is diagnostic-only since v0.13.0;
the actual injection is performed by the **package-0 finder** (§13), on its
own thread, calling the shared `Hooks::LoadPackage::InjectDepots`. The table
below compares that shared injector with LumaCore's `PackagePatch::LoadPackage`.

| | LumaCore | lumalinux |
|---|---|---|
| Trigger | `pInfo->PackageId == 0` (from inside the hook) | `PackageId == 0` (from the finder walking `CPackageInfoCache` — works even if Steam never calls `LoadPackage`, see §13) |
| What gets injected | `GetAllDepotIds()` (all of them) | Same |
| Duplicate filter | NO | YES (content-based dedup loop over `oldSize`) — makes re-injection idempotent |
| Vector sanity check | NO | YES (skips if `oldSize > 4096` or any sampled entry looks bogus, e.g. `0` or `> 50M`) |
| **How `AppIdVec` grows** | `oCUtlMemoryGrow(&pInfo->AppIdVec, numToAdd)` — resolves and calls Steam's own grow function | Direct `std::realloc(vec->m_pMemory, …)`. Policy mirrors Source SDK `CUtlMemory::Grow`: double capacity if possible, snap to requested size otherwise, minimum 32 entries on the first allocation. |

**Why realloc is safe on Linux i386 today** (verbatim from the comment in
`load_package_hook.cpp`):

> Steam Linux i386 uses raw libc realloc for CUtlMemory (verified by
> disassembling the only 5 callers of `realloc@plt` in steamclient.so — the
> leaf helper at +0x5c79d30 is `realloc(ptr, count * elem_size)` with no
> tier0 allocator wrapper, confirming that `m_pMemory` was malloc-allocated
> and is safe to realloc from outside. This is the Linux equivalent of
> LumaCore's `oCUtlMemoryGrow` on Windows.

History: an earlier version (≤v0.7) used a manual `m_pMemory` swap and
crashed the appinfo-cache rebuild thread (null deref in `libvstdlib_s.so`).
The crash went away once we verified that Steam itself uses libc `realloc`
rather than its tier0 internal allocator, so our `std::realloc` matches what
Steam does. There is no effective capacity ceiling any more.

### 11.3 DepotKey hook (`DepotKeys::LoadDepotDecryptionKey`)

| | LumaCore | lumalinux |
|---|---|---|
| Hooked function | `LoadDepotDecryptionKey(pObject, foo, KeyName, Key, KeySize)` — KeyValues-path-based | Same signature |
| How the depot is identified | Parses `KeyName` (`.../<DepotId>\DecryptionKey`) | Same |
| Buffer validation | `if (KeySize >= key.size()) memcpy(...)` | `if (keySize >= 32 && key != nullptr) memcpy(...)` |
| Return value on success | `static_cast<int32>(key.size())` = 32 | `static_cast<int32_t>(32)` |
| Passthrough on miss | `oLoadDepotDecryptionKey(...)` | `g_origFn(...)` |

No material divergence from LumaCore. Hook hits the KeyValues accessor with
buffer-size validation and a correct passthrough on miss.

History: ≤v0.8 lumalinux hooked the outer dispatcher
`(this_, app_id, depot_id, out_key)` **without validating the buffer size**,
because `out_key` was assumed to be a raw 32-byte slot. That contract turned
out to be false (the cache treated it as a 128-byte struct with metadata),
causing heap corruption on Formula Legends (see §12). v0.8+ migrated to
LumaCore's KeyValues accessor with `KeySize` validated, eliminating the
latent overrun.

### 11.4 BuildDep hook (`ManifestBind::BuildDepotDependency`)

> **Since then:** the BuildDep hook is off by default since v0.16.10 (SLSsteam hooks `BuildDepotDependency` itself; `LUMA_FORCE_BUILDDEP` turns ours back on, `src/main.cpp:175-186`), and version pinning is SLSsteam's `ManifestIds`; see [`nosotros.md`](nosotros.md) §2.1 and §2.5.

| | LumaCore | lumalinux |
|---|---|---|
| Hooked function | `BuildDepotDependency(this, AppId, ..., pDepotInfo, pSharedDepotInfo, ...)` | Same signature |
| Source of overrides | `LuaLoader::GetManifestOverrides()` (flat) | `KeyStore::GetDepotsForApp(AppId)` (filters `parent_app_id==AppId`) |
| Match on patch | Direct DepotId | Same |
| Which vectors are patched | **Only `pDepotInfo`** | **Only `pDepotInfo`** (mirrors LumaCore) |
| What is patched | `gid` + `size` (0 → keep original) | **Only `gid`** — `ManifestSize` is left as Steam had it |
| `patched` counter | Counts any match | **Only counts actual changes** (`if (gid != newgid) patched++`) |
| Guard on `result==false` | Yes — leaves the vector alone | Same (mirror) |

No operational divergence from LumaCore. Both the vectors patched and the
fields touched line up.

History: ≤v0.5 lumalinux patched **both** `pDepotInfo` and `pSharedDepotInfo`,
which broke the parent app's appinfo coherence for the shared depot (VC 2022
Redist 228989 under app 228980) and caused the Formula Legends crash (heap
corruption → SIGSEGV in `libc malloc_usable_size`). v0.6 restricted the
patch to `pDepotInfo`. Additionally, ≤v0.7 overwrote `ManifestSize` with our
KeyStore size (matching the theoretical divergence with LumaCore); v0.8
stopped touching it, mirroring what LumaCore actually does.

**Note on a historically misleading WARN**: the message
`BuildDep: app X has N KeyStore depots but NONE matched` that fired when the
gids were already correct was downgraded to `Log::Debug` with neutral text in
v0.8.1.

### 11.5 GMRC hook (`ManifestBind::FetchSteamRun`)

> **Since then:** the lumalinux column is out of date. Since v0.21.0 the cascade has four providers (20770407 → manifestdex → wudrm → steamrun, opensteamtool dropped), every code is checked against Valve's CDN before Steam sees it (`CdnAcceptsCode`), and the User-Agents are `lumalinux/<version>` and `ManifestDeX/1.0` (`src/gmrc_store.hpp:66-67, 154-159`); see §20.3 and [`nosotros.md`](nosotros.md) §2.4. The gating (`HasManifestGid || HasDepot`, `src/hooks/gmrc_hook.cpp:43`) is unchanged.

> **Correction (verified 2026-08 against SFF `6.6.4`).** An earlier note here said
> LumaCore had **no `GetManifestRequestCode` hook at all**. That is now **stale**.
> LumaCore runs a runtime GMRC bridge at the **wire layer** —
> `NetPacket_Manifest.cpp` (`Handlers::DepotFallback`) intercepts the outbound
> `ContentServerDirectory.GetManifestRequestCode#1` on
> `BBuildAndAsyncSendFrame`/`RecvPkt` and rewrites the CM response with a uint64
> code from `ManifestFetch.cpp` (the same three providers lumalinux uses).
> SteaMidra's Python `get_gmrc` pre-fetch still exists too, for pre-seeding
> `depotcache/`. So the runtime GMRC hook is **unique to lumalinux in *mechanism*
> (function-layer, SLSsteam-safe), not existence** — LumaCore reaches it on the
> wire layer, which SLSsteam owns and lumalinux cannot touch. The table's LumaCore
> column below is updated to the `6.6.4` wire-layer bridge. See
> [`nosotros.md`](nosotros.md) §2.4 and [`lumacore.md`](lumacore.md)
> §2.4.

| | LumaCore 6.6.4 (wire-layer) | lumalinux |
|---|---|---|
| Seam | **Wire handler** on `ContentServerDirectory.GetManifestRequestCode#1` (`NetPacket_Manifest.cpp`, via `BBuildAndAsyncSendFrame`/`RecvPkt`) — **not** a function hook | **Function hook** on `GetManifestRequestCode` (writes `*out_code`) |
| HTTP endpoint | **3-provider chain** (`ManifestFetch`): opensteamtool → steam.run → wudrm | **3-provider cascade** (v0.15.8): opensteamtool → wudrm → steamrun |
| HTTPS / HTTP | WinHTTP, `OpenSteamTool/1.0` UA for the opensteamtool host | **libcurl** (`dlopen`), HTTPS+HTTP, UA `OpenSteamTool/1.0` (§7) |
| Gating | `LuaLoader::HasDepot(depotId)` on the outbound request | `KeyStore::HasManifestGid(gid) \|\| KeyStore::HasDepot(depot_id)` — the OR covers the shader pre-cache case where Steam asks for a manifest that wasn't in the original `.lua` |
| Return on success | Async: rewrites the CM **response frame** with `set_manifest_request_code(code)` + `eresult=OK` (via PacketRouter) | Synchronous — `*out_code = code; return 1` |

lumalinux is functionally better here (covers shader pre-cache) and, since
v0.15.8, no longer worse on resilience: the single-plain-HTTP-host weakness was
replaced by the 3-provider cascade (§7). It still has no *offline* fallback — if
all three code providers are down at once, a manifest FILE that isn't already in
`depotcache/` can't be fetched (the content depots are unaffected: their
manifests ship in the Hubcap zip and are pre-seeded into `depotcache/`).

History: ≤v0.8.0 the gating was just `HasManifestGid(gid)`. That dropped the
shader-pre-cache manifests through to the original (which the CDN denies)
because their gid comes from PICS appinfo and is never pre-registered in
`keys.txt`. Steam interpreted the "Access Denied" as "No connection" in the
UI for the first few minutes of every new install (only visible with an
`.acf` of `StateFlags=1`; with `StateFlags=4` Steam skipped the shader
pre-cache entirely). v0.8.1 widened the gating with `|| HasDepot(depot_id)`.

### 11.6 Suspect list for future crashes / regressions

> **Since then:** the list that stood here is superseded. Which hook is critical and what breaks when each one fails is now the "Si falla" ("if it fails") column of the hook table in [`nosotros.md`](nosotros.md) §2.1 (today only DepotKey and the package-0 finder are critical; every hook resolves feed → pattern → rescue). The one item not covered there, a possible race in `Log::Init` (Issue #5), was not re-checked.

## 12. The DepotKey hook crash and its fix (Formula Legends)

*(2026-06-02, v0.8.x)*

Full investigation, reproduced on real Steam Linux (codespace Ubuntu 24.04,
Xvfb+noVNC, SLSsteam + lumalinux). Not hypothesis.

### 12.1 Symptom

Pressing Install on a game with shared depots (Formula Legends, app 3194360,
with redists 228989/228990 from app 228980), Steam crashed with
`free(): invalid pointer` → abort. The crash was **heap corruption**, not a
segfault in our hooks: it happened inside Steam code AFTER the DepotKey hook
had returned.

### 12.2 Isolation (which hook, which depots)

Disabling hooks via env vars (`LUMA_NO_GMRC`, `LUMA_NO_DEPOTKEY`):
- No lumalinux → no crash (but "content still encrypted": the native flow
  needs the hook to serve the content depot's key).
- DepotKey off → no crash, download proceeds, but `Missing decryption key`
  for 3194361 → the DepotKey hook **is required**.
- The crash correlated with **serving the keys for 228989/228990** (the
  redists Steam ALREADY OWNS via the 228980 licence). Serving the content
  depot key (3194361, unowned) did NOT crash.

### 12.3 Root cause

lumalinux ≤v0.8 hooked the **outer depot-key dispatcher**
(`(this, app_id, depot_id, out_key)`) and **short-circuited** it (served and
returned OK without letting Steam's own machinery run). For a depot Steam
owns, that leaves Steam's internal state inconsistent (the owned path
expected to flow through its cache/refcount) → heap corruption.

Also confirmed: that function's `out_key` is NOT a raw 32-byte buffer. The
cache function initialises it as a 128-byte struct (`KeySize=128`). Our
32-byte `memcpy` fit (no overflow), so the crash was about **state**, not
about the buffer.

### 12.4 Why dynamic ownership detection does NOT work on Linux

We tried to replicate LumaCore's `MarkOwned` (serve only the unowned ones):
- **`CheckAppOwnership`**: SLSsteam spoofs it → an unowned app looks owned.
  Useless signal.
- **Call the original dispatcher first** (option-C): the original returns OK
  for 3194361 because its key is in `config.vdf` → indistinguishable from
  owned. And for the ones that do fail, the **denied RPC leaves state that
  crashes** when we serve.
- **Call only the inner cache function** (cache-first): ownership detection
  works (the cache reflects real licences, not `config.vdf`), but its *miss*
  path has an **internal side effect** that crashes anyway — verified by
  probing even with a scratch buffer (the real `out_key` stayed intact and
  it still blew up). Conclusion: **you cannot call ANY depot-key function of
  Steam around serving without poisoning state.**

### 12.5 The fix (parity with LumaCore)

LumaCore does not hook the dispatcher: it hooks the **inner KeyValues
accessor** `LoadDepotDecryptionKey(pObject, foo, KeyName, Key, KeySize)`,
which the dispatcher invokes (via virtual call) to read the key out of the
store with KeyName `"Software\Valve\Steam\Depots\<depot>\DecryptionKey"`.

Hooking THERE just **answers the query**: Steam's cache gets a clean "hit"
and the dispatcher continues down its normal path (owned or not) — no
short-circuit, no corruption. Works for owned and unowned alike, **with no
static shared-depot list**.

Resolving the function on Linux i386 (it wasn't in patterns):
1. Locate the cache fn (its own pattern).
2. Follow its virtual call: `this=*(global)+0xd60; vtable=*this;
   fn=*(vtable+0x18)`, reading `/proc/PID/mem` of the live Steam (root).
   That gives the inner accessor.
3. Dump its prologue, derive the unique pattern (`kDepotKeyFnPattern`, v1.0).

Signature (cdecl i386, 5 stack args): `pObject, foo, KeyName, Key, KeySize`.
The hook parses the depot out of the KeyName, validates `KeySize >= 32`,
does `memcpy(Key, ourkey, 32)` and `return 32`; passes through if the
KeyName isn't a depot key or we don't have one.

### 12.6 End-to-end verification

With the v1.0 hook: FL serves 228989, 228990 and 3194361 (`KeySize=128`)
**without a single crash dump**, downloads and decrypts the 13 GB
(`ASC.exe` + content on disk), `appmanifest_3194360.acf` with
`StateFlags=4` and `InstalledDepots` populated. Full native install.

Benign residue: after completion, Steam queues an auto-update that returns
`Missing decryption key` / `UpdateResult=8` without flipping `StateFlags`
away from 4 — the same residue `_patch_acf_error_state` (SteaMidra) cleans
up; it doesn't block Play.

### 12.7 Implication for §11.3

Same conclusion as the "History" paragraph of §11.3: the v1.0 hook validates `KeySize`, and the crash came from short-circuiting the dispatcher, not from the buffer.

## 13. The package-0 finder — from a passive hook to an active walker

*(2026-06-15, v0.10.9–v0.13.0; §13.5 and §13.7-§13.11 carry their own later dates)*

(Origin: v0.10.9 – v0.10.11. Promoted to the sole, default-on injector in
v0.13.0.)

This is the story of how the LoadPackage hook stopped being enough after a
Steam update and why `src/hooks/package_zero_finder.cpp` exists. It is "the
new piece" of the v0.10.x cycle: it doesn't patch a byte pattern, it
**searches for the live object at runtime**.

### 13.1 Symptom

After a client update, an unowned game stopped installing. The other three
hooks (DepotKey, BuildDep, GMRC) installed and fired fine, but Steam never
put the content depots into the download plan: the log showed
`LoadPackage hook: INSTALLED` and **never** `LoadPackage: PackageId=0 hit`.
Without the injection into `PackageId=0`, Steam's per-depot licence filter
drops the content depots and the app sits at "Fully Installed" with 0 bytes.

### 13.2 The false lead (v0.10.6 / v0.10.7 / v0.10.8 rollbacks)

The initial hypothesis was "Steam refactored
`CPackageInfoCache::LoadPackage` and the pattern no longer matches the right
function":

- **v0.10.6** — rolled LoadPackage back to the v0.10.3 wrapper + added
  discovery probes.
- **v0.10.7** — restored the v0.5 pattern… and the commit title says it all:
  *"Steam did NOT refactor it"*. The pattern still matched the right
  function. The lead was false.
- **v0.10.8** — added `LUMA_LOADPKG_DEBUG=1` to log every hook firing. That
  surfaced the real issue: the hook **was** installed on the correct
  function, but `LoadPackage` **wasn't being called** for `PackageId=0` this
  boot.

### 13.3 Root cause

The LoadPackage hook is **passive**: it only acts when Steam *calls*
`CPackageInfoCache::LoadPackage`. And `LoadPackage` is only invoked when the
package is loaded into the cache. If Steam already has `PackageId=0` cached
from a previous session (on-disk licence cache), **it never re-calls
`LoadPackage` for package 0** — so our append never runs. The hook isn't
misplaced; the event it waits for doesn't happen this boot.

Corollary: relying on an *event* (the hook firing) for a structure that may
already exist is brittle. The robust thing is to go and **find** the structure.

### 13.4 The fix — an active cache walker

> **Since then:** the legacy LoadPackage hook is no longer installed at all unless `LUMA_LOADPKG_DEBUG` is set (`src/main.cpp:187-188`); the finder knows two cache layouts, `0xc58/0xc6c` and `0xf90/0xfa4` (`kCacheLayouts`, §13.5.c); it also removes from package 0 the ids that left `keys.txt` (`src/hooks/load_package_hook.cpp:227-247`); and its 2 s / 15 s wait is cut short by a `keys.txt` change (`LicenseReconcile::WaitForKeysChangeOr`, `src/hooks/package_zero_finder.cpp:827`). See [`nosotros.md`](nosotros.md) §2.1 and §2.2.

`PackageZeroFinder` (detached thread, **on by default** since v0.13.0;
disable with `LUMA_NO_PKG0_FINDER`, and `LUMA_PKG0_FINDER=diag` leaves it in
log-only mode) **walks the package cache directly** instead of waiting for
the hook. `CPackageInfoCache` is an index-based binary search tree with
stable class-layout offsets (verified on builds `7c4ac73e` and `db0d79c2`):

```
cache + 0xc58   int32   root node index (-1 = empty)
cache + 0xc6c   T*      node array base
node (0x18 B):  +0x00 left  +0x04 right  +0x10 packageId  +0x14 PackageInfo*
```

The worker looks for the node with `packageId == 0` and, once found, injects
the depots into its `AppIdVec` via the shared
`Hooks::LoadPackage::InjectDepots` (same append-in-place logic detailed in
§11.2).

**The finder is the SOLE injector** (v0.13.0). The classic LoadPackage hook
**no longer injects** — it stays installed only for the
`LUMA_LOADPKG_DEBUG` diagnostic. Reason: the hook is exactly the unreliable
piece the finder replaces (it doesn't fire when `PackageId=0` is cached), so
keeping it as a co-injector adds no coverage and would open a **two-thread
race** on the same `AppIdVec` (the dedup prevents duplicate ids, but NOT the
concurrent realloc). With a single injector (the finder thread) there is no
race and no lock is needed.

**Cadence (v0.13.0):** poll every 2 s **with no cap** until `PackageId=0`
appears (it only exists after login — a slow login must NOT break the
install; the old 5-min cap broke it). After the first hit, keep watching at
15 s and **re-inject** if Steam rebuilds the package (re-login / licence
refresh); `InjectDepots` is idempotent, so each re-check is a no-op unless
the depots have actually gone missing.

### 13.5 The two addresses derived at runtime

The cache-global pointer lives at `GOTbase + X`, and **both** values change
across builds. Five builds measured so far:

| `steamclient.so` | `X` (`disp32`) |
|---|---|
| `7c4ac73e…` | `0x3a1bc` |
| `db0d79c2…` | `0x3967c` |
| `d0c0ff6e…` | `0x3b314` |
| `bc54101b29…` | `0x3b7d4` |
| `237495b4…` (2026-09-08) | `0x3b7d4` |

Every one of them yields **exactly one** distinct `disp32` — which is the
property the whole design leans on, and the reason it is now enforced rather
than assumed (§13.5.a). To avoid per-build constants, the finder derives both
at runtime:

1. **`GOTbase` from the tail of the GMRC prologue.** GMRC's prologue is
   `E8 <call thunk> ; 05 <add eax,imm32> ; 55 89 E5 57 56 53 …`. The
   get_pc_thunk returns `GMRC+5` and the `add eax,imm32` sets `eax = GOT`.
   Fine detail: **lumalinux's own GMRC hook overwrites the leading 5 bytes
   `E8 <call>`** with its detour `jmp`, so the GMRC finder anchored on `E8`
   no longer matches once the hooks are installed — but the `05 add
   eax,imm32` at `GMRC+5` **survives** (the detour is only 5 bytes). So we
   scan for the surviving prologue tail and `GOT = (runtime address of the
   0x05 byte) + imm32` (`DeriveGotBase`, reproduces the exact `.got` VA).

2. **`X` from the cache-access idiom.** Scan `steamclient.so`'s `r-x` span
   for `lea r1, [GOT+X] ; mov r2, [r1] ; mov r3, [r2+root]`. The trailing
   `root` (the tree-root offset of a KNOWN layout, `0xc58` until 2026-09)
   is the anchor that confirms the match; `X` comes out of the `lea`'s
   `disp32` (`FindCacheGlobalDisp`). `cache_global = GOT + X`.

Same philosophy as `derive_patterns.py` (§8) but **at runtime**: zero
per-build offsets, everything reconstructed from stable anchors (the
hook-surviving GMRC prologue tail and the class-layout root offset).

**So why the feed does not break that.** Since 2026-09-08 both numbers come
first from the RVA feed, and the scan runs only if the feed does not carry them,
which seems to contradict the paragraph above. It does not, because what had to
be avoided was not *computing cold*: it was **baking a constant into
`liblumalinux.so`**, which is what forces a release for every Steam build. The
feed bakes nothing —

- it is **indexed by the SHA-256 of the loaded `steamclient.so`**
  (`rva_feed.cpp`, `getFileSHA256` over the real module), so a new build
  simply has no feed entry and **is scanned**: the case "old number applied to
  a new binary" does not exist;
- it is computed by **the same algorithm**, running in CI instead of on the Deck
  (`check_patterns.py` reproduces the runtime predicates, and
  `tools/test_cache_idiom.py` fails if they stop agreeing);
- it is **downloaded from the repo at every start**; it does not travel inside
  the binary.

So it is a **cache of the live computation**, not a substitute for it. The
property that mattered — *a new Steam build does not force a lumalinux release*
— still stands, and is in fact reinforced: before, a broken scan required a
release; now it is fixed by publishing the number.

And the finder still derives live everything the feed does not cover, which is
most of it: walking the tree, finding package 0, cross-checking its identity,
injecting. That depends on Steam's live state, not on the binary, and cannot be
precomputed.

**The cost, which is real:** the scan becomes the rare path — it only runs in
the hours between Valve publishing and the cron passing — and code that almost
never runs rots without warning. It is accepted for two reasons: it is the same
deal DepotKey and GMRC already took when they went RVA-first, and the scan
**does** run daily, in the nightly `check_patterns.py` against the real binary
and in the synthetic tests on every push. It runs in CI, not on the Deck.

What there is **not** is a runtime check of the feed against the scan, and that
is deliberate (`ec9e840` removed exactly that check from DepotKey): checking it
would cost the very scan being saved. Two safety nets remain: the feed only
publishes what CI resolved **UNIQUE**, and if the resulting pointer does not
lead to a coherent package 0, `FindPackage0`'s identity cross-check refuses to
write.

**Since 2026-09-08 both derivations are UNIQUE-or-nothing** (§13.5.a): if the
scan cannot name a single answer it returns none and the finder stands down,
rather than taking the first match. The two classify on **different axes**, and
the distinction matters:

- the **idiom** classifies over distinct `disp32`. Several matching sites are
  normal and healthy — the current build has two, at rva `0xfdd2dd` and
  `0x18964e5` — as long as they name the same `X`.
- the **GOT** classifies over the **derived GOT base**, not over the site
  count. A second PIC prologue of the same shape computes the *same* GOT, so
  counting sites would flag a healthy binary; comparing the derived base does
  not.

Two more properties worth stating, because both were decisions:

- **The base register is deliberately not pinned.** The recogniser requires the
  three instructions to be *chained* (`reg(lea) == rm(mov1)`,
  `reg(mov1) == rm(mov2)`) but does not demand a particular register, because
  the two real sites on the current build use different ones (`esi` and `eax`).
  Pinning `ebx` — the textbook PIC GOT register — would have missed both.
- **The scan runs once.** The bytes it reads are final the moment
  `steamclient.so` is mapped, so a failure cannot become a success by trying
  again: it resolves once and gives up on failure. (The *tree walk* is the
  opposite — genuinely transient, since the cache is populated
  asynchronously — so that one does retry; see §10.)

Scale note, since it justifies preferring the RVA feed: the `r-x` span being
scanned is a **single** `LOAD R E` segment of `0x1ffe574` bytes ≈ **32 MB**.
Both derivations are **feed-first** since 2026-09-08 (`finder.cache_global_disp`
and `finder.got_rva`), each falling back to its own scan, so on a build the CI
has already seen neither scan runs at all.

#### 13.5.a Audit of the locator (2026-09-08)

The locator above was audited in full: the nine functions of
`package_zero_finder.cpp`, its CI surface and the log strings that
`maintenance.md` uses for triage. The initial audit had 3 findings; the full
sweep turned up 20. The list is recorded **with a judgement of usefulness**,
because they do not weigh the same.

Outcome: **13 were fixed in code** (everything that touched the resolution
criterion, CI and triage), **6 were documented as accepted risk** in the
`KNOWN LIMITS` block of `package_zero_finder.cpp` — real nits, but none with a
fix worth its cost — and **1 remains open** because it has no cheap fix: the
double role of `0xc58`. The result was validated live on the SteamOS codespace (§13.5.b).

| # | finding | judgement | status |
|---|---|---|---|
| 1 | `FindCacheGlobalDisp` kept the **first** `disp32` when several disagreed, and built the address it writes to from that number | future-proofing; the measured build was already unique | **done** |
| 2 | the log came out **identical** with agreeing and disagreeing sites, so nobody could tell which case they were in | real: it is why 1 stayed invisible | **done** |
| 4 | nothing checked that the three instructions were **chained**, only their shape | needed together with 1 | **done** |
| 5 | the two branches of the `else` were identical: the "ambiguous" distinction was documented and did not exist | symptom of 1 | **done** |
| 6 | a ~32 MB rescan **every 2 s, forever**, when the scan did not resolve | only on the failure path | **done** |
| 10 | the `maintenance.md` triage quoted log strings that the changes had deleted | self-inflicted | **done** |
| 3 | CI did not compare the `disp32` values it collected: `AMBIGUOUS` passed green | **high** — CI is the only continuous watchman | **done** |
| 9′ | CI recognised **less** than the runtime (opcodes only), and so did `derive_patterns.py`: they approved what the Deck rejects | **high** | **done** |
| 7 | `DeriveGotBase` also returns the **first** match, without requiring uniqueness — and it is **upstream** of 1 | future-proofing | **done** — same treatment as 1, but classifying over the **derived GOT**, not over the site count (see below) |
| 8 | the CI comment justifies not blocking with *"DeriveGotBase finds the right one at runtime"*, which the code does not do | pending with 7 | **done** — both halves: CI blocks (`finder:gmrc_tail:…`) and the runtime fails closed, so the comment now describes the truth |
| 11 | triage searched for `outcome=pattern_miss`, which only `LoadPackage` emitted; DepotKey and GMRC say `outcome=miss` → **it did not catch the two critical hooks**. And it was not one row: there were **five** references in `maintenance.md`, four of them to hooks that never emitted that string | **real, today** | **done** — vocabulary unified on `miss` |
| 12 | the finder does not emit the structured `name=/method=/outcome=` line that the hooks do emit | cosmetic, but it broke `grep outcome=` as a health check: the only injector was invisible | **done** — `Finder resolve: name=PKG0Finder method=… outcome=resolved\|miss` |
| 13 | `PackageInfo` was reached by following seven offsets through a live structure without checking the object's **identity**; the id itself was already read **for the log** and not used: two independent sources, one wasted. *(Nuance: the write path was not bare — `AppendIdsToVec` already rejected an implausible `AppIdVec`. That is plausibility, not identity.)* | **unique to the live path** | **done** — `FindPackage0` cross-checks the id and retries if it disagrees; mitigation of [14] and a fourth option for §10 |
| 14 | five of the seven class offsets are validated by nobody — and they **cannot be validated** in a binary without symbols | accepted risk | see KNOWN LIMITS |
| 15,16,17,19,20,21 | walk consistency, `"r-x"` as a substring, `lo..hi` with gaps, dead branch, maximum depth, magic `0x50` | nits | see KNOWN LIMITS |
| — | `0xc58` plays a **double role**: it identifies the idiom *and* it is the offset we read afterwards. If Steam moves the field, both fail at once and the diagnosis will say "not found" | structural | open |

**The asymmetry with the hooks, half closed (2026-09-08).** DepotKey and GMRC
resolve `feed → pattern (unique or nothing) → rescue`; the finder did not read
the feed at all, even though CI published `finder.cache_global_disp` in every
`res/rvas/*.yaml`, `design/rva-feed-design.md` documented it and steamflipper
corroborated it by another method (the SteamFlipper analysis, a doc retired on
2026-10-08 with the project gone). `grep -rn cache_global_disp src/` returned
nothing: 32 MB were scanned per start to recompute a number already written
down. Now the finder does `feed → scan` (`RvaFeed::CacheGlobalDisp()`), with the
same rule of not revalidating the feed against the scan that was set in
`ec9e840` for DepotKey.

**And the other half, closed too (2026-09-08).** That left the GOT, which was
still derived by scanning because the feed published no RVA for it. Now it does
(`finder.got_rva`, from `verify_gmrc_got`/`distinct_got`) and the finder reads
it through `RvaFeed::GotBase()`. On a build with a complete feed entry the
finder **does not scan code at all**.

The detail that is not symmetric, and where the easy mistake was: the `disp` is
a constant used as is, but the GOT is an **address**, so it has to be translated
through `VaddrXlate` just like a hook RVA. And even so it is not a hook: it falls
in `.got`, which is mapped and writable but **not executable**, so the
`inSteamclientExec()` guard of `Resolve()` would reject every correct value.
`GotBase()` translates like `Resolve()` and validates against the module's
**whole** mapping. Getting that wrong would not have failed loudly: it would
have produced a plausible-looking cache pointer built on a file offset.

The two keys are published **separately**, each according to the verdict of its
own anchor: a build with an ambiguous idiom can have a unique GOT, and
publishing the half we are sure of is better than publishing nothing. The
runtime falls back to the scan number by number, not all or nothing.

**Lesson of method**, because the pattern of the findings says it on its own:
the initial audit looked at **one function**, not the subsystem. The ten that
were missing did not turn up over time — they were all there, and were stumbled
on one by one during the implementation. What was missing was sweeping the
whole file, checking code against documentation, and comparing CI against
runtime in both directions. All three are mechanical.

#### 13.5.b Live validation of the rewritten locator (2026-09-08)

Everything in §13.5.a was validated **on the SteamOS codespace, with Steam
running and a logged-in session**, not only in CI. It matters to write it down because it is
the only proof there is: `build.yml` compiles, `check_patterns.py` resolves on
paper, and `verify-fix.yml` — the workflow that should start the `.so` — has
never had a green run (its first real executions, that same day, all failed in
the harness before reaching any assertion).

Recipe: SteamOS codespace, stack installed normally from LumaDeck
(`setup.sh`), and then **only** `liblumalinux.so` replaced by the branch's
build. It is written step by step in `maintenance.md` §C.

The session's `steamclient.so`: `237495b4…`. What came out of the log:

```
PKG0_FINDER: GOT UNIQUE — 1 site(s), got=0x…
PKG0_FINDER: cache-access idiom UNIQUE — 2 site(s), disp=0x3b7d4
Finder resolve: name=PKG0Finder method=… got=0x… disp=0x3b7d4 … outcome=resolved
PKG0_FINDER: HIT pkg=… PackageId=0 AppIdVec{size=196}
```

and the injected appids starting with `{5,7,8,90}`. That is: both anchors
resolved unique, the new identity cross-check (finding 13) passed —
`PackageId=0` read from the object matches the node's key — and the injection
went through to the end with a plausibly sized `AppIdVec`. `3/3 hooks active`
on the same start, so none of this broke DepotKey, GMRC or ShaderDepot.

**What is still not exercised, and it has to be said:** everything validated is
the success path. The failure paths — `method=rva` (reading the `disp32` from
the feed instead of scanning), the two `AMBIGUOUS` branches, the thread ending
after a failed scan, and the finder's `outcome=miss` — have been run by nobody
but the synthetic tests in `tools/test_cache_idiom.py`. They are precisely the
ones only seen on the day Steam breaks something, so the synthetic test is the
only thing that covers them.

**How the test harness itself was verified.** The four scans of the idiom
(runtime, `check_patterns`, `derive_patterns`, the probe) were checked against
a synthetic ELF and agree. And to rule out that the tests were decorative,
**mutation** was done: altering the predicates one by one, the tests fail
(8/2/1 failures depending on the mutation) instead of passing green. A test
that does not bite when you break what it guards is not a test, and that
mistake had already been made in this repo — `verify-fix.yml` had been "green"
for months because the run being cited belonged to a different workflow.

#### 13.5.c The layout table (2026-09-22)

The root offset is not one number
any more. The `1790036264` beta (`9cf4720f…`, desktop and Deck public-beta
manifests ship the same file) recompiled `steamclient.so` (49.8 → 53.5 MB) and
both anchors came back NOT_FOUND. Measured with
`tools/experiment_cache_idiom_free.py` (root offset left free, then a
field-access histogram of the cache singleton on both builds):

| | `bc54101b` (stable) | `9cf4720f` (beta) |
|---|---|---|
| loads of the cache pointer | 598 | 607 |
| package-tree root (`RBTREE-SIG`, 3 sites) | `0xc58` | `0xf90` |
| node array | `0xc6c` | `0xfa4` |
| second tree root (`RBTREE-SIG`, 39 sites) | `0xc98` | `0xfd0` |
| its node array (8 sites) | `0xcac` | `0xfe4` |
| most-read field (40 / 42 sites) | `0xa70` | `0xda8` |
| GMRC `sub esp,imm32` | `0x110` | `0x120` |
| `X` | `0x3b7d4` | `0x3c7b0` |

26 fields matched one-to-one, same site counts, same signatures, **every one
of them exactly `0x338` higher**: Valve inserted an 824-byte member ahead of
the trees and changed nothing else. The finder's exact idiom exists at
`0xf90` with 2 sites (`lea eax,[esi+X] ; mov ecx,[eax] ; mov edx,[ecx+0xf90]`,
the same registers as on stable), and the GMRC prologue only grew its frame.
So `kCacheRootIdxOff`/`kCacheNodesOff` became `kCacheLayouts`, a table scanned
row by row with a two-level UNIQUE-or-nothing (exactly one row with sites, one
`disp32` within it; stable has 0 sites at `0xf90`, the beta 0 at `0xc58`), the
tail's frame bytes became wildcards, and the RVA feed carries the resolved row
(`finder.cache_root_off` / `cache_nodes_off`) so a Deck with the feed walks the
right struct without re-scanning. The four copies of the scan carry the same
table (`maintenance.md` §C). The SDK's `linux32/steamclient.so` of the same
version has the same layout (`X = 0x3dcdc`), so the change is in Steam's
source, not one compile. Ahead of that, the cache object also holds a second,
much busier tree `0x40` above the package tree (probably the app-id index) —
the same fingerprint in every build, which is what the free-offset probe keys
on. `Reconcile`'s pattern broke on the same build for an unrelated reason: it
pinned the register of one temporary (`edi`); the two ModRM bytes are
wildcarded now and the pattern is UNIQUE on both builds (stable `0x188c950`,
beta `0x1a0d010`, the latter agreeing with steam-monitor's locator).

### 13.6 End-to-end verification (Brotato, 1942280)

Clean test on the codespace with v0.10.11 and a fresh zip. The runtime
derivation hit on the first try and the full pipeline went through:

```
PKG0_FINDER: cache-access idiom found 2 time(s), disp=0x3a1bc
PKG0_FINDER: GOT=0xd4dbea04 disp=0x3a1bc cache_global=0xd4df8bc0
PKG0_FINDER: HIT pkg=0xd7c59380 PackageId=0 AppIdVec{mem=… size=192 alloc=202}
PKG0_FINDER:   AppIdVec[0..4) = {5, 7, 8, 90}        ← sanity OK (real app ids)
LoadPackage[finder]: APPENDED 8 depot id(s) to PackageId=0 (size 192 -> 200)
  + depot 1942280 1942281 1942282 1942283 2379780 2379781 2379782 2868390
```

The rest of the chain, in order:

- **BuildDep** surfaces the content depots (`2868390`, `1942282`) with their
  gids.
- **DepotKey** serves the local keys: `SERVED local key for depot 2868390 /
  1942282 / 1942280`.
- **GMRC** injects the request code: `got code … INJECTED for manifest …
  (depot 1942280)`.
- Steam's `content_log.txt`: chunks downloaded for the content depots →
  `starting commit … → steamapps/common/Brotato : 6 updated` →
  `finished update, 2 mounted depots : 2868390, 1942282` →
  `state changed : Fully Installed`.

The "post-Steam-update regression" is genuinely fixed: the finder replaces
the v0.5/v0.7 wrapper that depended on an event that no longer happened.

### 13.7 Brotato yes, Balatro no — and why this was NOT a lumalinux bug

The same flow had previously failed for Balatro (2379780) with
`Missing decryption key`. The difference wasn't lumalinux — it was the zips:

- **Brotato**: `keys.txt` had a **real** key for the shader-cache depot
  (`1942280`). The hook returned `SERVED local key for depot 1942280` → the
  shader decrypted → the pipeline kept going.
- **Balatro**: `keys.txt` had `2379780;` (no key) for its shader depot.
  Steam asked for that key → the hook fell through → the server denied it
  → `Missing decryption key` → Steam **cancelled the whole app**.

In other words: Balatro's zip was **incomplete** (missing the shader-depot
key), not a finder regression. Lesson for §6: a `Missing decryption key` on
the shader-cache depot kills the whole install even when every content
depot is fine — before blaming the hooks, verify that `keys.txt` has a key
for **every** depot Steam will ask for, including the shader-cache one (its
id is typically the app id itself, e.g. `1942280` for Brotato, `2379780`
for Balatro).

**Update (2026-06): the shader-depot `Missing decryption key` can be transient,
not always a hard abort.** Re-running the Balatro case with the same incomplete
zip (`2379780;` no shader key) on the canonical stack (Steam native Arch +
SLSsteam via Headcrab + lumalinux v0.13.5 release), the first install attempt
still failed with `Missing decryption key, state 0x40a`, but Steam **re-queued
it (300 s delay) and the retry succeeded**: it downloaded depot 2379781,
committed, and reached `StateFlags=4`. The reason it recovers is §11.5 — once the
`.acf` is `StateFlags=4`, Steam **skips the shader pre-cache entirely**, so the
missing shader key is never requested again. So the rule is: a missing shader key
can fail the *first* attempt (during `StateFlags=1`), but Steam's own retry,
landing on `StateFlags=4`, can still complete the install. A real shader key is
still the clean fix; the abort just isn't always terminal.

### 13.8 The shader-cache key is real and *keyable* — Hubcap just doesn't always ship it (2026-06-23, Deck)

Inspecting the Hubcap `.zip`s of five games on real hardware settled where the
shader-cache key comes from. The shader pre-cache depot's id is the **app id**,
its manifest is **never** in the zip (Steam generates it per-GPU), but the
**decryption key** for it sometimes *is* in the `.lua` and sometimes isn't:

| Game (app id) | `.lua` main-app line | shader key? | result |
|---|---|---|---|
| Brotato (1942280) | `addappid(1942280, 1, "<key>")` | **yes** | shader decrypts → installs clean |
| Formula Legends | `addappid(<id>, 1, "<key>")` | **yes** | clean |
| Balatro (2379780) | `addappid(2379780)` | no | `Missing decryption key` |
| CrossCode (368340) | `addappid(368340)` | no | `Missing decryption key` |
| Blasphemous (774361) | `addappid(774361)` | no | `Missing decryption key` |

So the shader depot **is keyable**: when the `.lua` carries
`addappid(<appid>, 1, "<key>")`, the DepotKey hook serves it, GMRC supplies the
manifest code (§11.5), the shader content decrypts, and the pre-cache completes.
Whether the key is present is **purely a property of the Hubcap snapshot** —
Brotato's recent snapshot (May 2026) shipped it; CrossCode's (Sep 2025) and
Blasphemous's (Oct 2025) did not, and those games no longer receive Hubcap
updates, so a keyed snapshot will never arrive. The only way to get a real
shader key for such a game is to **capture it from an account that owns it**
(depot keys are version-stable — capture once, valid forever; this is moon's
`recvDepotKey`).

**Correction to the §13.7 "transient" claim.** The 2026-06-23 Deck logs show the
shader `Missing decryption key` is **not** confined to `StateFlags=1`, and Steam
does **not** reliably skip the shader pre-cache once `StateFlags=4`. For
CrossCode and Blasphemous, with the game **Fully Installed** (`state 0xc`), Steam
re-ran the **Shader update** every ~5 min (`Shader update changed: Running
Update … → Missing decryption key`) — a recurring background loop, not a
one-time install transient. The game installs and plays, but the loop spams.

**Attempt 1 — gated zero key (v0.13.7, REVERTED).** lumalinux first served a
32-byte zero placeholder for presence-only depots (`KeyStore::IsPresenceOnly`)
in `depot_key_hook`. It *did* stop the post-install `Missing decryption key`
loop (the placeholder is terminal, Steam doesn't re-queue). **But on a cold
install it regressed:** the shader depot's **manifest** is itself encrypted with
the real key, so with the zero key Steam logged `Failed to decrypt cached
manifest 368340_…` and — because the shader runs as "Shader Priority" —
**suspended the whole install** (`Invalid content configuration`, app stuck at
`Update Paused` until a manual "resume"). Deck-confirmed 2026-06-23: CrossCode
reinstall paused mid-download, only finishing after a manual resume that
re-planned content-only. Faking the key is therefore wrong: you can't decrypt
the manifest without the real key, so any fake just moves the failure later and
makes it block the install. Reverted in v0.13.8.

**Attempt 2 — exclude the keyless shader depot from package-0 (v0.13.8, FAILED).**
Dropped the presence-only app-id/shader depot from the package-0 injection, on
the hypothesis that an *unlicensed* shader depot would make Steam skip the
pre-cache. **Deck-disproven 2026-06-24:** the exclusion ran
(`excluded 2 presence-only depot(s)`) but Steam **still** ran the shader update
for 368340 (`Missing decryption key`). So the shader pre-cache is driven by the
**appinfo** depot list, **not** the package-0 licence filter — and lumalinux
deliberately does not touch appinfo (that's SLSsteam's message layer). Reverted
in v0.13.9.

**No per-app *config* knob exists.** config.vdf's `ShaderCacheManager.App.<appid>`
holds only `ShaderCacheSize` (informational), not an enable/disable. Steam
exposes **no** per-game shader-cache control *via config*. The only config lever
is the global `"DisableShaderCache" "1"` in the `ShaderCacheManager` block —
which is exactly what Steam's own "Shader Pre-Caching" toggle writes (verified on
Deck) and what moon writes. (A per-game skip *is* possible — just not through
config: it needs a function hook inside the client. That's §13.10.)

**Resolution (v0.13.9 — interim, superseded by v0.14.0/§13.10).** v0.13.9
reverted the `.so` to plain pass-through for keyless depots (no placeholder, no
package-0 exclusion) and suppressed the doomed shader pre-cache by writing the
**global** `DisableShaderCache=1` into config.vdf at add-game time
(steamidra_lite), matching the Steam toggle / moon. That worked but was blunt:
keyed and owned games lost Valve's precompiled-shader download too. **v0.14.0
replaced it with a per-game skip** (the ShaderDepot hook, §13.10). As of v0.16.x
steamidra_lite **no longer writes or reverts `DisableShaderCache` at all** — the
flag is left entirely to the user (it *is* Steam's own "Shader Pre-Caching"
setting) and the hook handles keyless games per-game. (v0.14–0.16 had a
`restore_shader_precache` that reverted a legacy `1→0`, but it could not tell an
inherited kill from a deliberate user-off, so it was removed — see §13.9.) Note
the framing in this subsection — "making the shader pre-cache
*work* for a keyless game is impossible without the real key" — is still true,
but it was the wrong goal: §13.10 doesn't make it work, it makes Steam **skip it
cleanly**, which is all a keyless game ever needed.

### 13.9 Path B — reverse-engineering the manifest-decrypt path for a per-game skip (2026-06-24)

The global `DisableShaderCache` (§13.8) works but is a blunt instrument. Path B
was the research goal: make Steam's shader pre-cache skip *cleanly, per game*,
for a keyless title — by hooking the function that decrypts the cached manifest
and making the keyless shader depot resolve to "nothing to do" instead of an
error. This subsection records what the RE found, because the conclusion
changes the design.

**Target located.** On build `7c4ac73e` (the user's Deck steamclient.so,
sha256 `7c4ac73e…`), the function that logs
`Failed to decrypt cached manifest %d_%llu` (string at VA `0xdb0538`) is at
**VA `0xfa9050`**, in `applicationmanager.cpp` (an in-function assert names
`pCachedManifest->m_pManifest->BIsFinalized()` at
`/data/src/clientdll/applicationmanager.cpp`). It is reached by two callers
(`0xfe0940`, `0xfe0c95`). The string-load instruction is
`lea eax,[esi-0x20e74cc]` at `0xfa9386`; with the i386-PIC base
`esi = 0x2e97a04` (`.got`), `0x2e97a04 - 0x20e74cc = 0xdb0538`, confirming the
xref.

**What the function actually is.** `0xfa9050` is **not** a simple
decrypt-and-return; it is a **job/coroutine state machine** over a
`CCachedManifest` entry. It:
1. looks the entry up (`call 0xfa8b40`) and walks a sorted by-manifest-id list;
2. branches on the entry state byte `[entry+0xc]` (`0x16` = yield/wait via the
   job scheduler `0x2a5d060`; `1` = load; anything else = `return NULL`);
3. builds the path `"%s/depotcache/%d_%llu.manifest"`, reads + **decrypts** it
   (the decrypt is `call 0x29b5f10`, returns a bool in `al`);
4. on decrypt **success** sets `[entry+8]=1` and runs validation
   (`0x24fc030` + CRC); on **failure** logs our string and leaves `[entry+8]=0`;
5. **returns `[entry+4]` in both success and failure** — failure is *not*
   signalled by the return value. The only `NULL` return is the
   "wrong state" path (`0xfa92f0`).

So the verdict (ok vs. failed) is carried in **mutated object state**
(`[entry+8]`, and downstream `[ecx+0xbd]` read by the caller), not a return
code. Both callers do `test eax,eax; je …` then immediately read those state
bytes — i.e. they trust the object, not the return.

**The decrypt callee `0x29b5f10`.** Single arg (`CCachedManifest*` at
`[ebp+8]`), returns bool in `al`. It checks flags `[arg+0xbc] / 0xbd / 0xbe`,
then iterates `[arg+0xa8]` entries at `[arg+0xb0]` decrypting each chunk/file
name. Returning `1` from a hook here would make `0xfa9050` *believe* the
decrypt worked — but the manifest body is still encrypted garbage, so the
downstream `BIsFinalized()` assert, the CRC check (`0x24fc030`) and the chunk
walk would then fail or, worse, drive Steam to request chunks that do not
exist. **Faking the decrypt return is therefore insufficient.**

**Conclusion — Path B is blocked by manifest-internals fragility, not by not
finding the function.** A clean per-game skip needs the keyless shader depot to
resolve to a **valid, finalized, zero-chunk `CManifest`**. There is no
return-value seam that produces that; you would have to *fabricate* Steam's
internal `CManifest` object (finalized flag, empty chunk/file vectors, valid
CRC) and splice it into the `CCachedManifest` wrapper. That layout is
build-specific (offsets `0xa8/0xb0/0xbc/0xbd/0xbe`, `[entry+8]/0xc`, the
`m_pManifest` sub-object) and would break on every Steam update — exactly the
fragility lumalinux's pattern discipline (§8) exists to avoid. It is also a far
larger and riskier hook than anything currently shipped.

**Recommendation (at the time).** Do **not** ship a manifest-fabrication hook —
forging finalized manifest internals is too fragile. Path B is *located but
deliberately not pursued*. **What actually shipped instead** is path C (§13.10):
one level up the call stack there is a clean, single-value seam
(`GetShaderCacheDepot`) that routes keyless games down Steam's *own* skip path —
no fabrication, and it superseded both path B and the global toggle. (If a future
build ever exposes a real per-app shader knob, or we capture a real shader-depot
key from an owner per §13.8 top, those are cleaner still; manifest forgery never
was.)

**Artifacts.** Addresses in this subsection are for build `7c4ac73e` only and are
recorded for provenance — lumalinux ships **no** hook on the path-B (decrypt)
seam; the shipped shader hook is path C's `GetShaderCacheDepot` (§13.10). The
steamclient.so binary was transferred to a throwaway `tmp/steamclient-bin` branch
for the RE session and has since been deleted.

### 13.10 Path C — the per-game shader skip that actually shipped (v0.14.0)

> **Since then:** the failure mode below no longer holds as written: since 2026-09-22 ShaderDepot resolves feed → pattern → string-anchor rescue on `"shadercachedepot"` (`src/hooks/shader_depot_hook.cpp`, `Install`), and CI re-derives the pattern on new builds. The hook also has a fourth branch: a keyed game of ours returns 0 when the GMRC hook is not installed (§13.11 adds the provider check). See [`nosotros.md`](nosotros.md) §2.1.

Path B asked "how do we make the keyless shader manifest *decrypt*?" — the wrong
question. The right one is "how do we stop Steam from *trying* for this one
game?" Continuing the RE up the call stack from §13.9 answered it.

**The seam.** `CGetShaderDepotManifestJob::BYieldingRunClientJob` (VA `0x1725a00`
on build `7c4ac73e`) decides whether to run the shader pre-cache for an app:

```
id = GetShaderCacheDepot(appinfo);     // call 0xfd9ca0
if (id == 0)
    -> "[ AppID %u ] CGetShaderDepotManifestJob skipping because shader depot
        ID is invalid."   -> return cleanly (code 0xb). No error, no pause,
                              no "Invalid content configuration", no 300 s loop.
else
    -> "[ AppID %u ] Getting shader depot manifests"  -> download + decrypt
       (which is what FAILS for a keyless game and triggers §13.8's symptoms).
```

So Steam **already has a clean per-game skip path** — it just needs the app's
shader depot id to read as `0`/invalid. We don't fabricate anything (unlike
path B); we route the keyless games down Steam's own skip.

**`GetShaderCacheDepot` (`0xfd9ca0`).** Single-arg cdecl accessor. Reads the
shader-manager global (`[GOT+0x2b758] -> +0x44`; returns 0 if absent), then the
app's PICS appinfo KeyValues `appinfo.game.shadercachedepot`, and returns that
depot id (`== app id` by convention), or 0 if the app isn't a game / has no
shader depot. **Its only caller is the shader job above** (verified: a single
`E8` xref at `0x1725a99`), so changing its result has exactly one effect — the
job runs or it skips. Nothing else reads it.

**The hook (`src/hooks/shader_depot_hook.cpp`, `Hooks::ShaderDepot`).** Call the
original; if the returned id is one of *our* keyless games
(`KeyStore::IsPresenceOnly(id)` — present in `keys.txt` with no key, i.e. the
app-id/shader depot of a lumalinux-added game), return `0`; otherwise pass the
real id through. Result:

- keyless game (Balatro/CrossCode/Blasphemous) → id 0 → Steam logs "skipping…"
  and ends the shader job cleanly. Per game. No loop, no suspend.
- game that ships a real shader key (Brotato/Formula Legends) → not
  presence-only → real id passes through → shader pre-cache runs and decrypts.
- the user's genuinely-owned Steam games → not in `keys.txt` at all → real id
  passes through → shaders unaffected.

Pattern `kShaderCacheDepotPattern` (prologue `57 56 53 E8…81 C3…8B 83 ?? ?? ??
?? 8B 40 44 85 C0 75 0D 5B 31 C0`), verified UNIQUE on `7c4ac73e`. The
`mov eax,[ebx+0x2b758]` disp32 is **wildcarded** on purpose: it's a GOT-relative
offset to the shader-manager global, and such offsets shift between Steam
rebuilds (the package-0 cache global moved `0x3a1bc → 0x3967c` across two builds,
§13.5). Wildcarding it — verified to stay unique — removes the byte-pattern's
single most-likely break point on a future update, leaving the identity anchored
on the `57 56 53` prologue and the distinctive `8B 40 44 85 C0 75 0D 5B 31 C0`
tail. Collision check: SLSsteam hooks the message dispatch, not shader functions
— clean.

**Why this beats the global `DisableShaderCache` (§13.8).** The global flag
killed Valve's precompiled-shader download for *every* game (keyed and owned)
too; path C touches only the keyless games that can never use it anyway.
`steamidra_lite` therefore stops writing the global flag and, as of v0.16.x,
**no longer touches `DisableShaderCache` at all** — the hook governs the keyless
games, and the flag is left entirely to the user (it *is* Steam's own "Shader
Pre-Caching" setting). An earlier `restore_shader_precache` that reverted a
legacy `1 -> 0` on every install was removed: it could not tell a legacy kill
from a deliberate user-off, so it stomped the latter. The global flag remains a
manual fallback the user can toggle if the ShaderDepot pattern ever breaks (a
pattern miss is non-fatal: the hook just doesn't install and keyless games fall
back to the §13.8 shader loop — installs are unaffected either way; LumaDeck's
update gate keeps such a build off Stable users).

**Failure mode on a Steam update.** If `kShaderCacheDepotPattern` stops matching,
`ShaderDepot` shows FAILED in the startup toast (it's a counted core hook) and
keyless games regress to the §13.8 loop until the pattern is re-derived
(`GetShaderCacheDepot` is anchored by the strings `shadercachedepot` /
`CGetShaderDepotManifestJob skipping because shader depot ID is invalid`).

**End-to-end verification (2026-06-24, Deck, v0.14.0 release).** Both directions
confirmed on real hardware, not by reasoning:

*Keyless games skip cleanly.* Startup logged `4/4 hooks active` and
`ShaderDepot hook: INSTALLED … rva=0x1a5ca0`. For CrossCode (368340) and
Blasphemous (774361) the hook fired
`ShaderDepot: shader depot id <id> is keyless … returning 0`. The decisive
check is the absence afterwards: the last shader entry in `content_log.txt` for
368340 was `… 09:20:27 … scheduler finished: removed from schedule (result
Missing decryption key, state 0x41a)` — that's the OLD failure, *before* the
hook loaded at 10:45. From 10:48 (first `returning 0`) to 10:57 (`date`) there
were **zero** new shader lines for 368340: a live loop would have re-fired at
~10:48, ~10:53, … The loop is gone, and the game launches and plays.

*Keyed games keep their shaders.* For Brotato (1942280 — its `.lua` ships a
shader key) the hook **never** fired (1942280 never appears in a `returning 0`
line; it isn't presence-only). DepotKey served its key
(`SERVED local key for depot 1942280 (KeySize=128)`), GMRC injected the codes,
and `content_log.txt` shows the shader pre-cache running for real:
`Shader update changed: Running Update,Preallocating,Downloading,Committing` →
`starting commit … shadercache/1942280/downloads/ → shadercache/1942280/ :
6 updated` → `Shader update changed: None` → `finished update, 2 mounted depots
: 2868390, 1942282`. No `Missing decryption key`. So the gate discriminates
exactly: keyless → skip, keyed → run-and-decrypt.

This is the first known clean, per-game shader-pre-cache skip for a keyless
title — using Steam's own skip path, no global toggle, no manifest forgery —
and it is verified on hardware in both directions.

### 13.11 Code-availability-gated skip for KEYED shaders (v0.16.0)

§13.10 skipped the shader pre-cache only for **keyless** games. Keyed games
(Silksong, Brotato, Formula Legends — the common case for modern titles: their
`.lua` ships `addappid(<appid>, 1, "<key>")`) were let through to pre-cache
normally. That is correct **when a GMRC provider is up**, but it leaves one
cosmetic wart: the shader/app depot's manifest is **never** in the Hubcap zip
(§13.8, confirmed across Silksong/Brotato/Formula Legends/Binding-of-Isaac zips —
none ship a `<appid>_*.manifest`), so the shader job must fetch the manifest
request code live. If **all** providers are down at that moment, Valve's CDN
denies the manifest and Steam shows **"No internet connection"** (the Silksong
symptom: wudrm fully down, no opensteamtool yet — the game installed from its
content depots but the shader code fetch flashed the popup).

Pre-seeding the shader manifest at add-time was considered and rejected: the
shader cache is a **moving target** — it is regenerated on GPU/driver/SteamOS/
client updates, not just game-version bumps (community reports of ~daily churn),
so a pre-seeded manifest goes stale almost immediately and can't be pinned (its
gid isn't in the `.lua`). Globally skipping keyed shaders was also rejected: on
Deck/Proton the shader pre-cache genuinely reduces first-run stutter for
DX-under-Proton titles, so we do **not** want to discard it (unlike slsteam-moon's
global `disableShaderCache()`, or the pre-v0.14 `DisableShaderCache` flag).

The shipped fix makes the skip **conditional on code availability**, decided at
`GetShaderCacheDepot` time (before the job requests the code):

- keyless (presence-only) → skip, as §13.10.
- not ours (owned games) → pass through, as §13.10.
- **keyed & ours** → `Gmrc::ProvidersReachable()` probes the provider cascade
  (throwaway gid, short 3s/5s timeouts, HTML/Cloudflare-challenge body rejected,
  result cached 15s). Provider up → return the real id (shaders pre-cache
  normally). **All down → return 0** → Steam takes its clean skip path, never
  issues the code request, and the popup never appears.

Net: a keyed game **never** flashes "No internet connection". Shaders are lost
only in the exact case that would have flashed today (all providers down at
install), and recover on the next install/update when a provider is back up —
strictly better than the pre-v0.16 behaviour. The residual edge is a race
(probe sees a provider up, the real fetch then fails); closing it fully would
require pre-fetching the code for the shader gid read from `appinfo` ("Level 2",
not shipped — the probe covers the overwhelming majority).

## 14. Auto-update end-to-end validation (2026-06, Balatro + Vampire Survivors)

*(2026-06-18, v0.13.5)*

Validated, on the canonical stack (native Arch Steam + SLSsteam via
enter-the-wired/Headcrab + lumalinux v0.13.5 **release** loaded by the
`install.sh` `steam.sh` patch — not env-var injection), that a game installed
**pinned to an old manifest** auto-updates to Valve's current version once the
pin is removed. Build `7c4ac73e`. This is the concrete proof behind
[`nosotros.md`](nosotros.md) §2.5 (game updates).

### 14.1 The recipe that worked

> **Since then:** this is not how pinning works any more. A pin is SLSsteam's `ManifestIds` (depot → gid), written by `steamidra_lite --pin`/`--set-pin`/`--unpin` and managed by LumaDeck's `pins.py`; the BuildDep hook that read the gid from `keys.txt` is off by default (v0.16.10), and Steam does not read `config/depotcache/` (§19.3). See [`nosotros.md`](nosotros.md) §2.5.

Per content depot: (1) `keys.txt` gid+size → `0` (key kept); (2) comment
`--setManifestid` in `config/stplug-in/<appid>.lua` (interop); (3) **delete the
depot's `depotcache/` + `config/depotcache/` manifests**; (4) restart Steam.
Step 3 is non-obvious and **required** — a leftover cached old manifest is reused
and no update happens.

### 14.2 Who does what (from the logs, correlated with `content_log.txt`)

- **SLSsteam** — `Added <appid> to AdditionalApps` + `PlayNotOwnedGames: 1`.
  That fakes ownership, so Steam treats the app as subscribed and **checks PICS
  for updates** like any owned game. (It logs nothing per-depot; LogLevel 2.)
- **lumalinux LoadPackage/finder** — `PKG0_FINDER: HIT PackageId=0` then
  `all N depot ids already in PackageId=0`. **Confirms the `gid=0` depots stay
  injected**: `GetAllDepotIds()` doesn't filter on gid, so an unpinned depot is
  still surfaced and Steam still associates it with the app.
- **lumalinux BuildDep — passthrough.** Before unpin it logged
  `BuildDep: PATCH … gid <old> -> <old>`; after unpin it logs
  `app <appid> … none required patching` and **no PATCH line** for the unpinned
  depots → Steam keeps Valve's real current gid → schedules the update.
- **lumalinux DepotKey** — `LoadDepotKey: SERVED local key for depot …` for the
  new content (same key decrypts the new manifest; depot keys are version-stable).
- **lumalinux GMRC** — the new manifest is *not* in `depotcache` (nuked), so Steam
  requests its code at runtime and GMRC supplies it. This is the §11.5 /
  nosotros.md §2.5 "GMRC stays load-bearing for updates" path, exercised for real.
- **Steam native client** — downloads the delta, decrypts, commits:
  `finished update, 2 mounted depots : 1794681 (7054…), 1794685 (7852…)`.

### 14.3 Notes worth keeping

- **Per-depot detection, not buildid.** Both games kept the *current* `buildid`
  in the `.acf` (Steam stamps the live buildid at install even under a pinned old
  manifest); the update still fired because Steam compares the **per-depot
  manifest GID** against PICS. See nosotros.md §2.5.
- **The injection vehicle matters.** This worked because lumalinux was in the
  live 32-bit client via the `steam.sh` LD_PRELOAD patch (§5). Injecting
  `LD_PRELOAD` as a bare env var on the `steam` command instead does **not**
  survive Steam's client re-exec — the lib loads only into the launcher/wrapper
  processes, the real client never gets the hooks, and the install silently
  produces a 0-byte "phantom" `.acf`. Use the canonical patch, not env vars.
- **Multi-depot installs do happen.** Contrary to the single-depot Balatro case
  in §10, Vampire Survivors mounted **two** content depots (1794681 Windows-
  content + 1794685 Linux) on a Linux install; both pinned, both auto-updated.

## 15. RTTI-based update-resilient resolution (CloudRedirect's technique)

*(2026-06-26, v0.15.x; DepotKey slot derivation added 2026-07-06 and 2026-07-23, v0.16.x)*

> **Since then:** the "Status" paragraph, §15.3's "IMPLEMENTED" item and the end of §15.4's note no longer describe the runtime. `ResolveVtableSlotBySignature` was removed on 2026-09-08 (it was the byte pattern under another name); DepotKey now resolves RVA feed → `kDepotKeyFnPattern` → `Rtti::ResolveVtableSlotByName` as the last-resort rescue (`src/hooks/depot_key_hook.cpp:128-176`, `src/rtti.cpp:289`). See [`nosotros.md`](nosotros.md) §2.1. The technique (§15.1), the f5eb8bd3 measurements (§15.2) and the caveat (§15.4) remain valid as history.

**Context.** CloudRedirect's June-2026 release stopped needing an update per
Steam client build ("CR no longer requires an update for every Steam client
update"). Worth copying where it fits, because lumalinux's byte-pattern hooks
move on every Steam build (§8) and the SafeMode hash gate needs a per-build
whitelist entry (v0.15).

**Status: DepotKey has adopted this, and now DERIVES the slot.** It resolves via
`CConfigStore`'s vtable at runtime (`src/rtti.cpp` `ResolveVtableSlotBySignature`),
**deriving** the accessor's slot by matching `kDepotKeyFnPattern` inside the vtable
— no hardcoded index (was slot 6). RTTI locates the vtable; the byte pattern
identifies which slot is the accessor, so a vtable reorder is handled (the accessor
is found wherever it moved) instead of silently mis-resolving. The pattern is kept
as a fallback for RTTI-walk failure. The cron (`tools/check_patterns.py`) now mirrors
this statically and confirms per build that exactly one slot matches (the automatic
multi-build ground-truth #19 was blocked on — see §15.3). GMRC and BuildDep remain
byte-pattern (§15.3 for why).

### 15.1 The technique

CR does NOT scan byte patterns or hardcode RVAs. It resolves the hook target by
**C++ RTTI**, from anchors that are stable across builds:

1. Find the **RTTI typestring** (the Itanium-mangled class name, e.g.
   `30CClientUnifiedServiceTransport`) in `.rodata` — never relocated, never
   renamed.
2. Find the **`type_info`** struct whose name pointer points at that string.
3. Find the **vtable**: the Itanium ABI header `[offset_to_top=0, type_info*]`
   followed by the virtual function pointers.
4. Hook a **fixed vtable slot index**.
5. For RPC transports, dispatch inside the hook by the **RPC method-name string**
   (e.g. `Cloud.ClientFileDownload#1`) — also stable.

None of {class name, slot index, RPC name} is an address, so none moves per
build. An update is only needed if Valve *reorders virtuals* or *renames* a
class/RPC. Ref: `src/platform/linux/vtable_hook.cpp` (`FindTransportVtable`).

### 15.2 Applicability to lumalinux's hooks (verified on build f5eb8bd3)

Each hook target checked against the steamclient.so vtables (RTTI resolution
mimicking CR's `FindTransportVtable`):

| Hook | Target RVA | Virtual? | Update-proof path |
|---|---|---|---|
| **DepotKey** | `0x1188300` | **YES — `CConfigStore` vtable (slot 6 today)** | RTTI → vtable → slot DERIVED by `kDepotKeyFnPattern` (not hardcoded) |
| **GMRC** | `0x1351890` | no (but it's a named RPC) | transport RTTI + dispatch by `ContentServerDirectory.GetManifestRequestCode#1` |
| **BuildDep** | `0xfe1bf0` | no | none — stays pattern/string-anchored |
| **ShaderDepot** | `0x102d9f0` | no | none — stays pattern (non-critical) |
| LoadPackage | `0xfce960` | no | none (diagnostic-only) |

Evidence:
- `12CConfigStore` @0xb892e8 → typeinfo @0x2e65f0c → vtable @0x2e65e8c (26 slots);
  **slot 6 = 0x1188300**, exactly the DepotKey accessor hooked today (matches the
  `vtable[+0x18]` note in §12 / patterns.hpp — 0x18 = slot 6).
- `30CClientUnifiedServiceTransport` @0xb7bf20 → vtable @0x2e63cd0 (11 slots); CR
  hooks slots 5/7/8 (RPC send/dispatch). The GMRC RPC name
  `ContentServerDirectory.GetManifestRequestCode#1` and its protobuf
  Request/Response classes are present in the binary.

### 15.3 What this buys, and the cost

> **Since then:** the first item ("✅ IMPLEMENTED") is no longer true: the signature-derived slot was removed from the runtime on 2026-09-08 and RTTI is used only by name, as DepotKey's last-resort rescue after the feed and the pattern (see the note at the top of §15).

- **DepotKey → RTTI vtable, slot DERIVED by signature — ✅ IMPLEMENTED.**
  `src/rtti.cpp` `ResolveVtableSlotBySignature` (built on the same
  `FindVtableByRTTIName`-style walk as CR, resolve-only) finds `CConfigStore`'s
  vtable, then scans it for the UNIQUE slot whose prologue matches
  `kDepotKeyFnPattern` — no hardcoded index. `depot_key_hook.cpp` installs the
  existing detour (§4) at the derived slot and logs `rtti_slot=` for drift
  detection; the byte pattern is kept as a fallback (RTTI-walk failure, e.g.
  relocations not yet applied) and IS the derivation signature. This drops the
  hardcoded slot 6 (the last robustness gap of the fixed-index approach — §15.4)
  and the CI-side gap: `tools/check_patterns.py` now walks the vtable statically
  and BLOCKS the whitelist unless exactly one slot matches (zero/multiple/walk
  failure → exit 3), so the RTTI primary path is verified per build, not just the
  fallback. Because the cron fetches Valve's latest build (independent of
  headcrab's client pin) and the check is static, every build now auto-accumulates
  the ground-truth #19 was blocked on. Validated: derives slot 6 (`0x1188300`)
  uniquely on builds f5eb8bd3 / 1782861641. Remaining in #19: dropping the pattern
  entirely once enough builds confirm (but note §15.4 — the pattern doubles as the
  slot-derivation signature, so "drop" here means "drop the hardcoded index", which
  is now done; a bare RTTI with no signature could not tell which slot is the
  generic accessor).
- **GMRC → transport RPC (high value, high cost).** Hook the transport vtable
  and intercept by RPC name, injecting the response. Requires protobuf handling
  for `…GetManifestRequestCode_Request/Response` (CR has a whole `protobuf.cpp`).
  Trades pattern-fragility for protobuf-complexity.
- **BuildDep, ShaderDepot stay pattern-anchored.** Non-virtual local logic on
  appinfo/KeyValues, not RPC-dispatched. ShaderDepot is non-critical, so after
  migrating DepotKey+GMRC the only *critical* pattern-dependent hook left is
  **BuildDep**.

### 15.4 Caveat — not immunity

Vtable slot indices and RPC names are *much* more stable than byte patterns, not
immune. Valve reordered the `IClient*::RunIPCFrame` virtuals on 2026-06-24 and
SLSsteam had to bump (those are vtable-dispatched too). This trades "update every
build" for "update rarely" — what CR advertises.

> **Note on that example (2026-07-24).** SLSsteam has since **deleted every
> `IClient*::RunIPCFrame` hook** (`8de3384`), so the specific precedent no longer
> exists upstream — it navigates `CSteamEngine → CUser →` member offsets instead.
> The caveat itself is unaffected, and arguably reinforced: the reorder is part of
> *why* Ace moved off those hooks. See `slssteam.md` §5.3 (2026-07-24).

> **Since then:** the paragraph below describes `ResolveVtableSlotBySignature`, removed from the runtime on 2026-09-08. A vtable reorder is now handled by the by-name rescue (`Rtti::ResolveVtableSlotByName`, `src/rtti.cpp:289`), which reads the slot from the method name rather than from the prologue; see the note at the top of §15.

**DepotKey now closes even the
reorder gap**: instead of trusting a fixed index it DERIVES the slot by matching
the accessor's prologue signature inside the vtable (`ResolveVtableSlotBySignature`),
so a reorder is *handled* — the accessor is found at its new slot — and a
zero/multiple result fails closed. That is the "validate the resolved slot's
prologue" this caveat used to only recommend, made the resolution itself. (A hook
whose target self-names could instead derive the slot by that name, à la SLSsteam's
decompiler; DepotKey's accessor is a generic KeyValues call with no distinctive
string, so the prologue signature is the discriminant.)

## 16. SLSsteam's update-block (20260705) and lumalinux's runtime unblock

*(2026-07-07, v0.16.x; patch removed in v0.16.18, 2026-07-23)*

> **⚠️ SUPERSEDED / REMOVED (v0.16.18, 2026-07-23).** This section documents the
> `sls_update_unblock` in-memory patch, which targeted the **20260705** update-block
> mechanism (the inline `and …,0xFFFFF8E5` clear inside a `GetAppStateInfo` detour).
> SLSsteam **reverted** that on `20260714131044`, going back to a vtable hook on
> `IClientAppManager::GetUpdateInfo` gated by the `DisableUpdates` config option. The
> instruction this patch anchored on no longer exists, so the patch became dead code
> and was **removed** (`src/sls_update_unblock.{cpp,hpp}` deleted). The countermeasure
> now lives in LumaDeck at the config level: `ensure_slssteam_flags()` writes
> `DisableUpdates: no`, which short-circuits the new hook entirely. See
> `docs/slssteam.md` §2.5 for the current mechanism. The reverse-engineering
> below is kept as a historical record.

**Context.** SLSsteam's 2026-07-05 release (`5c632dd`) replaced its old
`GetUpdateInfo` VFT hook with a detour of `IClientAppManager::GetAppStateInfo`
(`Apps::getAppStateInfo`) that, for any app where
`shouldDisableUpdates(appId) = isAddedAppId(appId) || !isSubscribed(appId)` is
true, clears the `APPSTATE_UPDATE_*` bits in the app-state struct Steam reads.
`isAddedAppId` is true for **every** `AdditionalApps` entry, permanently — and
that's exactly where LumaDeck puts the games it manages. Net effect on the new
SLSsteam: those games stop auto-updating (Steam shows "Update required" but never
downloads on its own). Owned games are hit only transiently at startup (an
`isSubscribed` race before licences load — benign, but it's why owned games can
log a one-shot "Disabled updates"). This is deliberate on AceSLS's side; there is
no config toggle. See docs/slssteam.md §2.5 and §5.3 for the SLSsteam-side reading.

### 16.1 Why no config workaround is acceptable

- `AdditionalApps` → the block fires (that's the trigger; can't leave it).
- `PlayNotOwnedGames: yes` blanket-activates the whole non-owned library (clutter,
  and doesn't move the game out of the AdditionalApps trigger anyway).
- `UseWhitelist: yes` is a **global** switch: it flips SLSsteam from blacklist to
  whitelist for *every* app, breaking unlock/DLC for everything not whitelisted
  (the thecatantirat Discord case: Cuphead DLC vanished after enabling it).

So the fix has to be surgical and leave SLSsteam's config untouched.

### 16.2 The anchor — one instruction, ABI-stable immediate

SLSsteam ships built `-flto=auto -O3`, so `shouldDisableUpdates` inlines and the
six individual `state &= ~APPSTATE_UPDATE_*` clears fold into a **single**
combined-mask instruction:

    and dword [esi+0x8], 0xFFFFF8E5     ; 81 66 08 e5 f8 ff ff

`0xFFFFF8E5 = ~0x71A = ~(REQUIRED 0x2 | QUEUED 0x8 | OPTIONAL 0x10 | RUNNING
0x100 | PAUSED 0x200 | STARTED 0x400)`. On build `20260705132808` this sits at
file offset `0x1cd7b0`. We anchor on the **immediate** `E5 F8 FF FF`, not a
prologue: the `APPSTATE_*` values are ABI-stable (games depend on them), so the
constant survives SLSsteam recompiles even as the surrounding code shifts — the
very treadmill §8 / slssteam.md §5.3 describe for byte-patterns,
dodged here by anchoring on a value the ABI freezes.

The immediate `E5 F8 FF FF` also appears 6 other times in the binary as
jump/call displacements (preceded by `e9`/`e8`/`0f 84`). To isolate the real
clear, `AndInsnStart` requires the 4 bytes to be the imm32 operand of an
`81 /4 r/m32, imm32` (AND) instruction — checks opcode `0x81`, ModRM reg field
`== 4`, a non-SIB / non-`[disp32]` addressing form — across the three plausible
`[reg+disp]` encodings (disp8 / no-disp / disp32). On the verified build exactly
one match survives.

### 16.3 The patch and its fail-safes

`src/sls_update_unblock.cpp` (`SlsUpdateUnblock::Apply()`, called once from
`InstallHooks` after the package-0 finder):

1. Parse `/proc/self/maps` for `SLSsteam.so` `r-x` ranges — scan each mapping
   **separately** (never an aggregated min..max span) so a read can't fall into
   an unmapped hole between segments.
2. Scan for `E5 F8 FF FF`, keep only hits that form a valid `81 /4 …` AND (§16.2).
3. **Require exactly one hit.** Zero or >1 → log a warning and leave the block in
   place (auto-update degrades to manual — safe, never a crash). This is the
   guard against SLSsteam codegen shifting under us; refresh the anchor if it
   ever trips.
4. **Repoint the immediate atomically.** `mprotect(R|W|X)` the page(s) (handling
   the 4 bytes straddling a page) so the page never loses execute, store the
   immediate to `0xFFFFFFFF` with a single aligned `__atomic_store_n` (not a
   byte-wise `memcpy`), then `mprotect(R|X)` back. `AND reg, 0xFFFFFFFF` is a
   no-op, so the update flags survive untouched. This is the same atomic,
   executable-window-preserving write as §17.4's `WriteRel32`: the update-clear
   is a cold path (app-state queries, not a boot-hammered function) so the old
   `mprotect(RW)` + `memcpy` never collided in practice, but it carried the
   identical latent torn-write hazard, hardened in v0.16.8.
5. **Cache-line fail-safe.** If the 4-byte immediate would straddle a 64-byte
   cache line (where a single store stops being atomic on x86), skip the patch
   and leave the block in place rather than risk a torn write.

Env override `LUMA_NO_SLS_UNBLOCK=1` skips it entirely (the A/B control). Nothing
else in SLSsteam is touched: the game stays in AdditionalApps; ownership, DLC
surfacing, depot keys, tokens all behave exactly as before. The only removed
behaviour is the flag-clearing. This is the **first place lumalinux modifies
SLSsteam** rather than just coexisting with it (frontier note, slssteam.md §4.3).

### 16.4 End-to-end validation (2026-07-07, Balatro, clean codespace)

Stack: fresh Arch codespace, SLSsteam `20260705132808` (via Headcrab), lumalinux
built from `main` and wired through `install.sh`'s `steam.sh` LD_PRELOAD patch.
`SLS-unblock: patched SLSsteam update-clear … -> and reg,0xFFFFFFFF` appeared on
every Steam launch (exactly one anchor found each time; e.g. `insn=0xf57a77b0`).

- **Phase A (pin old):** built an "old" Balatro zip (depot 2379781 repinned to old
  gid `3742336026811834465`, old `.manifest` swapped in — the old-zip recipe of the update-testing
  procedure of the time, since retired; its dated results are in nosotros.md §5.2), deployed `steamidra_lite --pin`. Steam installed the old version: `.acf`
  `InstalledDepots 2379781 → 3742336026811834465`, `StateFlags 4`. Logs:
  `BuildDep: PATCH … 3512319404653808464 -> 3742336026811834465` and
  `LoadDepotKey: SERVED … depot 2379781`.
- **Phase B (unpin → observe):** `steamidra_lite --unpin 2379780` (gid→0, purge
  cached manifests). Restarted Steam and **touched nothing**. `content_log.txt`:

      state changed : Update Required,Fully Installed,Update Queued,Update Running,Update Started
      Downloading 2 chunks for depot 2379781 (3512319404653808464)
      finished update, 1 mounted depots : 2379781 (3512319404653808464)

  Balatro auto-updated old→current on its own — the exact behaviour the block
  kills. (The delta was tiny: `reuse 55000644, delta 18300195`, 2 chunks fetched.)
  With `LUMA_NO_SLS_UNBLOCK=1` the game instead stays at "Update required" and
  never downloads — matching the pre-patch Mina observation that opened issue #20.

Coexistence held throughout: SLSsteam and lumalinux both mapped in the same client
(disjoint hook sets, §11 / slssteam.md §2.1), no heap corruption.

## 17. Native achievements — scoping SLSsteam's borrow guard (`sls_achievement_unblock`), and the atomic-rel32 OOBE fix

*(2026-07-12, v0.16.2–v0.16.7; updated 2026-07-23 and 2026-08-13)*

**Context.** SLSsteam's 2026-07 releases added *native achievement support*: when
a game asks Steam for its achievement stats and the account does **not** own the
app, SLSsteam borrows an achievement *schema* from a real owner it finds via the
game's recent reviews. Two entry points carry it —
`Achievements::sendAndRecvGetUserStats` (legacy `CMsgClientGetUserStats`) and
`Achievements::sendAndRecvGetPlayerStats` (unified `Player.GetUserStats#1`) — and
both open with the same guard:

    if (g_pSteamEngine->getUser(0)->isSubscribed(appId)) return;   // "you own it → don't borrow"

`isSubscribed` reads *real* server ownership (through SLSsteam's own
`CheckAppOwnership` trampoline). The invariant it enforces: never rewrite a stats
request for an app the account actually holds a licence for — borrowing a
stranger's schema would hijack your own request and hand back a zeroed one.

**Why LumaDeck games get no native cheevos by default.** LumaDeck installs games
via a *native Steam download* that writes a **real local licence** (`config.vdf`
DecryptionKeys/AppTokens + the `.acf`). So `isSubscribed(appId)` returns **true**
for them — the guard fires, the borrow is skipped, and the game shows no
achievements. (Contrast a DepotDownloader-style tool that self-downloads with no
local licence → `isSubscribed=false` → the borrow runs → cheevos work. lumalinux's
whole differentiator is the native path, so it loses the borrow.) The
SLSsteam-side reading of the borrow flow is in docs/slssteam.md §2.7.

**Resolving the apparent paradox — "SLSsteam fakes ownership, so why does its own
guard see the game as owned?"** Two facts that look contradictory but aren't:

1. **SLSsteam's `isSubscribed` deliberately reads REAL ownership, bypassing
   SLSsteam's own faking.** SLSsteam *does* hook `CheckAppOwnership`
   (`hkUser_CheckAppOwnership`) to fake ownership for `AdditionalApps` — but that
   hook is the *outward* face it shows steamclient. SLSsteam's SDK
   `CUser::checkAppOwnership` calls the **trampoline** (`.tramp.fn`, the original
   un-hooked function), so `CUser::isSubscribed` answers "does the account *really*
   hold a licence?", not "does our fake say so?". SLSsteam needs that honest check
   internally (e.g. `fakeappid.cpp`: *only fake an app the user does not really
   own*). So with **stock SLSsteam alone**, an `AdditionalApps` game reads
   `isSubscribed = false` → the borrow runs → cheevos work with **no patch**. A pure
   SLSsteam user never needs `sls_achievement_unblock`.

2. **The LumaDeck stack injects the licence deep enough to fool even that honest
   check.** For native Steam download to work, the app must sit in Steam's *real*
   licence set — which is exactly what the **package-0 finder** does (seeds the AppId
   into `PackageId=0`, the anonymous licence package) on top of the on-disk
   `config.vdf`/`.acf` licence. Once the licence is genuinely in Steam's set, even the
   trampoline `CheckAppOwnership` returns true, so `isSubscribed = true` and the guard
   fires. Verified on-device: the guard trace logged `appid=1454400 sub=1 added=1`.

So it is not SLSsteam blocking `AdditionalApps` achievements — it is **our own deep
licence injection** (the price of the native-download model) making the game read as
genuinely owned, which trips a guard that assumes owned ⇒ has a legit schema.
`sls_achievement_unblock` reconciles the two by excluding `AdditionalApps` from the
skip. This is why the patch is **LumaDeck-specific**, not an SLSsteam bug.

### 17.1 The scope we want: `isSubscribed && !isAddedAppId`

A blunt NOP of the guard's `jne` would run the borrow for **genuinely owned**
games too — hijacking your real stats request with a reviewer's and clearing your
unlocked achievements. So the guard must stay for real purchases and only lift for
LumaDeck games. The distinguishing fact: LumaDeck games are the ones SLSsteam has
in `AdditionalApps`, i.e. `CConfig::isAddedAppId(appId) == true`. Target predicate:

    if (isSubscribed(appId) && !isAddedAppId(appId)) return;

- `subscribed && !added` → 1 → skip borrow (real purchase, untouched)
- `subscribed &&  added` → 0 → run borrow  (LumaDeck game → native cheevos)
- `!subscribed`          → 0 → run borrow  (unchanged from stock)

### 17.2 The patch — `sls_achievement_unblock.cpp`

`SlsAchievementUnblock::Apply()` (called once from `InstallHooks`; it used to run
alongside the since-removed `sls_update_unblock` §16) does an in-memory redirect of
SLSsteam.so:

1. **Resolve symbols** from the on-disk `SLSsteam.so`: `CUser::isSubscribed`,
   `CConfig::isAddedAppId`, the `g_config` object, and the two
   `sendAndRecvGet*Stats` functions. The four functions match by **mangled-name
   PREFIX** (the nested-name up to the closing `E`, *before* the parameter
   encoding), scanning `.symtab` then `.dynsym`; `g_config` (a plain, param-free
   global) matches exactly, and LTO `.cold`/`.lto_priv` split-offs (a `.` in the
   name) are skipped. All-or-nothing — a miss fails the resolve and `Apply()`
   no-ops (cheevos off, never a crash).

   > **Why prefix, not the full mangled name (fixed after SLSsteam `20260728`).**
   > The mangled name encodes parameter types, so a signature change breaks an
   > exact-name lookup. SLSsteam changed `sendAndRecvGetUserStats`' last
   > arg from `uint32_t` (`j`) to `EMsg` (`4EMsg`) — in `59f8259` (**2026-07-20**,
   > *"Replace handmade EMsgType enum with protoc generated EMsg"*), first shipped in
   > release **`20260722152506`**, NOT in `20260728212859` as this note and `e3dc918`
   > originally said; `20260728212859` is where it was *detected*. The broken window is
   > therefore six days and two releases wider than first recorded (see
   > `slssteam.md` §5.2 F7 (M63) for the release-by-release table). The hardcoded
   > `…sendAndRecvGetUserStatsE…S3_j` stopped matching the real `…S3_4EMsg`, and
   > since the resolve is all-or-nothing `Apply()` **silently no-op'd on every Deck
   > that had updated SLSsteam** — native achievements OFF, and (because the
   > licence-reconcile's `CUser` is captured *only* inside this guard, see §18 /
   > `license_reconcile.cpp`) the **no-restart Add path died with it**.
   > Prefix matching survives parameter-type changes; the `.dynsym` fallback
   > survives a future `.symtab` strip. Symbols were all still present (`GLOBAL
   > DEFAULT`) — only the one hardcoded string was stale. Root-caused + fixed on a
   > Deck-mirroring SteamOS codespace (LumaDeck #38 chased the same window from the
   > finder side).
2. **Find the guard call** inside each `sendAndRecvGet*Stats` by scanning for
   `E8 rel32 (call isSubscribed) · 83 C4 imm8 (add esp — cdecl caller cleanup) ·
   84 C0 (test al,al) · 75 rel8 (jne)`. Require **exactly one** hit per function
   and **cross-check** both call the same target (= `isSubscribed`). Zero /
   multiple / mismatch → no-op. (The `83 C4 imm8` between the call and the test is
   the byte that a first, wrong pattern missed — it's the cdecl caller-side stack
   cleanup, imm left wild.)
3. **Repoint** each `call isSubscribed` (E8 rel32) to a replacement free function
   `sls_ach_combined_guard` in lumalinux.so, which calls the real `isSubscribed`,
   then — only if true — `isAddedAppId(&g_config, appid)`, and returns
   `sub && !added`. The untouched `test al,al; jne` then skips the borrow exactly
   for genuinely-owned games.

The ABI works out because SLSsteam is x86-32 GCC: non-static methods are **cdecl
with `this` as an explicit first stack arg** (`push appId; push CUser*; call
isSubscribed; add esp,imm`), so a plain `extern "C" uint8_t(void* this, uint32_t
appid)` matches the call site's stack exactly. `combined_guard` calls back into
SLSsteam via absolute pointers; each callee is normal PIC and establishes its own
GOT (`get_pc_thunk`), so entering SLSsteam code through our rel32 is
indistinguishable from any indirect call. `g_config` is the global object itself
(symbol address = the `this` pointer). All verified on-device via the opt-in
trace (17.5): a real appid (e.g. `1454400`) and sane 0/1 flags, no swapped ABI.

This is lumalinux's **second** SLSsteam in-memory patch (after §16), same frontier
note and same fail-closed discipline.

### 17.3 The OOBE — a torn-write postmortem

The first shipped build that actually applied the repoint (v0.16.2 — the earlier
v0.16.1 had a wrong byte pattern and silently no-op'd) **hard-crashed Steam on the
switch to game mode, five fast exits in a row → gamescope's `short_session_recover`
wiped `~/.local/share/Steam` → OOBE.** No backtrace survived the wipe. Painful to
diagnose because the *identical patch bytes*, armed from a desktop terminal, ran
clean (guard fired for three appids, cheevos popped, owned games untouched).

The evidence that cracked it was a later coredump (`coredumpctl`), captured on an
armed desktop run before its Steam was killed:

    trap invalid opcode ip:f59da5e0 … in liblumalinux.so[…]
    #0  YAML::Scanner::PushIndentTo (liblumalinux.so + 0x3175e0)
    #1  0x00000013  (garbage return address)

A **SIGILL** (illegal instruction) whose instruction pointer was **inside
liblumalinux.so** — our own module, the one holding `combined_guard` — but at the
*wrong offset* (a yaml-cpp function, not `combined_guard`), reached with a
**garbage stack** (`0x13` return). Nothing else jumps into liblumalinux.so, and
normal control flow never lands in yaml-cpp with a `0x13` return. The only
reading: a steamclient thread executed the patched `call` **while the 4-byte rel32
was half-written**, read a torn target (some new bytes, some old), and jumped into
hyperspace.

Root cause = the original `WriteBytes`:

    mprotect(page, RW);        // ← dropped PROT_EXEC for the whole page
    memcpy(addr, rel32, 4);    // ← byte-wise, non-atomic
    mprotect(page, RX);

Two hazards in that window: (a) the page was momentarily **non-executable**, so
any concurrent instruction fetch on it faults; (b) the rel32 store was **not
atomic**, so a concurrent execution of the `call` could observe a torn operand.
**Why game mode and not desktop:** game mode drives `sendAndRecvGet*Stats`
concurrently at boot (it brings a title up and pulls its stats immediately), so a
Steam thread is very likely executing that exact page while `Apply()` writes it. A
single hand-paced desktop launch almost never overlaps the microsecond write
window — which is why v0.16.4 looked clean and why this took a coredump to pin.

(A second coredump — `cloud_redirect.so → __backtrace → ld.so` during `sigreturn`
— is a *separate*, pre-existing full-stack teardown race, the one `main.cpp`'s
`_exit(0)` atexit already documents, not this patch.)

### 17.4 The fix — `WriteRel32` (v0.16.7)

`WriteBytes` was replaced with a call-redirect-specific `WriteRel32` that removes
both hazards:

1. **Keep `PROT_EXEC` set for the write** — `mprotect(R|W|X)`, so the page is
   never non-executable and a concurrent fetch can't fault. If a hardened kernel
   refuses W^X the mprotect fails and we no-op (fail-safe).
2. **Store the rel32 as one aligned `__atomic_store_n` dword.** x86 makes a 4-byte
   store atomic as long as it does not cross a 64-byte cache line, so a thread
   executing the `call` concurrently sees **either the old target (`isSubscribed`)
   or the new one (`combined_guard`) — never a torn mix**. If the operand would
   straddle a cache line (rare, ~6%), skip that site rather than risk it.

Both targets are valid instructions, so even mid-swap the worst case is "guard
runs once with the old semantics" — no illegal jump. The guard *logic*, ABI, PIC,
symbol resolution and `g_config` were all already validated correct on-device
(v0.16.4); v0.16.7 fixes the *delivery*, not the computation.

**Lesson (see also docs/maintenance.md §D):** any in-memory patch of *live,
multithreaded* code must (a) keep the page executable across the write and (b)
make the operand store atomic (a single naturally-aligned ≤8-byte store, or park
the threads). The since-removed `sls_update_unblock` (§16) wrote a 4-byte immediate
the same way; it collided far less (a rarely-hot app-state path, not a function
steamclient calls at boot) so it never bit, but it carried the identical latent
hazard — **v0.16.8 gave
it the same atomic, executable-window-preserving write.**

### 17.5 Switches & validation

- `LUMA_NO_SLS_ACH_UNBLOCK=1` — disable the patch entirely (panic button; native
  cheevos off, everything else unchanged).
- `LUMA_SLS_ACH_TRACE=1` — per-call guard trace (`ENTER cuser=… appid=…`,
  `isSubscribed ok`, `isAddedAppId ok`, `→ skip=`), off by default.

On-device (v0.16.4 armed, then v0.16.7 default-on):

    appid=1454400  sub=1 added=1 -> skip=0   ← LumaDeck game: borrow runs → native cheevos (popped in-game)
    appid=22380    sub=1 added=0 -> skip=1   ← owned: guard holds, borrow skipped
    appid=2371090  sub=1 added=0 -> skip=1   ← owned: untouched

## 18. No-restart Add Game — the license reconcile (v0.16.15)

*(2026-07-21, v0.16.15–v0.16.16; addendum 2026-09-04)*

> **Since then:** `NotifyLicensesUpdated` is no longer resolved by moon's byte pattern alone: the chain is RVA feed → `kNotifyLicensesUpdatedPattern` → callback-125 anchor rescue (`src/license_reconcile.cpp:37-57`, `src/reconcile_anchor_core.hpp`), and CI re-derives the pattern from that anchor (`tools/derive_reconcile_byanchor.py`), not from the `17LicensesUpdated_t` RTTI string. See [`nosotros.md`](nosotros.md) §2.1 and `maintenance.md` A.2.

**Problem.** A game added while Steam is *running* clears all six gates
(ownership, package-0 depot injection, keys, GMRC) yet still won't download until
a **Steam restart**. Symptom in `content_log.txt`: `has no changes, 0 active: 0
target` → `finished update, 0 mounted depots` → the app is marked *Fully
Installed, Files Missing* and launching it fails with a missing executable. After
a restart, `AppID … config changed : added depots …` appears and the exact same
install proceeds and completes.

**Root cause.** The blocker is Steam's **appinfo** (PICS product info) cache for
the app: it carries the depot *list*, and lumalinux **cannot inject depots into
appinfo** (§6 dead-end — SLSsteam owns the message layer). Steam only re-fetches
that appinfo on a **restart / re-login / PICS change**. Until then the app's depot
list is empty, so the per-depot eligibility filter has nothing to pass and Steam
downloads nothing. This is *upstream* of everything the gates do — the keys are
served and the depots are injected into package 0, but Steam never asks for them
because it doesn't believe the app has any.

**Dead end — `DepotIdVec` (tested, ruled out).** slsteam-moon populates the
package's separate `DepotIdVec` (+0x48) and comments that "Steam's depot
eligibility filter looks at pkg.DepotIdVec"; lumalinux always put everything in
`AppIdVec` (+0x38). We shipped an experiment (v0.16.14, `LUMA_DEPOT_IDVEC`) that
*also* seeds `DepotIdVec`. Result: **no change** — still `0 target depots` without
a restart. Because the failure is upstream (empty appinfo depot list), eligibility
(whichever vector it reads) is never consulted. So `DepotIdVec` is **not** the
lever; it's only relevant *with* the reconcile, as moon's way of keeping `AppIdVec`
clean (see below). Removed in v0.16.15.

**The fix — a license reconcile.** Broadcast the `LicensesUpdated_t` callback
(ECallbackType `0x7d`) on the local `CUser` via `CUser::NotifyLicensesUpdated`.
That makes Steam re-read ownership **and appinfo** for the newly-owned apps live —
the depot list appears and the download starts, no restart. This is what
slsteam-moon (`NotifyLicensesUpdated`) and OpenSteamTool
(`MarkLicenseAsChanged` + `ProcessPendingLicenseUpdates`) both do; the reconcile,
not the vector, is the mechanism. OST needs an anti-hang hook
(`CAppInfoCache::GetOrAddAppData` → `bSkipFlag`) because it injects depot ids into
`AppIdVec` that never resolve appinfo, blocking `ProcessPendingLicenseUpdates`
forever; OST shipped that hook **disabled** ("TODO: robust way"). moon avoids the
hang by keeping `AppIdVec` app-ids-only and depots in `DepotIdVec` — that is the
*only* reason moon touches `DepotIdVec`.

**lumalinux implementation** (`src/license_reconcile.cpp`, default ON since
v0.16.16; kill-switch `LUMA_NO_RECONCILE`):
- **Resolve** `NotifyLicensesUpdated` by moon's Linux byte-pattern
  (`kNotifyLicensesUpdatedPattern`), **unique-match-or-no-op**
  (`FindNotifyLicensesUpdatedFunction`) — a wrong-build pattern disables the
  feature rather than calling garbage.
- **`CUser*`** captured from the SLSsteam achievement guard (a Steam thread with a
  valid pipe-0 `CUser`) — no new hook, no `CSteamEngine::getUser` resolution.
- **Trigger**: the re-enabled `keys.txt` watcher reloads the KeyStore and *arms*
  the reconcile (it never calls Steam itself); the **package-0 finder** fires it
  on its own thread **after** re-injecting the new depots (correct ordering).
- **Hang**: none observed. The cold-cache PICS-re-request hang doesn't apply — the
  finder injects after login (warm cache), exactly moon's prediction.
  **[EXTENDED 2026-09-04 — the immunity is WIDER than this paragraph.]** The
  targeted stress test this reasoning did not cover was run
  (`tools/experiment_cold_cache.sh`, client `1788400362`): three arms — warm
  cache + reconcile, **cold** cache + reconcile, and a second consecutive
  trigger — injecting **non-existent** depot ids, which never resolve
  appinfo. **In all three, `APPENDED` and `Reconcile: broadcast` land in the same
  second and the UI responds normally.** That is: **it does not hang cold either**,
  so the warm-cache argument is sufficient but not necessary —
  consistent with the conclusion further down (*"we over-weighted it"*, and OST ships
  its anti-hang disabled **and it works in the field**: LuaToolsLinux uses it and
  so does the Windows port `madoiscool/BetterSteamTools`, without hangs).
  Side data point: `AppIdVec` comes out `size=196 alloc=202` **identical cold and
  warm** — the licence set comes from the server, not from the `appcache`.
  **Limits**: synthetic trigger (the real-game verification of 2026-07-20,
  further down, is better evidence), a single build, and `Reconcile: broadcast`
  returning only proves that `NotifyLicensesUpdated` returned, **not** that
  `ProcessPendingLicenseUpdates` finished (it is asynchronous). Details in
  [`slssteam-plugins.md`](slssteam-plugins.md) §5.2.

**Did the hang matter for us?** No. We over-weighted it. moon's own note: the hang
is a cold-cache/early-inject problem and lumalinux (finder, post-login) "most
likely doesn't need" the anti-hang; OST even ships with its anti-hang hook
disabled. Confirmed live: the reconcile fired and Steam downloaded cleanly.

**Verified live (2026-07-20, v0.16.15).** Game added mid-session (at the time this
was behind the since-removed `no_restart` opt-in marker; the feature is now
default-ON with kill-switch `LUMA_NO_RECONCILE`):

    22:02:28  KeyStore watcher: keys.txt changed — reloaded (Size=24), reconcile armed
    22:02:33  LoadPackage[finder]: APPENDED 5 id(s) to PackageId=0 AppIdVec (+ the new depots)
    22:02:33  Patterns: NotifyLicensesUpdated found at 0x… (RVA 0x9f0f10)   ← unique match
    22:02:33  Reconcile: broadcast LicensesUpdated_t (CUser=0x…) — appinfo/ownership refresh, no restart
    # content_log, no restart in between:
    22:02:36  preallocated 6 files (272 MB)      ← Steam SEES the depots (no "0 target")
    22:02:36  Downloading … depot 2868390 / 1942282
    22:02:50  finished update, 2 mounted depots  → Fully Installed
    22:03:04  App Running                        ← launched, still before any restart

**Maintenance.** The pattern is **non-load-bearing**: a break disables no-restart
(→ restart fallback), never blocks installs, never crashes. Re-derive via the RTTI
anchor `"17LicensesUpdated_t"` — see `docs/maintenance.md` A.2.

## 19. The request-code providers die (2026-09-09) — what still works

*(2026-09-09 to 2026-09-14, lumalinux v0.20.0–v0.20.1; §19.2b written 2026-09-21, §19.4 notes 2026-10-07)*

All three GMRC providers (`manifest.opensteamtool.com`, `gmrc.wudrm.com`,
`manifest.steam.run`) stopped serving codes on 2026-09-09; the tools built on
them (KeySteam, SteaMidra, the LuaTools Windows client) broke the same day and
the 3a.lol / caigamer threads confirmed nobody had a replacement. Everything
below was measured on the SteamOS devcontainer between 2026-09-10 and
2026-09-11, one change at a time.

### 19.1 What actually broke in lumalinux: the shader depot

> **Since then:** v0.21.0 reversed both v0.20.0 rules below. GMRC is on by default again (off only with `LUMA_NO_GMRC`; `LUMA_GMRC` is no longer read, `src/main.cpp:158-173`), and ShaderDepot lets a keyed game's shader pre-cache run when the GMRC hook is installed and a provider answers, skipping otherwise (`src/hooks/shader_depot_hook.cpp`); see §20.3 and [`nosotros.md`](nosotros.md) §2.1.

Installs from a Hubcap zip never needed a code — the content manifests are
pre-seeded. What produced "Unknown error" was the **shader pre-cache**: Steam
asked for the shader depot's manifest, the GMRC hook fed it whatever the dying
providers returned (garbage), and Steam went into a 401 storm on the CDN and
stuck the whole download queue. With the hook returning nothing ("clean
fallthrough") Steam only shows the "No internet connection" popup, stalls ~30 s
and then installs the content. **v0.20.0** removes the case: `ShaderDepot`
returns 0 for every managed depot (keyed or keyless), Steam logs "skipping
because shader depot ID is invalid", and GMRC is opt-in (`LUMA_GMRC=1`) and no
longer a critical hook — its absence must not switch the package-0 finder off
(that produced 0-target-depot phantom installs during testing).

### 19.2 Which codes Valve still grants anonymously

`tools/gmrc_mint.py`: Valve grants a request code for **public** depots and
denies the rest. Public means the Steamworks redistributables (app 228980,
depots 228988–229005) and the **Workshop depot**, whose id equals the app id —
which is also the shader depot's id. So Workshop games (Brotato, RimWorld) get
their shader manifest anonymously and non-Workshop games (Lethal Company,
Silksong, Vampire Survivors) don't. Content depots are always denied. A gid
Valve has never seen (e.g. 1) is granted, which makes it useless as a probe.

### 19.2b Two doors that do not lead anywhere (2026-09-09, on an experiment branch since deleted)

Two more attempts at minting codes without a provider, both measured and
both dead, so nobody repeats them:

- **Anonymous login.** `node-steam-user` anonymous session,
  `getManifestRequestCode` for unowned paid depots (Brotato, Lonely
  Mountains): **AccessDenied**, same as a logged-in account that does not
  own the game. The free control (Dota) gets a code and a 200 from the CDN.
  Whatever the providers ran, it was not an anonymous session.
- **`GetCDNAuthToken`** (steamclient RPC, signature from Ghidra on build
  1788652215: `bool GetCDNAuthToken(this, app_id, depot_id, char* host,
  CUtlString* out)`). Hooked in-process: Valve emits it (rc=1) for an
  unowned paid depot too — it is **not** ownership-gated. Irrelevant
  though: that token is the legacy authentication for CDN hosts that still
  ask for one (almost none, `steam-user` notes it "usually is an empty
  string"); what the CDN demands for a manifest today is the **request
  code** in the URL, which this token does not replace. Phase 2 (token
  against the CDN) was never worth running.

The branch also carried a GMRC passthrough probe (let Steam ask for the
code itself and log what Valve answers: denied for content depots, see
19.2) and a Ghidra script to locate the content RPCs' signatures. All
build-pinned probes, off by default; nothing of it is in main.

### 19.3 What Steam needs on disk, and what it does with it

- A content install needs only `depotcache/<depot>_<gid>.manifest`, the depot
  key (never rotates) and the chunks (public on the CDN, retained ≥14 months).
- **`config/depotcache/` is not read.** Test: manifest only there → Steam still
  requests the code and fails; copy it into `depotcache/` → picked up on the next
  ~30 s retry with no other change. steamidra stopped writing that copy (0.20.1).
- **Steam's uninstall** deletes the game's `depotcache/` manifests and the
  `.acf`, rewrites `config.vdf`, and touches nothing of ours (`keys.txt`, the
  stplug-in lua, SLSsteam's config). A reinstall in the same session asks for
  the same gid and fails until the manifest is put back — then it installs.
- **`ManifestIds` (SLSsteam) is re-read** at Steam start, when the game is
  launched, and on every retry while an update is pending. It is **not**
  re-read while the game sits idle in "Play", and nothing pushed from outside
  (`steam -ifrunning steam://…`, `~/.steam/steam.pipe`) made it. SLSsteam itself
  hot-reloads the file instantly ("Config reloaded!"). Consequence: a pin moved
  with the manifests in place is applied at the next launch or Steam start,
  exactly like a native update.
- **DLC licences seen by the downloader come from package 0**, i.e. from
  `keys.txt` via the package-0 finder, not from SLSsteam's DLC hooks (those fool
  the game's Steamworks calls). A DLC without a key line is invisible to
  Steam's downloader — no popup, no loop. Adding the key + manifest and
  restarting Steam made it download the DLC by itself (Brotato, 55 MB,
  "Update Optional").

### 19.4 Where manifests come from now

- **Hubcap** still generates zips on demand (lua + keys + manifests) from
  owning accounts, with a daily API cap per key.
- **`P-ToyStore/SteamManifestCache_Pro`** (pjy612, via Fluent-Steam-Lua):
  branch = appid, one `<depot>_<gid>.manifest` per depot of the current build,
  pushed by a bot minutes after Valve (Valheim: 15 min); tags `<depot>_<gid>`
  keep a partial history. Files are a 10-byte header + raw deflate of a normal
  manifest (`tools/smc_fresh.py` inflates and verifies a chunk on the CDN: 22/22
  OK). No keys. Raw URL, no API, no auth.
- **`api.993499094.xyz/depotkeys.json`**: a flat `{depot: hexkey}` dump from
  the Fluent-Steam-Lua / OpenSteamTool-fork ecosystem (same host family as
  the SDM request-code front). Measured 2026-10-07 on the codespace: 240,103
  keys (221,727 when first noted), 18 MB, `last-modified` the same day, ids
  up to 5.3 M (218 above 5 M, 3,136 in 4.5-5 M); every one of the 32 depots
  in LumaDeck's cached Hubcap luas present with the right key (two stored
  upper-case). NOT day-one: STAR WARS: Galactic Racer (4078430, released
  2026-10-06) had neither its base depot 4078431 nor its DLC depot 4554460.
  So it cannot replace the Hubcap zip for a new DLC's key, but could fill
  the "zip is stale or lacks the new depots" gap some days later. Same
  verdict as KoriaPolis' base (`steamidra.md` §4.4): noted, not wired in
  until a real log shows that message. A wrong key cannot harm (Steam just
  fails to decrypt); the risks are the dump vanishing and its lag.
- LuaTools version "manifests" are bare luas (keys + `setManifestid`), no
  `.manifest` files.

LumaDeck's `manifests.py` chained depotcache → its own archive → repo branch →
repo tag → luastools → Hubcap zip (current build only, once a day per app).

> **2026-10-07:** `P-ToyStore/SteamManifestCache_Pro` is gone (GitHub API 404
> for the repo). Removed from the chain, which is now depotcache → own archive
> → luastools → Hubcap `/generate/manifest` (one manifest, any gid, the
> `single` quota of 1,500/day, one attempt per depot+gid per day) → Hubcap zip.
> The single manifest is the only online source left for an OLD build, which
> is what moving a pin needs for the installed build's manifests. This was
> A1 of `assella.md` §4.4 (formerly F1), whose trigger ("P-ToyStore 404") has fired.
> Measured 2026-10-07 10:54 (codespace, user key): `GET /generate/manifest?
> depot_id=1942280&manifest_id=2046527723717816735` → 200,
> `application/octet-stream`, 271 bytes, byte-identical to the file Steam had
> in depotcache/.

> **2026-09-14, measured:** the "Free Providers" chain that SteaMidra's live
> fork (`drappula/SFF`) added on 09-10 brings no live source. Its key DB
> (`fylsdy/ManifestHub`) is 404 and its GitHub manifest mirrors
> (`steamtoolsapp/ManifestHub`, `steamtools-games/ManifestHub3`,
> `qwe213312/k25FCdfEOoEJ42S6`) carry 0 of the 6 current gids that P-ToyStore
> had the same day. See `steamidra.md` §5.2 (M23).

### 19.5 First production update through the pinned model (2026-09-13)

Lonely Mountains: Snow Riders (2545360) on the user's Deck, LumaDeck 0.8.0 +
lumalinux 0.20.1, nothing touched by hand. From LumaDeck's log
(`~/homebrew/logs/`) and Steam's `content_log.txt`:

- 2026-09-12 18:40 — `ensure_pinned` pins the game to its installed build
  (`2545361 → 6942462877456514386`) and archives the present manifest.
- 2026-09-12 19:09 — the 30-minute pass sees build 25172008, fetches
  `2545361_9107064576136045598.manifest` **from P-ToyStore (branch)**, archives
  it, seeds `depotcache/` (file mode 0600 = written by us), moves the pin:
  `update check 2545360: pin moved to build 25172008`.
- 2026-09-13 08:46:50 — first Steam start afterwards: `state changed : Update
  Required … Update Started`, `Downloading 626 chunks for depot 2545361
  (9107064576136045598)`, commit at 08:47:07, `finished update, 1 mounted
  depots (BuildID 25172008)`. 17 seconds. `.acf` now `buildid 25172008`,
  `StateFlags 4`; `config.yaml` `ManifestIds 2545361: 9107064576136045598`.

No request code was asked for, Hubcap was not used, the game stayed on
"Play" throughout. The two `failed to update ownership ticket (Access Denied)`
lines an hour later are SLSsteam's usual noise for unowned apps.

### 19.6 The installed build's manifest, healed on device (2026-09-14)

§19.3 established that Steam needs **both** the installed manifest and the
target to compute an update (Balatro, 2026-09-13: old manifest removed →
`BYldRequestDepotManifest … 'Access Denied'`, game stuck on "Update"; put back
→ the 30 s retry finished). LumaDeck's local pass only guarded the *pinned*
manifest, so a pruned installed manifest stayed missing. Commit `0f4a14e`
(merged as `3e4a155`) adds the installed build's manifests to the 60 s heal and
makes the 30-minute pass refuse to move a pin while they cannot be resolved.

Validated on the SteamOS codespace, Balatro 2379780, depot 2379781, branch
deployed on top of 0.8.1, Decky restarted, Steam up:

- installed `3512319404653808464` (B), pin B, archive holds B and the older
  `3742336026811834465` (A) from the 09-13 experiment.
- `steamidra_lite.py --set-pin 2379780 2379781:A` → pin A, installed B — the
  mid-update state.
- `rm depotcache/2379781_B.manifest`; 61 s later the file is back and the log
  reads `restored installed-build 2379781_3512319404653808464.manifest for
  2379780 from the archive (Steam needs it to compute the update)`.
- Control from a first attempt where `--set-pin` had failed (pin still B): the
  pre-existing pinned-manifest heal restored the same file two minutes later
  with the old `restored … from the archive` line. The two heals coexist.
- Reset: `--set-pin 2379780 2379781:B`.

Not exercised: the second half of the change (the update pass declining to move
the pin when the installed manifest is in no source), which needs a real Valve
update; and the case where the archive lacks the installed manifest, which
falls through to P-ToyStore / luastools online, and past that to the Hubcap
single-manifest plan B in `assella.md` §4.4 A1 (not implemented at the time; done in LumaDeck `47f719a`).

## 20. The providers come back (2026-09-15/16) — two pools, the CDN check, GMRC on by default

*(2026-09-16, v0.21.0; §20.2 note 2026-10-05, §20.6 written 2026-09-22)*

Continuation of §19. Everything measured from the SteamOS codespace, with
`tools/gmrc_probe.py` (one request per second, every code taken to the CDN)
and the hook itself; the dated results of the runtime tests are in
[`nosotros.md`](nosotros.md) §5.2 (functions 4 and 5).

### 20.1 What 09-09 was, seen from the providers' side

A request code is a **bearer token**: Valve signs it for (depot, manifest) and
the CDN serves the manifest to whoever presents it — the CDN GET is anonymous,
and a code minted by a provider's server works from any machine (measured:
a 20770407 code fetched `steampipe.akamaized.net/depot/2545361/manifest/…`
from the codespace, 200, 160159 B). What changed on 09-09 is the check **at
issue time**: Valve mints only for an account holding a licence for the app
(BetterSteamTools' reading, `bettersteamtools.md` §2.4: the code
became depot-bound; a gid-only request fell back to a free-to-play carrier
account and 401'd everywhere else). The pre-09-09 providers lived off that
missing check and died together.

The cleanest confirmation is 20770407's own history: on 2026-07-21 it was
already handed around as `20770407.xyz/manifest/<gid>` (OpenSteamTool #164);
on 2026-09-13 huanyuejue's fork (`224931d`) switched it to
`/manifest/<depot>/<gid>` — once Valve checked licences the provider had to
know which app each request was for, to pick an account that owns it ("Auto
getAppid" on its status page). It adapted in four days; the others did not.

### 20.2 Two pools, not five providers

| | 20770407.xyz | manifestdex / wudrm / steam.run |
|---|---|---|
| who | one person, Ryzen at home, public status page (~70k users, ~170k req/day, 10 req/10 s per IP, donations to UNICEF) | one backend with three fronts; manifestdex is the free endpoint of a paid catalogue (manifestdex.com, credits + ad links, OpenSteamTool PR #200) |
| asks | depot + gid | gid only (they hold a manifest catalogue to map it) |
| unknown / bad gid | `Unauthorized` | a **number that the CDN rejects** |
| seen | healthy → 31 % failures for an hour → Cloudflare 502 for ~18 h (09-15) → back (09-16) | dead 09-09 → 09-16, then all three back the same morning |

The shared backend is inferred from the codes themselves: Valve mints a code
per request (20770407 and wudrm asked in the same second differ), yet wudrm,
steam.run and manifestdex return the **same** code for the same manifest
often enough (3 pairwise matches in the first run, dozens across a 42-depot
run) that they must serve one cache. Each front keeps its own copy for minutes
(manifestdex served one code for 5+ min). Codes live **≥ 58 min** (one checked
against the CDN every 2 min from 07:40 to 08:38, still 206), so the "15
minutes" figure was wrong.

`ManifestDeXCore.dll` (their Windows client, read with `strings`/`pefile`) is
**OpenSteamTool rebranded**: PDB path `C:\Users\berke\source\repos\OpenSteamTool`,
built 2026-09-13, same `dwmapi.dll`/`xinput1_4.dll` proxies, same net-packet
hook on `GetManifestRequestCode` (eMsg 151/147), same provider table with theirs
first. It reports no codes back and sends no identity; the credits/ads gate
only their lua catalogue. Where their backend gets its accounts is not visible
from the client.

The Chinese OST community (3a.lol) converged on exactly this pair by 09-13:
"the domestic source" (20770407) plus ManifestDeX "complement each other";
huanyuejue's fork made 20770407 the default and, when it went down on 09-15,
added manifestdex and switched the default to wudrm (`a730c12`, `b754d13`) —
which is how we learned wudrm was back.

**manifestdex, a note on dependence (2026-10-05).** In the ASSella Discord
Ace described its operator as selling manifests ("to download ETS2 one time
you have to pay manifestdex about 5$", "costs like 1 cent per manifest") while
handing out the MRC endpoint for free, and warned not to depend on it. That
matches the table above (free endpoint of a paid catalogue). The cascade keeps
it second; it is one of four and the CDN check rejects anything wrong it might
return, so if it closes or starts charging the cascade falls through to wudrm
and steam.run (the same backend) and 20770407. Nothing to change; worth knowing
which of the four has a commercial reason to disappear.

### 20.3 What the cascade does with that (v0.21.0)

- Order: 20770407 (the only one whose "no" is honest) → manifestdex (fastest
  front of pool B, no limit at 1 req/s) → wudrm → steam.run. opensteamtool out.
- **Every code is checked against Valve's CDN** with a one-byte ranged GET of
  the manifest before Steam sees it (`CdnAcceptsCode`). Measured on 09-15
  with a bogus code from a local server (`LUMA_GMRC_URL`): injected, Steam
  cancels the install with `Unspecified Error`, parks it in `Update Paused`,
  removes it from the schedule and never retries — the wudrm failure users
  saw on 09-10. Checked and refused: Steam's own `Access Denied` path, "No
  internet connection", `Update delayed for 30 secs`, retries by itself.
- One request per second, three paced attempts on 429 / 5xx / "Service
  temporarily unavailable", a provider that fails outright skipped for 60 s
  (with the first provider down every depot paid ~12 s before the next one
  answered), 120 s cache, User-Agent `lumalinux/<version>` (manifestdex
  requires its own; 20770407's users blame "OST scraping" for its outage and
  we do not want to share OST's bucket if it ever filters by client).
- `gmrc.json` next to `status.json`: `{"providers":"up"|"down","at":…}` after
  every lookup; "down" only when no provider answered at all.
- ShaderDepot is conditional again: keyed game + GMRC installed + a provider
  answering → the shader pre-cache runs; otherwise the v0.20.0 clean skip.

### 20.4 Measured coverage

- The codespace catalogue, 42 depots of 7 apps including 30+ DLC depots:
  42/42 `CODE_VALID` from all four providers.
- Old builds: Balatro's gid of 3 days before and one of **2.5 years** before
  (`1435140510430378530`) both granted by all four. A pin to any historical
  build needs only the gid, no manifest file (#43 in LumaDeck).
- Native install without any local manifest (Balatro): provider → CDN 206 →
  injected → `manifest request received 200` → 75 chunks → `finished update`;
  Steam wrote the manifest into `depotcache/` itself.
- Keyed shader depot (Lethal Company 1966720, install triggers the job;
  "verify integrity" does not): two shader manifests, both `via 20770407`,
  CDN accepted, `shadercache/1966720/` populated, no popup.

### 20.5 What this changes upstream of lumalinux

> **Since then:** implemented. LumaDeck's `backend/pins.py` follows exactly this model: native (no pin) while `gmrc.json` says `up`, every unfrozen game frozen to its installed build on `down` and released on `up`, user and LuaTools freezes kept; with no `gmrc.json` yet it counts as `up` when the GMRC hook is installed (§21.4). See [`nosotros.md`](nosotros.md) §2.5.

With a live provider Steam can fetch any manifest it needs, so the pinned
model of §19 (every managed game frozen to a build whose manifests we hold)
stops being the system and becomes the fallback. LumaDeck's `pins.py` is to
move to: no pin while `gmrc.json` says `up` (Steam installs and updates like
an owned game), freeze every unpinned game to its installed build when it
says `down` and release them when it says `up` again; explicit pins stay for
Auto-update-off, LuaTools fixes and #43. Keys still only come from Hubcap
zips; a new DLC depot still needs one. Not implemented at the time of writing.

### 20.6 Where the archives' codes come from: owners donate them (seen 2026-09-22)

`slsteam-moon` 2.9 (`f40d35b`, a port of `madoiscool/BetterSteamTools@4a97d9d`,
i.e. LuaTools for Windows) shows the supply side of §20.2. The archive
`manifest.luastools.xyz` — the same one LumaDeck reads manifests from —
publishes a wanted list (`GET /manifestwanted`, `depot:gid[:appid]` lines).
Every client with `Donate.Enabled: yes` (the default) runs a thread that,
every 30 s, takes the wanted entries whose depot sits in a package **its own
account really owns**, asks Valve for the manifest request code with its own
session (the ordinary `GetManifestRequestCode`, granted because the account
owns the game) and posts `depot:gid:code` to `POST /manifestcode/submit`;
codes its Steam obtains on its own for owned depots are captured passively
too. The archive spends the short-lived code on the CDN, stores the manifest
and serves it to anyone from then on. Limits: 25 mints per cycle, 2 s apart,
list refreshed every 5 min.

So the providers of §20.2 are not a protocol trick — §19.2b measured that
there is none — but crowdsourced ownership: the codes come from people who
bought the game. Moon itself stopped injecting codes the same week
(`c1b5e15`): it stages the archived manifest into `depotcache/` before Steam
asks, exactly the §19 pinned model, and only tells the user to wait when the
archive has not got the gid yet. LumaDeck is a reader of that archive and
not a donor; whether it should donate (the user's Steam minting codes every
30 s for manifests it is not installing) is a decision to put to the user,
not a default. Details in `slsteam-moon.md` §2.4 (donor) and §5.2 M90.

## 21. DLC of a game you own — what grants what (measured 2026-10-05)

*(2026-10-05, lumalinux and LumaDeck `main` of that day, after v0.22.1; §21.4 update 2026-10-06)*

*Codespace SteamOS (`.devcontainer/steamos/`, user `deck`), stack installed by
LumaDeck's Quick Install: SLSsteam `main@9c829a7` (release build), lumalinux
and LumaDeck from `main`. Game: **Darkest Dungeon (262060), owned by the test
account**. Its six DLC all have content depots; two are free and owned
(Musketeer 445700 → depot 445702, The Butcher's Circus 1117860 → 1117862), four
are paid and not owned (The Crimson Court 580100, The Shieldbreaker 702540,
The Color of Madness 735730, The Fire's Edge 4964110; Linux depots 580102,
702542, 735732, 4964111). Each DLC's Windows depot carries the same number as
the DLC's AppID. The game keeps each DLC in `common/DarkestDungeon/dlc/<appid>_<name>`.
Sources: `content_log.txt`, `~/.SLSsteam.log`, `~/.cache/lumalinux/lumalinux.log`,
`keys.txt`, the `.acf`, the Steam DLC list and the game's "Activate DLC" screen.*

### 21.1 Three questions, three answerers [read]

| Question | Answered by | Needs |
|---|---|---|
| Is this app yours? (library, "Your stuff", Play) | SLSsteam `Apps::checkAppOwnership` (`src/feats/apps.cpp:93-136`) | the AppID in `AdditionalApps` |
| May this depot be downloaded? | Steam's per-depot licence filter → **package 0** `AppIdVec` (nosotros.md §2.3), filled by lumalinux with every id in `keys.txt` (`key_store.cpp:246`, `load_package_hook.cpp:171-182`); then the key (`LoadDepotKey`) | the depot in `keys.txt` with its key — **not** `AdditionalApps` |
| Does the running game have the DLC? | SLSsteam `DLC::shouldUnlockDlc` (`src/feats/dlc.cpp:8-27`), wired into `CheckAppOwnership`, `IsAppDlcInstalled`, `BIsDlcEnabled`, `IsUserSubscribedAppInTicket` | nothing: *yes* for any not-owned, not-excluded DLC while a game is running |

The depot list of a DLC comes from the **base game's** appinfo (the DLC depots
are listed under 262060 with `dlcappid`), so Steam needs no appinfo of the DLC
AppID itself. `DlcData` is unrelated to all three: it feeds
`GetDLCCount`/`GetDLCDataByIndex` (the list a game enumerates), only for games
over Steam's 64-DLC limit (`res/config.yaml:40-42`).

### 21.2 The runs [measured]

| Run | State | Steam ("Your stuff") | Depot download | In game |
|---|---|---|---|---|
| Ref. | plain Steam, no stack | owned: the two free DLC | 4 depots: 262065, 262066, 445702, 1117862 | — |
| A | stack loaded, nothing added (`AdditionalApps` empty) | same | same; nothing for 702540/702542 | no content (`dlc/` has only the two free DLC) |
| B | `702540` hand-added to `AdditionalApps` (`Config reloaded!`) | **Shieldbreaker shown owned** | **none**, also after *Verify* (`finished update, 4 mounted depots`) | — |
| C | B undone; Darkest Dungeon added with LumaDeck | Shieldbreaker **not** owned | **all four paid DLC** (see below) | **all DLC available** |
| E | C, then `262060` removed from `AdditionalApps` and 580100, 702540, 735730, 4964110 added; Steam restarted | **all six DLC owned**, "Install" ticked | unchanged (8 depots), no `added`/`removed depots` | all DLC available; game launches; back to "Fully Installed" on exit |

Run C in detail. The Hubcap `.lua` lists, per DLC, `addappid(<dlc>)` without
a key plus one `addappid(<depot>, 1, "<key>")` + `setManifestid` per platform
depot. `steamidra_lite` keeps only keyed lines (`:1462`), so `keys.txt` got
`702540;262060;…`, `702541;…`, `702542;262060;4258374143576351227;40024569;4ede…`
(the AppID-numbered line is the Windows depot), and `AdditionalApps` got only
`262060` (`steamidra_lite.py:13-15`, `:1490-1497`). `add_game_dlcs` wrote
nothing (6 DLC ≤ 64, `slssteam_ops.py:564`). Then:

```
13:35:29 LoadPackage[finder]: APPENDED 27 id(s) to PackageId=0 AppIdVec … + AppIdVec 702540 / 702541 / 702542 …
13:37:28 AppID 262060 config changed : added depots 580102,702542,735732,4964111     (on game exit)
13:38:17 Downloading 169 chunks for depot 702542 (4258374143576351227)
13:38:17 LoadDepotKey: SERVED local key for depot 702542
13:38:55 AppID 262060 finished update, 8 mounted depots …
```

Steam did not replan while the game was running; it did on exit, unprompted.
`dlc/` then held `580100_crimson_court`, `702540_shieldbreaker`,
`735730_color_of_madness`, `4964110_fires_edge`. The game's DLC screen lists
more entries than Steam (Duelist, Runaway, Districts): they are sub-packs of
Steam DLC (Districts under `dlc/580100_crimson_court/features/districts/`;
Runaway and Duelist mostly in `dlc/4964110_fires_edge`), not extra DLC.

Side notes: a *Verify* during B re-downloaded 1 KB (`Validation: read 1 files
missing` in depot 262066) — the game's own `app.log`, which it rotates; not
related. With `262060` (owned) in `AdditionalApps` (C) nothing broke: the game
launched, no update loop. Keys were served for owned depots (445702, 1117862)
without a crash, against the older "never serve keys for an owned depot" note
(`steamidra_lite.py:153-159`); the newer comment at `:427-430` says the v1.0
hook makes that harmless. One run, release build (no `Unlocked` lines: those
are `LOG_ONCE`, debug only).

### 21.3 What it means

- **Flag DLC** (no depot): stock SLSsteam alone. [read; not measured — this
  game has none]
- **Depot DLC**: needs the base game added with LumaDeck (keys → package 0 →
  download). Measured: works, and takes **every** DLC depot in the `.lua`.
- Hand-adding the DLC to `AdditionalApps` (the usual community advice) only
  makes it *look* owned; it does not download the content (B).
- For an **owned** game the right shape is run E — the same as ASSella's
  "DLC Mode" (`utils/dlc_helpers.py:325-480`: base game out of
  `AdditionalApps`, DLC AppIDs in, keys only for DLC depots). Measured: DLC
  shown owned, content kept, game plays.

### 21.4 What LumaDeck does today with an owned game, and what is open

Today (C): base game in `AdditionalApps` (not needed, and SLSsteam's own
comment, `res/config.yaml:34-35`, warns it breaks downloads for
family-shared apps); DLC AppIDs not added, so Steam shows them as not owned;
and the base game's content depots got **pinned**: the add ran with
`pin=True` because `pins.pin_new_installs()` returns `gmrc_state() != "up"`
(`pins.py:316-319`) and the fresh codespace had no `gmrc.json` yet. The pin
that does this is SLSsteam's `ManifestIds` (depot → gid), written by
`steamidra_lite --pin`; the gids that also land in `keys.txt` are inert since
the BuildDep hook is off by default (v0.16.10, §3). Whether that pin freezes an
owned game when Valve ships a build is **not measured**, but nothing in the
path would let the update through: Steam plans against the pinned gids.

Why the file was missing: lumalinux writes `gmrc.json` only after Steam's
first manifest-request-code lookup (`status.cpp:87`, `RecordGmrc`). On a
device where Steam asks for a code at startup (every managed game's update
check) the window is seconds; on a fresh install with nothing to update it
lasts until the first add. LumaDeck now reads that window as "up" when
`status.json` reports the GMRC hook installed (the hook will answer the first
request like any other), so nothing is pinned and `Add Game` runs native;
"absent" only remains for an older lumalinux or a build where the GMRC
pattern failed. (`pins.gmrc_state`, LumaDeck 2026-10-06.)

Open: (1) an "owned / DLC-only" add in LumaDeck following run E — needs a way
to know the base game is owned (ASSella uses a manual toggle); (2) measure the
pin on an owned game.

**2026-10-06 update.** (1) is done: LumaDeck decides "owned" from Steam's own
package cache (`appcache/packageinfo.vdf`, `backend/steam_licenses.py`), adds
only the DLC the account lacks, and uninstalls them through Steam's own DLC
tick box. Every piece and every flow, with the day's measurements (what makes
Steam plan an installed game's depots and what does not, why depotcache must
stay, what `DisabledDLC` is), lives in [nosotros.md](nosotros.md).
(2) is still open.
