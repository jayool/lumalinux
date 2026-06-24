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
| Pin each depot to the right manifest (gid/size) | **lumalinux** BuildDep |
| Provide the depot AES decryption keys | **lumalinux** DepotKey |
| Provide the **manifest request code** (CDN download authorization) | **lumalinux** GMRC |

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
   fetch the code from `gmrc.wudrm.com` and return it. **This is the
   load-bearing piece** — without it nothing downloads.
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
  our manifests (`KeyStore::HasManifestGid`), fetch the code from gmrc.wudrm.com
  (`gmrc_store.hpp`), write `*out_code`, return 1. Else fall through to original.
- Uses `get_pc_thunk.ax` (PIC, eax-based). `FixPicThunk` handles any register
  generically, so the trampoline works for the fallback path.

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

## 7. The GMRC endpoint

- LumaCore (Windows) fetches the code from `manifest.steam.run` — **dead**.
- The live endpoint (the one SteaMidra uses) is
  **`http://gmrc.wudrm.com/manifest/{manifest_gid}`** → returns the request code
  as a plain-text decimal number, HTTP 200, with header `Referer: http://gmrc.wudrm.com`.
- It's plain **HTTP** (port 80) → no TLS needed; lumalinux fetches it with a raw
  socket (`gmrc_store.hpp`).
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
| HTTP endpoint | `https://manifest.steam.run/api/manifest/{gid}` | `http://gmrc.wudrm.com/manifest/{gid}` |
| Endpoint status | **DEAD** (no longer responds) | **Live** (same one SteaMidra uses) |
| HTTPS / HTTP | HTTPS via WinHttp | Plain HTTP via raw socket |
| Gating | Whenever it knows the manifest | `KeyStore::HasManifestGid(gid) \|\| KeyStore::HasDepot(depot_id)` — the OR covers the shader pre-cache case where Steam asks for a manifest that wasn't in the original `.lua` |
| Return on success | (LumaCore has an async layer that fills the cache; it writes back via PacketRouter) | Synchronous — `*out_code = code; return 1` |

lumalinux is functionally better here (live endpoint, covers shader
pre-cache) but worse on resilience (a single plain-HTTP host, no persistent
cache, no fallback endpoint — see Issue #2).

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
   most for **DepotKey, BuildDep and GMRC**: those are pattern-anchored
   inline hooks, and if they break, install breaks. **LoadPackage is much
   less critical now**: since v0.13.0 it's diagnostic-only, and depot
   injection runs through the package-0 finder, which derives its addresses
   at runtime and doesn't depend on `kLoadPackagePattern` (so even a broken
   LoadPackage pattern still leaves installs working — only the
   `LUMA_LOADPKG_DEBUG` diagnostic stops firing).
2. **`gmrc.wudrm.com` down / DNS-blocked** — no offline fallback, no
   alternative endpoint (Issue #2). Symptom: new installs failing in a loop
   with "Access Denied" → "No connection" in the UI.
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

**No per-app knob exists.** config.vdf's `ShaderCacheManager.App.<appid>` holds
only `ShaderCacheSize` (informational), not an enable/disable. Steam exposes
**no** per-game shader-cache control. The only lever is the global
`"DisableShaderCache" "1"` in the `ShaderCacheManager` block — which is exactly
what Steam's own "Shader Pre-Caching" toggle writes (verified on Deck) and what
moon writes.

**Resolution (v0.13.9).** The `.so` reverts to plain pass-through for keyless
depots (no placeholder, no package-0 exclusion). The shader pre-cache — which
can never succeed for a keyless game, and whose failure is cosmetic (the game
installs and plays; Proton/DXVK caches shaders locally regardless) — is
suppressed by writing the global `DisableShaderCache=1` into config.vdf at
add-game time (steamidra_lite), matching the Steam toggle / moon. The cost is
global (keyed games lose Valve's precompiled shader download too), but for the
2D indie titles this targets that is negligible. Making the shader pre-cache
actually *work* for a keyless game remains impossible without the real
shader-depot key (§13.8 top) — capture from an owner is the only path.

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

**Recommendation.** Keep the shipped global `DisableShaderCache` (§13.8) as the
default. Do **not** ship a manifest-fabrication hook. Path B is documented as
*technically located but deliberately not pursued*: the cost/fragility of
forging finalized manifest internals outweighs the benefit over the global
toggle, for the 2D-indie target where the precache is worthless anyway. If a
future build ever exposes a real per-app shader knob, or if we capture a real
shader-depot key from an owner (§13.8 top), revisit — those are clean; manifest
forgery is not.

**Artifacts.** Addresses above are for build `7c4ac73e` only and are recorded
for provenance, not used at runtime (lumalinux ships no hook on this path). The
binary was transferred to the `tmp/steamclient-bin` branch for the RE session
and should be deleted once this note is committed.

**Superseded by §13.10.** Path B (faking the *decrypt*) is the wrong layer.
Going one level UP — stopping the shader job from being *scheduled* — turns out
to be both clean and shippable. See §13.10.

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

Pattern `kShaderCacheDepotPattern` (prologue `57 56 53 E8…81 C3…8B 83 58 B7 02
00 8B 40 44 85 C0 75 0D 5B 31 C0`), verified UNIQUE on `7c4ac73e`. Collision
check: SLSsteam hooks the message dispatch, not shader functions — clean.

**Why this beats the global `DisableShaderCache` (§13.8).** The global flag
killed Valve's precompiled-shader download for *every* game (keyed and owned)
too; path C touches only the keyless games that can never use it anyway.
`steamidra_lite` therefore stops writing the global flag and *reverts* a
previously-written `DisableShaderCache "1" -> "0"` (`restore_shader_precache`)
so keyed/owned games get their shaders back and the hook governs the rest. The
global flag remains a manual fallback if the pattern ever breaks (a pattern miss
is non-fatal: the hook just doesn't install and behaviour reverts to pre-13.9).

**Failure mode on a Steam update.** If `kShaderCacheDepotPattern` stops matching,
`ShaderDepot` shows FAILED in the startup toast (it's a counted core hook) and
keyless games regress to the §13.8 loop until the pattern is re-derived
(`GetShaderCacheDepot` is anchored by the strings `shadercachedepot` /
`CGetShaderDepotManifestJob skipping because shader depot ID is invalid`).

This is the first known clean, per-game shader-pre-cache skip for a keyless
title — using Steam's own skip path, no global toggle, no manifest forgery.

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
