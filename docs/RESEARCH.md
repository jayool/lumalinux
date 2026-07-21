# lumalinux — Research log & internals

Detailed notes on how lumalinux works, how we found each piece, the dead-ends we
hit, and the reverse-engineering workflow — so this can be extended later without
repeating the whole journey.

Target binary during this research:
`~/.local/share/Steam/linux32/steamclient.so`, **ELF 32-bit i386**, ~48 MB,
BuildID `f92deb5ee064a2cf28977bd86a6ed43f420cfcba` (SteamOS, ~May 2026).
All RVAs/patterns in §4 and §7 are for that build — **re-derive on Steam
updates** (§8.1 covers semi-automatic re-derivation).

The package-0 injection path (§13) is the exception: it does NOT depend on
per-build RVAs at all — the finder derives `GOTbase` and the cache-global
offset at runtime from stable anchors, and has been verified on the
post-May-2026 builds `7c4ac73e` and `db0d79c2` without code changes.

---

## 1. The goal

Make Steam's **native Install button** download and run a game you don't own
(test case: Balatro, AppID 2379780), on a Steam Deck, **coexisting with
SLSsteam** (no fork, no patch of SLSsteam).

## 2. Division of labour (what each component provides)

| Concern | Provided by |
|---|---|
| Ownership spoof (app shows as owned, license checks pass) | **SLSsteam** (`CUser::CheckAppOwnership`, `GetSubscribedApps`, cached tickets) |
| Appinfo for unowned apps (so Steam sees the depot/manifest list) | **SLSsteam** ownership spoof (`CheckAppOwnership`) + lumalinux package-0 inject — these open it. `Apps::sendPICSInfoRequest` (eMsg 8903) can *also* attach an access token, but only as a **secondary** helper for apps Steam already queries; not load-bearing, and LumaDeck writes none |
| Family-share / offline bits | **SLSsteam** |
| Surface the content depots into the download plan | **lumalinux** package-0 finder (see §13). The LoadPackage hook stays installed but is diagnostic-only since v0.13.0. |
| Pin each depot to the right manifest (gid/size) | **SLSsteam** `ManifestIds` (since v0.16.10; lumalinux's BuildDep hook is **disabled** because SLSsteam 20260714 owns `BuildDepotDependency`) |
| Provide the depot AES decryption keys | **lumalinux** DepotKey |
| Provide the **manifest request code** (CDN download authorization) | **lumalinux** GMRC |
| Skip the shader pre-cache for keyless games (per game, not global) | **lumalinux** ShaderDepot (§13.10) |

SLSsteam gets you ownership + appinfo. lumalinux gets you the actual bytes.

## 3. The Steam content-install flow (and where each hook sits)

(For the high-level, cross-tool version of this — the "six gates", the two
download models, and a side-by-side with LumaCore/SteaMidra and
SteamTools/OpenSteamTool — see [`method.md`](method.md). This section is the
lumalinux-specific, function-level detail.)

Clicking Install kicks off, roughly:

1. **Ownership ticket** (eMsg 858, `CMsgClientGetAppOwnershipTicketResponse`).
   SLSsteam fakes ownership. The content log still prints
   `failed to update ownership ticket (Access Denied)` — this is **non-fatal**;
   the install proceeds.
2. **PICS appinfo** — Steam fetches the app's product info (depot list, manifest
   ids). What actually opens this for an unowned app is SLSsteam's **ownership
   spoof** (`CheckAppOwnership`) plus the **package-0 injection** — Steam then
   queries the appinfo through its normal handshake. The access token SLSsteam
   *can* attach (`Apps::sendPICSInfoRequest`) is a **secondary** helper, only for
   apps Steam is already asking about; it is **not** the load-bearing piece, and
   the lumalinux/LumaDeck flow writes **no** AppTokens. (Confirmed against
   moon/LumaCore, whose source states ownership is established *purely* by the
   package-0 `AppIdVec` injection + `CheckAppOwnership`.) The depot **list**
   itself still has to come from this appinfo: lumalinux **cannot inject depots**
   (doing so SIGSEGVs Steam — see §6); it only *patches* the GIDs of depots Steam
   has already surfaced. So an appinfo that **carries the depot list is
   required** — what is **not** required is the access **token** (the ownership
   spoof + package-0 is what makes the CM return that appinfo). Everything
   lumalinux adds (GID pin, key, pre-seeded manifest, request code) is
   **downstream** of Steam already knowing the depot list.
3. **`PackageId == 0`** — the implicit "free apps everyone owns" package, which
   Steam's per-depot license filter consults. Our depot ids must be in its
   `AppIdVec` or the content depots are dropped (→ "0 target depots" → instant
   "Fully Installed"/Play). → **package-0 finder** (since v0.13.0): walks the
   `CPackageInfoCache` BST directly, locates the `PackageInfo*` for
   `PackageId == 0`, and appends our depot ids in place. Runs on its own thread
   and polls forever, so it works whether Steam loads the package fresh or
   keeps it cached from a previous session (the LoadPackage hook can't see the
   cached case — see §13). The LoadPackage hook is still installed on
   `CPackageInfoCache::LoadPackage(PackageInfo*, sha1, cn, p4)` but it no
   longer injects — diagnostic-only via `LUMA_LOADPKG_DEBUG`.
4. **`CUserAppManager::BuildDepotDependency(...)`** — builds the depot list for
   the app (`pDepotInfo`, `pSharedDepotInfo`, each a `CUtlVector<DepotEntry>`).
   → **BuildDep hook**: PATCH the `ManifestGid`/`ManifestSize` of our depots to
   pin the right manifest. **Patch only — never inject** (see dead-end §6).
5. **`LoadDepotDecryptionKey(this, app_id, depot_id, out_buf)`** — Steam asks for
   each depot's AES key. Valve refuses for unowned. → **DepotKey hook**: serve
   the 32-byte key from `keys.txt`. (config.vdf can't hold these — see §6.)
6. **`…BYieldingGetManifestRequestCode(...)`** — Steam asks Valve for a
   per-manifest **request code** that authorizes the manifest download from the
   CDN. Valve denies for unowned (`Failed to get manifest request code,
   'Access Denied'` → surfaced to the UI as "No connection"). → **GMRC hook**:
   fetch the code from the provider cascade (opensteamtool → wudrm → steamrun, §7)
   and return it. **This is the load-bearing piece** — without it nothing
   downloads.
7. Manifest downloads from the CDN (authorized by the code), chunks download by
   SHA, are decrypted with the depot key, committed to
   `steamapps/common/<game>`. Done.

## 4. The hooks (functions, signatures, how found)

All hooks installed via libmem inline hooking (`LM_HookCode`) + a get_pc_thunk
fixup (`lmhook.cpp::FixPicThunk`) for the PIC prologue. Loaded via **LD_PRELOAD**
(see §5).

### DepotKey — `LoadDepotDecryptionKey` (inner KeyValues accessor, v1.0)
- `int32_t LoadDepotDecryptionKey(void* pObject, uint32 foo, char* KeyName, char* Key, uint32 KeySize)`
- Pattern: `patterns.hpp::kDepotKeyFnPattern`.
- This is the INNER KeyValues accessor (the function LumaCore hooks), NOT the
  outer dispatcher. `KeyName` = `"Software\Valve\Steam\Depots\<depot>\DecryptionKey"`.
- Hook: parse the depot id from `KeyName`; if in keystore and `KeySize >= 32`,
  `memcpy(Key, key, 32); return 32`; otherwise passthrough.
- Records `g_lastServedDepot` for GMRC correlation.
- WHY this function and not the outer dispatcher: see §12. Hooking the outer
  dispatcher and short-circuiting it corrupts Steam's heap on owned depots.

### BuildDep — `CUserAppManager::BuildDepotDependency`

> **STATUS (v0.16.10+): the BuildDep hook is DISABLED.** SLSsteam 20260714 hooks
> `BuildDepotDependency` itself (for its `ManifestIds`/`DepotBlacklist` features)
> and loads first (`LD_AUDIT` before our `LD_PRELOAD`), overwriting the prologue,
> so our pattern scan can no longer match. Version pinning moved to SLSsteam's
> `ManifestIds` (written by `steamidra_lite --pin-installed`). The hook is
> pin-only and inert in the default no-pin flow, so nothing is lost. Re-enable
> for testing with `LUMA_FORCE_BUILDDEP`. Everything below describes how the hook
> worked while it was active.

- 8 args, cdecl; `pDepotInfo`/`pSharedDepotInfo` are `CUtlVector<DepotEntry>`.
- `DepotEntry` = 32 bytes: `{u32 DepotId; u32 AppId; u64 ManifestGid; u64 ManifestSize; u32 DlcAppId; u8 LcsRequired; u8 bNotNewTarget; u8 SharedInstall; u8 pad}`.
- `CUtlVector<T>` = 16-byte header `{T* m_pMemory; int m_nAllocationCount; int m_nGrowSize; u32 m_Size}`.
- Pattern: `kBuildDepotDependencyPattern`.
- Hook: after original, for each depot in the vectors matching our keystore,
  patch `ManifestGid`/`ManifestSize` (keep original size if our override is 0,
  like LumaCore). Never injects.

### LoadPackage — `CPackageInfoCache::LoadPackage`
- `bool LoadPackage(PackageInfo* pInfo, uint8 sha1[20], int32 cn, void* p4)`.
- `PackageInfo` (Linux i386): `PackageId` at +0x00, `AppIdVec`
  (`CUtlVector<AppId_t>`) at **+0x38**.
- Pattern: `kLoadPackagePattern` (matches ~2 candidates; we use index 0,
  overridable with `LUMA_LOADPKG_IDX`).
- **Since v0.13.0 this hook is diagnostic-only.** It no longer injects depot
  ids — the package-0 finder owns injection now (see §13 for the why). The
  hook stays installed purely for `LUMA_LOADPKG_DEBUG`, which logs every
  `PackageId + AppIdVec` triple it sees and is what told us, in v0.10.8, that
  Steam was keeping `PackageId=0` cached and never re-calling LoadPackage.
- The actual injection — done by the finder — uses `KeyStore::GetAllDepotIds`
  (equivalent to LumaCore's `GetAllDepotIds`), with a Source-SDK-style grow
  policy via `std::realloc` (Linux Steam uses raw libc realloc for
  `CUtlMemory`; see §11.2 for the verification). Sanity check before any write:
  the existing `AppIdVec` entries are real low app ids (observed
  `{5,7,8,90,...}`) — bail if they look bogus (offset wrong on this build).
- A multi-match on the pattern only affects the diagnostic site, not
  injection: if a Steam update breaks `kLoadPackagePattern`, install still
  works because the finder doesn't depend on it (see §11.6).

### GMRC — `…BYieldingGetManifestRequestCode` (the key find)
- Ghidra name `FUN_012d3bd0`, **file vaddr 0x12c3bd0** (Ghidra image base 0x10000
  → Ghidra addr 0x12d3bd0).
- Signature (cdecl, i386):
  `int32 GetManifestRequestCode(void* this, u32 app_id, u32 depot_id, u32 manifest_lo, u32 manifest_hi, char* branch, uint64* out_code)`
  — args at `[ebp+0x08 .. +0x20]`; `out_code` at `[ebp+0x20]`.
- Decompiled core (the part that matters):
  ```c
  cVar3 = (**(code **)(*piVar6 + 0x10))(
            piVar6, "ContentServerDirectory.GetManifestRequestCode#1",
            &request, &response, 0);
  if (cVar3 == '\0') { return error; }          // server denied (Access Denied)
  else { *out_code = *(u64*)(response + 0x10);   // success: write the code
         return 1; }
  ```
- Pattern: `kGmrcFunctionPattern`
  (`E8 ?? ?? ?? ?? 05 ?? ?? ?? ?? 55 89 E5 57 56 53 81 EC 10 01 00 00 8B 7D 08 8B 4D 20`).
  Verified **unique** in this build.
- Hook: `manifest_gid = manifest_lo | (manifest_hi << 32)`. If the gid is one of
  our manifests (`KeyStore::HasManifestGid`), fetch the code from the provider
  cascade (`gmrc_store.hpp`, §7), write `*out_code`, return 1. Else fall through
  to original.
- Uses `get_pc_thunk.ax` (PIC, eax-based). `FixPicThunk` handles any register
  generically, so the trampoline works for the fallback path.

### ShaderDepot — `GetShaderCacheDepot` (per-game shader skip, v0.14)
- `uint32_t GetShaderCacheDepot(void* appinfo)` — cdecl, 1 stack arg. Reads the
  game's PICS appinfo KeyValues `appinfo.game.shadercachedepot` and returns that
  depot id (== app id by convention), or 0 if the app isn't a game / has no
  shader depot. File vaddr **0xfd9ca0** on build `7c4ac73e`.
- **Sole caller** is `CGetShaderDepotManifestJob::BYieldingRunClientJob`: when it
  gets 0 back it logs `skipping because shader depot ID is invalid` and ends the
  shader pre-cache job cleanly (no error, no install pause, no retry loop).
- Pattern: `kShaderCacheDepotPattern`
  (`57 56 53 E8 ?? ?? ?? ?? 81 C3 ?? ?? ?? ?? 8B 83 ?? ?? ?? ?? 8B 40 44 85 C0 75 0D 5B 31 C0`).
  Verified **unique** in this build. The `mov eax,[ebx+0x2b758]` disp32 (a
  GOT-relative offset that shifts per build) is **wildcarded** so a rebuild that
  only moves that global doesn't break the hook (see §13.10).
- Hook: call the original; if the returned id is one of OUR keyless games
  (`KeyStore::IsPresenceOnly` — present in `keys.txt` with no key), return 0 so
  Steam skips its shader pre-cache; otherwise pass the real id through (keyed
  games and the user's owned games keep their shaders). This is the per-game
  shader skip — see §13.10 (verified on Deck) for the full story and why it
  replaced the global `DisableShaderCache`.

## 5. Loading: LD_PRELOAD, not LD_AUDIT (important)

- lumalinux is a **32-bit** lib hooking the 32-bit `steamclient.so`.
- Setting it in **LD_AUDIT** (where SLSsteam lives) loads it into a separate
  linker namespace and **corrupts the heap** — Steam dies on startup with
  `realloc(): invalid pointer`. (And at the 64-bit launcher level it's rejected
  with `wrong ELF class: ELFCLASS32` anyway.)
- Setting it in **LD_PRELOAD** loads it in the normal namespace → works.
- lumalinux supports both entry styles (`la_objopen`/`la_preinit` + a
  `__attribute__((constructor))` fallback that polls `/proc/self/maps`), but
  **LD_PRELOAD is the supported, working path**.
- The injection point is **`~/.local/share/Steam/steam.sh`** — the
  user-local launcher wrapper that Headcrab installs and maintains. It
  defines `INJECT_SLS=LD_AUDIT=…` (and `INJECT_CR=LD_PRELOAD=…` if
  CloudRedirect is enabled) and exports them inside `GameLauncher()`
  before sourcing the vanilla Valve `client.sh`. `install.sh` inserts the
  lumalinux `LD_PRELOAD` export right before that `source` line, with the
  `${LD_PRELOAD:+:}${LD_PRELOAD:-}` preserve pattern so CR's `LD_PRELOAD`
  survives. **`/usr/bin/steam` is not touched** — Headcrab doesn't edit
  it, and our installer doesn't either; the system file stays vanilla and
  survives `pacman -Syu steam`.

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
pre-seeded). opensteamtool/wudrm/steamrun are **code sources, not file mirrors** —
don't confuse "the manifest" (file) with "the manifest request code" (token).

Since **v0.15.8** lumalinux tries a **3-provider cascade** (mirroring
OpenSteamTool's own `kProviders` table in `ManifestClient.cpp`); first usable
code wins:

| Order | Provider | URL | Body format |
|---|---|---|---|
| 1 | opensteamtool | `https://manifest.opensteamtool.com/{gid}` | plain uint64 |
| 2 | wudrm | `http://gmrc.wudrm.com/manifest/{gid}` | plain uint64 |
| 3 | steamrun | `https://manifest.steam.run/api/manifest/{gid}` | JSON `{"content":"<uint64>"}` |

- Transport is now **libcurl** (`Curl::getString`, `dlopen`'d at runtime —
  `src/curl.cpp`, compiled **unconditionally** since v0.15.8, not only in
  SafeMode) because opensteamtool/steam.run are **HTTPS**. The pre-v0.15.8
  raw-socket plain-HTTP path (wudrm only) is gone.
- **Cloudflare / User-Agent gate — verified on-device 2026-07, and non-obvious:**
  `manifest.opensteamtool.com` sits behind Cloudflare whose WAF **challenges the
  default `curl`/libcurl User-Agent** (serves the "Just a moment…" JS
  interstitial) but **lets any other UA straight through** to the plain code.
  `curl -A "OpenSteamTool/1.0" …/{gid}` returns the code where a bare `curl` gets
  the challenge. It is **not** TLS/JA3 or IP-based — a residential Steam Deck
  gets the same challenge with bare curl, and OpenSteamTool's own WinHTTP client
  has zero Cloudflare-solving code; it simply opens with UA `OpenSteamTool/1.0`.
  So `gmrc_store.hpp` sends `User-Agent: OpenSteamTool/1.0`. **⚠ Do not strip
  that UA** or opensteamtool regresses to always-challenged (a non-numeric HTML
  body → parser rejects → cascade falls through).
- Endpoint status observed 2026-07 from the Deck: **wudrm 502** (flaky/down),
  **steam.run 404** (dead path — the old LumaCore endpoint), **opensteamtool
  works** (with a non-curl UA). The cascade means one provider being down no
  longer blocks installs — that single-source fragility (wudrm 502 + wudrm being
  the *only* provider) was the root cause of the recurring Silksong "No
  connection" popup on depot 1030300 (the base/shader depot, whose manifest is
  not in the Hubcap zip, so its code is fetched live).
- `Curl::getString` returns 0 on transport success **without** checking the HTTP
  status, so a Cloudflare challenge page / 502 / 404 all arrive as a non-numeric
  body; each provider's parser (`ParsePlainUint` / `ParseSteamRunJson`) rejects
  those and the loop tries the next provider.
- **The code is session/time-dependent**: the same gid returned different codes
  on different days (e.g. gid 3512319404653808464 → 15549905601718457808, then
  3261884576850880630). So fetch fresh; don't hardcode/persist across sessions.
- Confirmed working values (Balatro, for reference only):
  - depot 2379781, gid 3512319404653808464
  - depot 2379782, gid 1898957422191678575
  - depot 228989 (Steamworks redist, genuinely owned), gid 3514306556860204959

## 8. Reverse-engineering workflow (how to find these on a new build)

What worked, after a lot of what didn't:

1. **Get the binary off the Deck.** `~/.local/share/Steam/linux32/steamclient.so`
   (~48 MB). It has only ~826 exported symbols; internal C++ methods are **not**
   symbolized, and string references are PIC (per-function get_pc_thunk base), so
   `objdump`/`grep` and even `radare2 5.5` (`aar`/`aaa`/`aae`) **fail or only
   partially resolve** string xrefs.
2. **Transfer for analysis.** We pushed it to a throwaway branch of a GitHub repo
   (`git push …:refs/heads/scbin`) and fetched it on a machine with proper tools.
   (Delete that branch after — it's a Valve binary.)
3. **Use Ghidra** (it resolves i386 PIC string xrefs that r2 5.5 misses). Headless:
   ```sh
   analyzeHeadless <proj> <name> -import steamclient.so \
     -scriptPath . -postScript find_gmrc.py
   ```
   where the postScript finds the anchor strings and decompiles the referencing
   functions:
   - `"CDepotDownloadMgr::BYldRequestDepotManifest("` (the GMRC consumer)
   - `"ContentServerDirectory.GetManifestRequestCode#1"` (the job name → the
     getter function)
   - `"bClientTryRequestManifestWithoutCode"` (the convar)
   Analysis of 48 MB takes ~5–15 min; bump Ghidra `MAXMEM` to ~8G.
4. **Extract a unique byte pattern** from the function prologue (with `??`
   wildcards over the PIC `add`/get_pc_thunk rel32 and the GOT offset) and verify
   it matches once in the binary. That becomes the runtime finder
   (`patterns.cpp::FindInSteamclient`, which scans the `r-x` `steamclient.so`
   mappings in `/proc/self/maps`).
5. **Note:** patterns are build-specific. On a Steam update, re-run the Ghidra
   pass and re-derive the patterns.

### 8.1 Semi-automatic re-derivation — `tools/derive_patterns.py`

To turn step 4 from a full RE session into a single command, `derive_patterns.py`
is a Ghidra **headless postScript** that re-locates each hook and prints a fresh,
correctly-wildcarded pattern. Run it against the *new* `steamclient.so`:

```sh
# one-time import (analysis ~5–15 min):
analyzeHeadless <proj> <name> -import steamclient.so
# then, any time, re-derive:
analyzeHeadless <proj> <name> -process steamclient.so \
    -noanalysis -scriptPath tools -postScript derive_patterns.py
```

It works two ways per hook, because **not every function has a usable anchor**:

| Piece | Anchor | How the tool handles it |
|---|---|---|
| **BuildDep** | string `"BuildDepotDependency"` | string → referencing function → fresh prologue pattern (PIC `call` rel32 + GOT `add reg,imm32` auto-wildcarded). **Fully auto.** |
| **GMRC** | string `"ContentServerDirectory.GetManifestRequestCode#1"` | same. **Fully auto**, e.g. `E8 ?? ?? ?? ?? 05 ?? ?? ?? ?? 55 89 E5 …`. |
| **ShaderDepot** | string `"shadercachedepot"` (referenced directly inside `GetShaderCacheDepot`) | string → referencing function → fresh prologue pattern. **Fully auto** (since v0.14.1). `extract_pattern` additionally wildcards the `mov eax,[picbase+0x2b758]` global-load disp32 (it shifts per build, §13.10). A miss is non-critical — only the per-game shader skip is lost, installs are fine. |
| **DepotKey** | indirect: dispatcher refs `"Software\Valve\Steam\Depots\"`, then virtual call (§12.5) | tries to follow the dispatcher's `CALL [reg+0x18]` to reach the inner accessor and emit a fresh pattern. If that vcall walk fails (Ghidra didn't resolve it on this build), falls back to validating the current pattern. |
| **LoadPackage** | *none* — and **diagnostic-only since v0.13.1** | validates current pattern. A miss does NOT break installs (the package-0 finder injects); only `LUMA_LOADPKG_DEBUG=1` is affected. The script output flags this explicitly so a maintainer doesn't waste time on it. |
| **package-0 finder** anchors (§13.5) | none — **runtime-derived**, not in `patterns.hpp` | verifies the two anchors are still present: (a) the cache-access idiom `8D ?? disp32 8B ?? 8B ?? 58 0C 00 00`, anchored on the `0xc58` tree-root offset; prints `disp32` (= the X used at runtime). (b) The GMRC prologue tail `55 89 E5 57 56 53 81 EC 10 01 00 00 8B 7D 08 8B 4D 20`. If either says NOT FOUND → the fix is in `src/hooks/package_zero_finder.cpp`, not in `patterns.hpp`. |

What you get end-to-end: the **string-anchored** hooks (BuildDep, GMRC) and
DepotKey-via-vcall are auto-derived. LoadPackage is treated as best-effort
diagnostic. The **finder anchors** are auto-validated against any new binary so
a Steam update that moves the runtime-derivation contract surfaces immediately
(without it, you'd only notice when installs silently fail). For the rare cases
the tool can't resolve (e.g. DepotKey if the vcall walk fails on a future
build), the output is explicit about WHICH piece needs manual work and where
in the source tree the fix lives.

**The wildcarding gotcha** (worth knowing if you extend the tool): the
get_pc_thunk-relative `add` has two encodings — `0x05` (`add eax,imm32`, 5 bytes)
and `0x81 /0` (`add r/m32,imm32`, 6 bytes). Both must have their imm32 masked;
GMRC uses the short `0x05` form, BuildDep/LoadPackage the `0x81` form.

**Jython quirk noticed during the v0.13.1 verification pass**: `Memory.findBytes`
with full-byte wildcards in short patterns returned zero hits even when the
bytes existed. The finder-anchor check sidesteps this by searching for the
unambiguous trailing literal first and verifying the surrounding bytes via
`mem.getByte()`. Keep that in mind if you ever add another anchor verification.

**Which immediates to wildcard, and which are load-bearing** (robustness audit,
2026-06-24, after the ShaderDepot hardening — RESEARCH §13.10). `extract_pattern`
wildcards exactly the bytes that move between Steam builds *for reasons unrelated
to the function's identity*:

- the PIC `call` rel32 and the GOT `add reg,imm32` — **always** wildcard;
- any `mov/lea r32,[picbase+disp32]` global-load disp32 — **wildcard** (these
  GOT-relative offsets shift on almost every build, the package-0 cache global
  moved `0x3a1bc→0x3967c`, §13.5). This was the ShaderDepot fragility; auto since
  v0.14.1. Audited: BuildDep/GMRC/DepotKey load **no** GOT global in their
  captured prologue, so the rule is a no-op for them (verified — they re-derive
  byte-identical and still UNIQUE).

But do **NOT** wildcard the **stack-frame size** (`sub esp, imm32`) even though it
*looks* build-specific. Verified empirically on `7c4ac73e`: wildcarding it leaves
GMRC unique but makes **BuildDep collide (6 matches) and LoadPackage (6)** — the
frame size is genuinely part of those functions' byte-identity, and the string
anchor finds the *function* but the runtime finder still needs a *unique* pattern.
Frame sizes have survived at least one build transition (`f92deb5e→7c4ac73e`)
intact, so they're an acceptable, non-removable anchor — not a bug to "fix".

**Robustness ranking of the derivations** (most → least): the string-anchored
trio (BuildDep, GMRC, ShaderDepot) is the most robust — a stable Steam string →
the function → an auto-wildcarded prologue. **DepotKey *was* the least robust as
a byte-sig**: no in-function anchor, so the tool walked the dispatcher's `CALL
[reg+0x18]` (hardcoded vtable slot `0x18`, relying on Ghidra resolving the
vtable), which could fail. **Since 2026-07-06 DepotKey no longer depends on that
at runtime**: the shipped hook resolves via RTTI (`CConfigStore` slot 6, §15)
with the byte pattern kept only as a validated fallback — so a Ghidra vcall-walk
miss no longer breaks the hook; the derivation below just refreshes that
fallback. LoadPackage is diagnostic-only (multi-match by design). All current
shipped patterns were re-confirmed UNIQUE (DepotKey/BuildDep/GMRC) or
expected-multi (LoadPackage) on `7c4ac73e`.

Paste the printed `UNIQUE` patterns into `src/patterns.hpp`, rebuild, redeploy.
(`tools/ghidra_find_gmrc.py` is the original GMRC-only finder kept for reference.)

## 9. Per-game data (handled by tools/steamidra_lite.py)

For a game you have a `.lua` + `.manifest` set for (e.g. ManifestHub zip):
- Copies `.manifest` files into `Steam/depotcache/` **and** `Steam/config/depotcache/`
  (Steam reads either; syncing both avoids intermittent "missing manifest").
- Writes `~/.config/lumalinux/keys.txt` in the extended format:
  `depot_id;parent_app_id;manifest_gid;manifest_size;hexkey`. `parent_app_id` is
  the app for all content depots (so `GetDepotsForApp` covers them); manifest gid
  and size come from the `.lua` (`setManifestid`) and the `.manifest` binary
  (`cb_disk_original`, protobuf metadata magic `0x1F4812BE`, field 5).
- Adds the app + depot ids to SLSsteam's `config.yaml` `AdditionalApps`.
- **Ecosystem interop** (best-effort, beyond SteaMidra's flow): copies the
  `.lua` to `config/stplug-in/<appid>.lua`, and writes ACCELA-compatible
  markers — an in-game `.DepotDownloader/` dir in
  `steamapps/common/<installdir>/` plus a `~/.local/share/ACCELA/depots/<appid>.depot`
  tracker. ACCELA's scanner (`game_manager.py:_get_accela_marker_path`) only
  checks the marker folder *exists* next to real content — it doesn't parse
  anything. Because the game isn't downloaded at deploy time, the marker is
  finalised post-install by the **`--accela-mark <appid>`** mode, which reads
  the real `installdir` from the `.acf` and re-creates the marker + tracker in
  place (LumaDeck triggers this on library refresh).

`tools/vdf_inject_keys.py` writes keys into `config.vdf` without the `vdf` python
module — but those get pruned (§6), so it's only a belt-and-suspenders helper.

## 10. Open items / ideas for extension

- **Only depot 2379781 mounted** for Balatro (64 MB) — the game runs. Depot
  2379782 (81 MB) wasn't downloaded; likely the other-OS depot Steam doesn't need
  on the Deck (Proton). If a game needs multiple depots and some don't mount,
  investigate why Steam isn't requesting their manifest (OS filter / branch).
- **Headcrab Updater regenerates `~/.local/share/Steam/steam.sh`** on
  every run and the lumalinux block goes with it. Workaround today is
  manually re-running `install.sh` after Headcrab updates. Better fixes
  would be (a) upstream Headcrab learning to read a sidecar
  `~/.config/lumalinux/inject.env` and emit the export itself, or (b)
  LumaDeck wrapping Headcrab + lumalinux installs together so the user
  hits one button.
- `RelocateChainedJmp` (lmhook.cpp) is dormant infra for hooking a function
  SLSsteam already detoured; no current hook needs it.
- Consider prefetching all keystore gids' codes at startup (background) instead
  of lazily in the hook, if the first-manifest stall is noticeable.
- **Async finder ↔ Steam read race on `CPackageInfoCache`** (theoretical, no
  observed crash). The package-0 finder (§13) reads `CPackageInfoCache` and the
  `PackageInfo*` it returns from its own thread, while Steam can mutate them
  from its threads. Already protected against the easy modes: every dereference
  goes through a `/proc/self/maps` readability check (so a freed-and-unmapped
  address yields `nullptr` + a log line, not SIGSEGV), and `InjectDepots`
  sanity-checks `AppIdVec` (rejects clearly bogus entries like `0` or
  `> 50M`). NOT protected against a "freed-and-still-mapped" use-after-free:
  Steam destroys `PackageId=0` (re-login / licence refresh) → finder grabs
  the stale `PackageInfo*` in the gap → realloc on dangling `m_pMemory` =
  heap corruption. In practice the tree is built once at login and stays
  stable; rebuilds are rare and the window is microseconds. Mitigation
  options if it ever surfaces, cheapest first:
  (a) drop the post-hit re-inject loop entirely — only the first-hit window
      exists, no slow watch at all;
  (b) double-read — read the `PackageInfo*` twice with ~10 ms between, abort
      and retry if it changed; tightens the window without true atomicity;
  (c) atomic CAS on `m_pMemory` via `__atomic_compare_exchange_n` — true
      guarantee, but bets that Steam keeps using libc realloc for
      `CUtlMemory` (§11.2). Leave alone until there's a real repro;
  speculative concurrency fixes tend to introduce more bugs than they fix.

## 11. lumalinux vs LumaCore — verified divergences

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

> **Correction (verified 2026-06 against the SFF LumaCore source).** The
> "LumaCore" column below describes an *older or assumed* LumaCore. The current
> LumaCore.dll in `Midrags/SFF` has **no `GetManifestRequestCode` hook at all** —
> `ManifestBind.cpp` only implements `BuildDepotDependency`, and the only GMRC
> reference anywhere is the protobuf message in `proto/steam_messages.proto`. On
> Windows the request code is fetched by the **SteaMidra Python layer**
> (`sff/http_utils.py:get_gmrc`) to *pre-download manifests into depotcache*, not
> supplied to the Steam client at runtime. So the runtime GMRC hook is **unique
> to lumalinux's native-client-download path**; LumaCore doesn't need it because
> SteaMidra pre-seeds the manifests. See [`method.md`](method.md) §5.

| | LumaCore (older/assumed) | lumalinux |
|---|---|---|
| Hooked function | `GetManifestRequestCode` | Same |
| HTTP endpoint | `https://manifest.steam.run/api/manifest/{gid}` | **3-provider cascade** (v0.15.8): opensteamtool → wudrm → steamrun |
| Endpoint status | **DEAD** (no longer responds) | opensteamtool **live** (needs non-curl UA), wudrm flaky (502s), steam.run dead (404) |
| HTTPS / HTTP | HTTPS via WinHttp | **libcurl** (`dlopen`), HTTPS+HTTP, UA `OpenSteamTool/1.0` (§7) |
| Gating | Whenever it knows the manifest | `KeyStore::HasManifestGid(gid) \|\| KeyStore::HasDepot(depot_id)` — the OR covers the shader pre-cache case where Steam asks for a manifest that wasn't in the original `.lua` |
| Return on success | (LumaCore has an async layer that fills the cache; it writes back via PacketRouter) | Synchronous — `*out_code = code; return 1` |

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

The historical divergences with LumaCore that motivated this section have all
been resolved by v0.8.1 (see the subsections above). The current order of
suspicion, no longer using LumaCore parallels as anchors, is:

1. **Stale Patterns after a Steam update** — a single byte change in any
   hooked function's prologue silently uninstalls that hook. This matters
   most for **DepotKey and GMRC**: those are pattern-anchored inline hooks,
   and if they break, install breaks. (**BuildDep** was in this set until
   v0.16.10; it is now disabled — SLSsteam owns `BuildDepotDependency` — so a
   BuildDep pattern miss no longer matters and `check_patterns.py` treats it as
   diagnostic.) **LoadPackage is much less critical now**: since v0.13.0 it's
   diagnostic-only, and depot
   injection runs through the package-0 finder, which derives its addresses
   at runtime and doesn't depend on `kLoadPackagePattern` (so even a broken
   LoadPackage pattern still leaves installs working — only the
   `LUMA_LOADPKG_DEBUG` diagnostic stops firing). **ShaderDepot (§13.10) is
   also non-critical**: a `kShaderCacheDepotPattern` miss only stops the
   per-game shader skip — installs still work, and keyless games merely
   regress to the §13.8 shader loop (the global `DisableShaderCache` remains a
   manual fallback). It does show FAILED in the startup toast, so a miss is
   visible.
2. **All GMRC providers down at once** — since v0.15.8 there are three
   (opensteamtool → wudrm → steamrun, §7), so a single one being down (e.g.
   wudrm's frequent 502s) is survivable. Still no *offline* fallback: if all
   three are unreachable, a manifest FILE not already in `depotcache/` can't be
   fetched. Symptom: new installs failing in a loop with "Access Denied" → "No
   connection" in the UI. Only affects depots whose manifest isn't pre-seeded
   from the zip (typically the base/shader depot). Note the Cloudflare UA gate
   (§7): if opensteamtool alone starts failing, first suspect a stripped/changed
   `User-Agent` in `gmrc_store.hpp`, not the endpoint being down.
3. **Race in `Log::Init`** — only realistic if the LD_AUDIT preinit, the
   LD_PRELOAD ctor and a worker thread coincide within microseconds
   (Issue #5).
4. **The Headcrab Updater regenerates** `~/.local/share/Steam/steam.sh` and
   the lumalinux block goes with it — there is no automatic re-apply today,
   so `install.sh` has to be re-run.


## 12. The DepotKey hook crash and its fix (Formula Legends)

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

The §11.3 divergence ("DepotKey hook without `KeySize` validation") is
resolved: the v1.0 hook DOES validate `KeySize` (like LumaCore) because it
now hooks the KeyName-based variant that receives the size. The crash was
not about the buffer (32 bytes fit in 128) but about short-circuiting the
dispatcher — fixed by hooking the inner accessor instead.

## 13. The package-0 finder — from a passive hook to an active walker

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
across builds (`X` was `0x3a1bc` on `7c4ac73e` and `0x3967c` on `db0d79c2`).
To avoid per-build constants, the finder derives both at runtime:

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
   for `lea r1, [GOT+X] ; mov r2, [r1] ; mov r3, [r2+0xc58]`. The trailing
   `0xc58` (the stable tree-root offset) is the anchor that confirms the
   match; `X` comes out of the `lea`'s `disp32` (`FindCacheGlobalDisp`).
   `cache_global = GOT + X`.

Same philosophy as `derive_patterns.py` (§8) but **at runtime**: zero
per-build offsets, everything reconstructed from stable anchors (the
hook-surviving GMRC prologue tail and the `0xc58` class-layout offset).

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
replaced it with a per-game skip** (the ShaderDepot hook, §13.10) and
steamidra_lite now *reverts* a previously-written `DisableShaderCache` instead of
writing one. Note the framing in this subsection — "making the shader pre-cache
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

Validated, on the canonical stack (native Arch Steam + SLSsteam via
enter-the-wired/Headcrab + lumalinux v0.13.5 **release** loaded by the
`install.sh` `steam.sh` patch — not env-var injection), that a game installed
**pinned to an old manifest** auto-updates to Valve's current version once the
pin is removed. Build `7c4ac73e`. This is the concrete proof behind
[`method.md`](method.md) §6's "Auto-update by unpinning".

### 14.1 The recipe that worked

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
  method.md §6 "GMRC stays load-bearing for updates" path, exercised for real.
- **Steam native client** — downloads the delta, decrypts, commits:
  `finished update, 2 mounted depots : 1794681 (7054…), 1794685 (7852…)`.

### 14.3 Notes worth keeping

- **Per-depot detection, not buildid.** Both games kept the *current* `buildid`
  in the `.acf` (Steam stamps the live buildid at install even under a pinned old
  manifest); the update still fired because Steam compares the **per-depot
  manifest GID** against PICS. See method.md §6.
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

**Context.** CloudRedirect's June-2026 release stopped needing an update per
Steam client build ("CR no longer requires an update for every Steam client
update"). Worth copying where it fits, because lumalinux's byte-pattern hooks
move on every Steam build (§8) and the SafeMode hash gate needs a per-build
whitelist entry (v0.15).

**Status (2026-07-06): DepotKey has adopted this.** It now resolves via
`CConfigStore` vtable slot 6 at runtime (`src/rtti.cpp`), with the byte pattern
kept as a validated fallback. Confirmed on build 1782861641 both statically (CI
ground-truth) and at runtime on a live Steam (codespace, `method=rtti(agrees-with-pattern)`)
— the codespace's 1782866176 was downgraded by headcrab to its pin 1782861641
before the test, so this is one build in two environments, NOT two builds; a
second-build confirmation is still pending. See §15.3. GMRC and BuildDep remain
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
| **DepotKey** | `0x1188300` | **YES — `CConfigStore` vtable slot 6** | RTTI → vtable → slot 6 |
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

- **DepotKey → RTTI vtable — ✅ IMPLEMENTED (2026-07-06).** `src/rtti.cpp`
  (mirrors CR's `FindVtableByRTTIName`, resolve-only) resolves `CConfigStore`
  slot 6; `depot_key_hook.cpp` installs the existing detour (§4) there, with the
  byte pattern kept as a fallback that never regresses (RTTI==pattern → use RTTI;
  disagreement → use pattern + loud log). Validated on build 1782861641:
  statically on CI (slot 6 == `kDepotKeyFnPattern` == RVA `0x1188300`) and at
  runtime on a live Steam in the codespace (`method=rtti(agrees-with-pattern)`).
  NOTE: the codespace Steam bootstrapped at 1782866176 but headcrab downgraded it
  to its pin 1782861641 before the test — so this is the *same* build in two
  environments (static file check + live runtime), not two builds. The live run
  did exercise the runtime-only path the static check can't: the `.data.rel.ro`
  relocation wait (from CR, 30 s cap) fired for 5.65 s. Still to do: a
  second-build confirmation; the standalone slot-prologue sanity check (only
  needed once the pattern is dropped); and dropping the pattern after more
  `agrees-with-pattern` confirmations. Tracked in the DepotKey-RTTI issue.
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
build" for "update rarely" — what CR advertises. A migrated hook should still
validate at runtime (e.g. check the resolved slot's prologue) and fail closed if
the layout shifted.

## 16. SLSsteam's update-block (20260705) and lumalinux's runtime unblock

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
no config toggle. See docs/slssteam-analysis.md §7 for the SLSsteam-side reading.

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
very treadmill §8 / slssteam-analysis §7 point 1 describe for byte-patterns,
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
SLSsteam** rather than just coexisting with it (frontier note, slssteam-analysis
§5 / §7.1).

### 16.4 End-to-end validation (2026-07-07, Balatro, clean codespace)

Stack: fresh Arch codespace, SLSsteam `20260705132808` (via Headcrab), lumalinux
built from `main` and wired through `install.sh`'s `steam.sh` LD_PRELOAD patch.
`SLS-unblock: patched SLSsteam update-clear … -> and reg,0xFFFFFFFF` appeared on
every Steam launch (exactly one anchor found each time; e.g. `insn=0xf57a77b0`).

- **Phase A (pin old):** built an "old" Balatro zip (depot 2379781 repinned to old
  gid `3742336026811834465`, old `.manifest` swapped in — docs/update-testing.md
  recipe), deployed `steamidra_lite --pin`. Steam installed the old version: `.acf`
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
(disjoint hook sets, §11 / slssteam-analysis §5), no heap corruption.

## 17. Native achievements — scoping SLSsteam's borrow guard (`sls_achievement_unblock`), and the atomic-rel32 OOBE fix

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
SLSsteam-side reading of the borrow flow is in docs/slssteam-analysis.md §7.3.

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

`SlsAchievementUnblock::Apply()` (called once from `InstallHooks`, alongside
`sls_update_unblock` §16) does an in-memory redirect of SLSsteam.so:

1. **Resolve symbols** from the on-disk `SLSsteam.so` `.symtab` (SLSsteam ships
   un-stripped): `CUser::isSubscribed`, `CConfig::isAddedAppId`, the `g_config`
   object, and the two `sendAndRecvGet*Stats` functions. All-or-nothing — a
   rename/strip fails the resolve and `Apply()` no-ops (cheevos off, never a crash).
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
the threads). `sls_update_unblock` (§16) writes a 4-byte immediate the same way; it
collides far less (a rarely-hot app-state path, not a function steamclient calls at
boot) so it never bit, but it carried the identical latent hazard — **v0.16.8 gave
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

**Did the hang matter for us?** No. We over-weighted it. moon's own note: the hang
is a cold-cache/early-inject problem and lumalinux (finder, post-login) "most
likely doesn't need" the anti-hang; OST even ships with its anti-hang hook
disabled. Confirmed live: the reconcile fired and Steam downloaded cleanly.

**Verified live (2026-07-20, v0.16.15).** Game added mid-session, `no_restart`
marker set:

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
