# lumalinux — Research log & internals

Detailed notes on how lumalinux works, how we found each piece, the dead-ends we
hit, and the reverse-engineering workflow — so this can be extended later without
repeating the whole journey.

Target binary during this research:
`~/.local/share/Steam/linux32/steamclient.so`, **ELF 32-bit i386**, ~48 MB,
BuildID `f92deb5ee064a2cf28977bd86a6ed43f420cfcba` (SteamOS, ~May 2026).
All RVAs/patterns below are for that build — **re-derive on Steam updates**.

---

## 1. The goal

Make Steam's **native Install button** download and run a game you don't own
(test case: Balatro, AppID 2379780), on a Steam Deck, **coexisting with
SLSsteam** (no fork, no patch of SLSsteam).

## 2. Division of labour (what each component provides)

| Concern | Provided by |
|---|---|
| Ownership spoof (app shows as owned, license checks pass) | **SLSsteam** (`CUser::CheckAppOwnership`, `GetSubscribedApps`, cached tickets) |
| PICS access token (so Steam can query appinfo for unowned apps) | **SLSsteam** (`Apps::sendPICSInfoRequest`, eMsg 8903) |
| Family-share / offline bits | **SLSsteam** |
| Surface the content depots into the download plan | **lumalinux** LoadPackage |
| Pin each depot to the right manifest (gid/size) | **lumalinux** BuildDep |
| Provide the depot AES decryption keys | **lumalinux** DepotKey |
| Provide the **manifest request code** (CDN download authorization) | **lumalinux** GMRC |

SLSsteam gets you ownership + appinfo. lumalinux gets you the actual bytes.

## 3. The Steam content-install flow (and where each hook sits)

Clicking Install kicks off, roughly:

1. **Ownership ticket** (eMsg 858, `CMsgClientGetAppOwnershipTicketResponse`).
   SLSsteam fakes ownership. The content log still prints
   `failed to update ownership ticket (Access Denied)` — this is **non-fatal**;
   the install proceeds.
2. **PICS appinfo** — Steam fetches the app's product info (depot list, manifest
   ids). Needs the access token SLSsteam injects.
3. **`CPackageInfoCache::LoadPackage(PackageInfo*, sha1, cn, p4)`** — when
   `PackageId == 0` (the implicit "free apps everyone owns" package), Steam's
   per-depot license filter consults this package. → **LoadPackage hook**:
   inject our depot ids into `pInfo->AppIdVec` so the content depots pass the
   filter instead of being dropped (→ "0 target depots" → instant "Fully
   Installed"/Play).
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
- Hook: when `PackageId==0`, append our **depot ids** (`KeyStore::GetAllDepotIds`,
  the equivalent of LumaCore's `GetAllDepotIds`) to `AppIdVec`. **Append in place**
  only (capacity is generous; `alloc=202 size=192` observed) — never realloc
  (see §6). Sanity check: the existing `AppIdVec` entries are real low app ids
  (observed `{5,7,8,90,...}`) — bail if they look bogus (wrong offset).

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
- On SteamOS the launcher is `/usr/bin/steam` (it sets SLSsteam's `LD_AUDIT`).
  Add the lumalinux `LD_PRELOAD` export there, before the final `exec`. The
  stock `~/.local/share/Steam/steam.sh` is Valve's runtime script and has no
  injection point — don't edit that.

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
- **LoadPackage realloc of `AppIdVec` crashes** the appinfo-cache rebuild thread
  (null deref in `libvstdlib_s.so`). The free package has spare capacity →
  append in place; never swap `m_pMemory`. (LumaCore calls Steam's
  `CUtlMemoryGrow`; we avoid growth entirely.)
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

| Hook | Anchor string | How the tool handles it |
|---|---|---|
| **BuildDep** | `"BuildDepotDependency"` | finds the string → the referencing function → re-extracts a fresh prologue pattern (PIC `call` rel32 + the GOT `add reg,imm32` auto-wildcarded). **Fully auto.** |
| **GMRC** | `"ContentServerDirectory.GetManifestRequestCode#1"` | same — **fully auto**, prints e.g. `E8 ?? ?? ?? ?? 05 ?? ?? ?? ?? 55 89 E5 …`. |
| **DepotKey** | *none* (leaf-ish, logs nothing) | re-validates the **current** pattern against the new binary; if it still matches **uniquely**, "keep it", else flags for manual re-derivation. |
| **LoadPackage** | *none* | same validation, but a **multi-match is expected** (the runtime enumerates candidates and picks one via `LUMA_LOADPKG_IDX`, default 0) so the tool just lists the candidate sites. |

So the two string-anchored hooks (BuildDep, GMRC) — the ones most likely to move
across builds — are auto-derived; the two anchorless ones are auto-*validated* and
flagged if they ever stop matching, at which point you re-derive them by hand in
the Ghidra GUI (locate the function per §4, copy ~28 prologue bytes, wildcard the
get_pc_thunk `call` rel32 and the following `add reg,imm32`).

**The wildcarding gotcha** (worth knowing if you extend the tool): the
get_pc_thunk-relative `add` has two encodings — `0x05` (`add eax,imm32`, 5 bytes)
and `0x81 /0` (`add r/m32,imm32`, 6 bytes). Both must have their imm32 masked;
GMRC uses the short `0x05` form, BuildDep/LoadPackage the `0x81` form.

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

`tools/vdf_inject_keys.py` writes keys into `config.vdf` without the `vdf` python
module — but those get pruned (§6), so it's only a belt-and-suspenders helper.

## 10. Open items / ideas for extension

- **Only depot 2379781 mounted** for Balatro (64 MB) — the game runs. Depot
  2379782 (81 MB) wasn't downloaded; likely the other-OS depot Steam doesn't need
  on the Deck (Proton). If a game needs multiple depots and some don't mount,
  investigate why Steam isn't requesting their manifest (OS filter / branch).
- `install.sh` could auto-patch `/usr/bin/steam` (currently manual, since the
  launcher path varies and it's a system file reset by SteamOS updates).
- **SteamOS updates reset `/usr/bin/steam`** → the LD_PRELOAD line must be
  re-applied after each update. A systemd unit or a `~/.steam` hook could
  re-apply it.
- `RelocateChainedJmp` (lmhook.cpp) is dormant infra for hooking a function
  SLSsteam already detoured; no current hook needs it.
- Consider prefetching all keystore gids' codes at startup (background) instead
  of lazily in the hook, if the first-manifest stall is noticeable.

## 11. lumalinux vs LumaCore — divergencias verificadas

Comparación side-by-side de las piezas equivalentes, leyendo el código de
ambos. Útil como mapa cuando algo no funciona y hay que decidir si replicar
literal LumaCore o asumir que nuestra divergencia es deliberada.

### 11.1 KeyStore vs `LuaLoader::DepotKeySet`

| | LumaCore | lumalinux |
|---|---|---|
| Fuente | `.lua` con `lua_State` real | `keys.txt` (formato propio) |
| Estructura | `unordered_map<DepotId, hex_string>` (**plano**) | `map<DepotId, {parent_app_id, gid, size, key}>` (rico) |
| `parent_app_id` | **NO EXISTE** — todos los depots son globales | Sí — invención lumalinux; usado por `GetDepotsForApp` |
| `manifest_size` | **Siempre 0** — comentario verbatim: *"size is always forced to 0 to prevent incorrect size from breaking Steam"* (`LuaLoader.cpp:172`) | Sí se almacena (del 3-arg `setManifestid` o del binary parse), pero **no se usa** efectivamente — BuildDep hook lo ignora deliberadamente. Campo dormant. |
| `GetAllDepotIds()` | Lista todos los depots del DepotKeySet | Igual |
| `GetDepotsForApp(app_id)` | **NO EXISTE** | Filtra `parent_app_id==app_id && gid!=0` |

Sin divergencia operativa real: aunque guardamos `manifest_size`, no lo
proyectamos a Steam. El campo podría eliminarse en una futura limpieza
(seguimos parseándolo del .lua para compatibilidad con el formato Hubcap).

### 11.2 LoadPackage hook (`PackagePatch::LoadPackage`)

| | LumaCore | lumalinux |
|---|---|---|
| Trigger | `pInfo->PackageId == 0` | Igual |
| Qué inyecta | `GetAllDepotIds()` (todos) | Igual |
| Filtra duplicados | NO | SÍ (`oldSize` loop) |
| Sanity check del vector | NO | SÍ (rechaza si `oldSize > 4096` o entries `> 50M`) |
| **Cómo crece `AppIdVec`** | `oCUtlMemoryGrow(&pInfo->AppIdVec, numToAdd)` — resuelve y llama la función real de Steam | `std::realloc(vec->m_pMemory, ...)` directo. Política mirror Source SDK `CUtlMemory::Grow`: duplica capacity si es posible, snap a tamaño pedido si no, mínimo 32 entries en primera asignación |

**Por qué realloc es seguro en Linux i386 hoy** (verbatim del comentario en
`load_package_hook.cpp:122`):

> Steam Linux i386 uses raw libc realloc for CUtlMemory (verified by
> disassembling the only 5 callers of `realloc@plt` in steamclient.so — the
> leaf helper at +0x5c79d30 is `realloc(ptr, count * elem_size)` with no
> tier0 allocator wrapper, confirming that `m_pMemory` was malloc-allocated
> and is safe to realloc from outside. This is the Linux equivalent of
> LumaCore's `oCUtlMemoryGrow` on Windows.

Histórico: una versión anterior (≤v0.7) usaba `m_pMemory` swap manual y
crashaba el hilo de appinfo-cache rebuild (null deref en `libvstdlib_s.so`).
El crash desapareció cuando se verificó que Steam usa libc realloc en
lugar de su allocator interno tier0, alineando nuestro `std::realloc` con
lo que Steam mismo hace. Ya no hay límite efectivo de capacity.

### 11.3 DepotKey hook (`DepotKeys::LoadDepotDecryptionKey`)

| | LumaCore | lumalinux |
|---|---|---|
| Función hookeada | `LoadDepotDecryptionKey(pObject, foo, KeyName, Key, KeySize)` — KeyValues-path-based | Igual signature |
| Cómo identifica el depot | Parsea `KeyName` (`.../<DepotId>\DecryptionKey`) | Igual |
| Validación de buffer | `if (KeySize >= key.size()) memcpy(...)` | `if (keySize >= 32 && key != nullptr) memcpy(...)` |
| Return value en éxito | `static_cast<int32>(key.size())` = 32 | `static_cast<int32_t>(32)` |
| Passthrough en miss | `oLoadDepotDecryptionKey(...)` | `g_origFn(...)` |

Sin divergencias materiales con LumaCore. Hook al KeyValues accessor con
validación de buffer y passthrough correcto en miss.

Histórico: ≤v0.8 lumalinux hookeaba el dispatcher externo
`(this_, app_id, depot_id, out_key)` **sin validar el tamaño del buffer**,
porque el `out_key` se asumía un raw 32-byte slot. Ese contrato resultó
falso (la cache lo trataba como estructura de 128 bytes con metadata),
causando heap corruption en Formula Legends (ver §12). v0.8+ migró al
KeyValues accessor de LumaCore con `KeySize` validado, eliminando el
overrun latente.

### 11.4 BuildDep hook (`ManifestBind::BuildDepotDependency`)

| | LumaCore | lumalinux |
|---|---|---|
| Función hookeada | `BuildDepotDependency(this, AppId, ..., pDepotInfo, pSharedDepotInfo, ...)` | Igual signature |
| Source de overrides | `LuaLoader::GetManifestOverrides()` (plano) | `KeyStore::GetDepotsForApp(AppId)` (filtra `parent_app_id==AppId`) |
| Match en patch | DepotId direct | Igual |
| Qué vectores parchea | **SOLO `pDepotInfo`** | **SOLO `pDepotInfo`** (mismo que LumaCore) |
| Patch what | `gid` + `size` (0 → keep original) | **Solo `gid`** — `ManifestSize` se mantiene a lo que Steam tenía |
| Contador `patched` | Cuenta cualquier match | **Solo si cambia algo** (`if (gid != newgid) patched++`) |
| Guard en `result==false` | Sí — no toca el vector | Sí (mirror) |

Sin divergencias operativas con LumaCore. Tanto vectores como campos
patcheados están alineados.

Histórico: ≤v0.5 lumalinux parcheaba **AMBOS** `pDepotInfo` y
`pSharedDepotInfo`, lo que rompía la coherencia del appinfo del app padre
del shared depot (VC 2022 Redist 228989 bajo app 228980) y causaba el crash
de Formula Legends (heap corruption → SIGSEGV en
`libc malloc_usable_size`). v0.6 limitó el patch a `pDepotInfo`. Además
≤v0.7 sobrescribía `ManifestSize` con nuestro size del KeyStore (en línea
con la divergencia teórica con LumaCore); v0.8 dejó de tocarlo,
replicando el comportamiento real de LumaCore.

**Nota sobre el WARN engañoso histórico**: el mensaje
`BuildDep: app X has N KeyStore depots but NONE matched` que aparecía cuando
los gids ya estaban correctos fue reescrito a `Log::Debug` con texto
neutral en v0.8.1.

### 11.5 GMRC hook (`ManifestBind::FetchSteamRun`)

| | LumaCore | lumalinux |
|---|---|---|
| Función hookeada | `GetManifestRequestCode` | Igual |
| Endpoint HTTP | `https://manifest.steam.run/api/manifest/{gid}` | `http://gmrc.wudrm.com/manifest/{gid}` |
| Estado del endpoint | **DEAD** (ya no responde) | **Vivo** (mismo que SteaMidra) |
| HTTPS / HTTP | HTTPS via WinHttp | HTTP plano via raw socket |
| Gating | Siempre que sea un manifest que conozca | `KeyStore::HasManifestGid(gid) \|\| KeyStore::HasDepot(depot_id)` — el OR permite cubrir el caso shader pre-cache donde Steam pide un manifest que no estaba en el .lua original |
| Return en éxito | (LumaCore tiene una capa async para llenar el cache; lo escribe via PacketRouter) | Síncrono — `*out_code = code; return 1` |

Aquí lumalinux está mejor en lo funcional (endpoint vivo, cubre shader
pre-cache) pero peor en resiliencia (un único host HTTP plano, sin cache
persistente, sin endpoint alternativo — ver Issue #2).

Histórico: ≤v0.8.0 el gating era solo `HasManifestGid(gid)`. Eso dejaba
caer al original (que el CDN deniega) los manifests del shader pre-cache,
porque su gid viene de PICS appinfo y nunca está pre-registrado en
`keys.txt`. Steam interpretaba el "Access Denied" como "No connection" en
la UI durante los primeros minutos de cada install nuevo (solo visible con
`.acf` de `StateFlags=1`; con `StateFlags=4` Steam saltaba el shader
pre-cache entero). v0.8.1 amplió el gating con `|| HasDepot(depot_id)`.

### 11.6 Resumen de sospechosos para crashes futuros

Las divergencias históricas con LumaCore que motivaban este resumen han
sido todas resueltas en v0.8.1 (ver subsecciones anteriores). El orden de
sospecha actual, ya sin paralelos con LumaCore como anclas, es:

1. **Patrones de Patterns desactualizados** tras un update de Steam — un
   solo byte change en el prologue de cualquier función hookeada
   desinstala el hook entero sin alarma clara.
2. **`gmrc.wudrm.com` caído / DNS bloqueado** — sin fallback offline
   ni endpoint alternativo (Issue #2). Síntoma: instalaciones nuevas que
   fallan en bucle con "Access Denied" → "No connection" en la UI.
3. **Race en `Log::Init`** — solo realista si LD_AUDIT preinit, el ctor
   LD_PRELOAD y el worker thread coinciden al microsegundo (Issue #5).
4. **Updates de SteamOS sobreescriben** `/usr/bin/steam` y la línea
   `LD_PRELOAD` se pierde — actualmente no hay re-aplicado automático.


## 12. El crash del DepotKey hook y su solución (Formula Legends)

Investigación completa, reproducida en un Steam Linux real (codespace Ubuntu
24.04, Xvfb+noVNC, SLSsteam + lumalinux), no hipótesis.

### 12.1 Síntoma

Al pulsar Install en un juego con shared depots (Formula Legends, app 3194360,
con redists 228989/228990 del app 228980), Steam crasheaba con
`free(): invalid pointer` → abort. El crash era **heap corruption**, no un
segfault en nuestros hooks: ocurría en código de Steam DESPUÉS de que el hook
DepotKey devolvía.

### 12.2 Aislamiento (qué hook, qué depots)

Desactivando hooks por env-var (`LUMA_NO_GMRC`, `LUMA_NO_DEPOTKEY`):
- Sin lumalinux → no crash (pero "content still encrypted": el flujo nativo
  necesita el hook para servir la key del depot de contenido).
- Con DepotKey desactivado → no crash, descarga, pero `Missing decryption key`
  para 3194361 → el hook DepotKey **es necesario**.
- El crash correlacionaba con **servir las keys de 228989/228990** (los redists
  que Steam YA POSEE vía la licencia de 228980). Servir la key del depot de
  contenido (3194361, no-owned) NO crasheaba.

### 12.3 Causa raíz

lumalinux ≤v0.8 hookeaba el **dispatcher externo** de depot keys
(`(this, app_id, depot_id, out_key)`) y lo **cortocircuitaba** (servía y
devolvía OK sin dejar correr la maquinaria de Steam). Para un depot que Steam
posee, eso deja su estado interno inconsistente (la ruta owned esperaba pasar
por su cache/refcount) → corrupción de heap.

Confirmado además que el `out_key` de esa función NO es un buffer de 32 bytes
crudo: la función de cache lo inicializa como estructura de 128 bytes
(`KeySize=128`). Nuestro `memcpy` de 32 cabía (no era overflow), así que el
crash era de **estado**, no de buffer.

### 12.4 Por qué la detección dinámica de ownership NO funciona en Linux

Intentamos replicar el `MarkOwned` de LumaCore (servir solo lo no-owned):
- **`CheckAppOwnership`**: SLSsteam lo spoofea → un app no-owned parece owned.
  Señal inservible.
- **Llamar al dispatcher original primero** (option-C): el original devuelve OK
  para 3194361 porque su key está en `config.vdf` → indistinguible de owned. Y
  para los que sí fallan, el **RPC denegado deja estado que crashea** al servir.
- **Llamar solo a la función de cache interna** (cache-first): detecta ownership
  bien (la cache refleja licencias reales, no `config.vdf`), pero su ruta de
  *miss* tiene un **efecto secundario interno** que crashea igual — verificado
  sondeando incluso con un buffer scratch (el `out_key` real quedaba intacto y
  aun así petaba). Conclusión: **no se puede llamar a ninguna función depot-key
  de Steam alrededor de servir sin envenenar el estado.**

### 12.5 La solución (paridad con LumaCore)

LumaCore no hookea el dispatcher: hookea la **KeyValues accessor interna**
`LoadDepotDecryptionKey(pObject, foo, KeyName, Key, KeySize)`, que el dispatcher
invoca (vía virtual call) para leer la key del store con el KeyName
`"Software\Valve\Steam\Depots\<depot>\DecryptionKey"`.

Hookeando AHÍ solo **respondemos la query**: la cache de Steam recibe un "hit"
limpio y el dispatcher continúa por su ruta normal (owned o no) — sin
cortocircuito, sin corrupción. Funciona para owned y no-owned por igual, **sin
lista estática de shared depots**.

Resolución de la función en Linux i386 (no estaba en patterns):
1. Localizar la cache fn (su propio patrón).
2. Seguir su virtual call: `this=*(global)+0xd60; vtable=*this; fn=*(vtable+0x18)`
   leyendo `/proc/PID/mem` del Steam vivo (root). Da la accessor interna.
3. Volcar su prólogo, derivar el patrón único (`kDepotKeyFnPattern`, v1.0).

Firma (cdecl i386, 5 args en pila): `pObject, foo, KeyName, Key, KeySize`.
El hook parsea el depot del KeyName, valida `KeySize >= 32`, hace
`memcpy(Key, ourkey, 32)` y `return 32`; passthrough si el KeyName no es de
depot o no tenemos la key.

### 12.6 Verificación end-to-end

Con el hook v1.0: FL sirve 228989, 228990 y 3194361 (`KeySize=128`) **sin un
solo crash dump**, descarga y descifra los 13 GB (`ASC.exe` + contenido en
disco), `appmanifest_3194360.acf` con `StateFlags=4` y `InstalledDepots`
poblado. Install nativo completo.

Residuo benigno: tras completar, Steam encola una auto-actualización que da
`Missing decryption key` / `UpdateResult=8` sin cambiar `StateFlags` de 4 — es
el residuo que `_patch_acf_error_state` (SteaMidra) limpia; no bloquea Play.

### 12.7 Implicación para el inventario §11.3

La divergencia §11.3 ("DepotKey hook sin validar KeySize") queda resuelta: el
hook v1.0 SÍ valida `KeySize` (como LumaCore) porque ahora hookea la variante
KeyName-based que recibe el tamaño. El crash, sin embargo, no era el buffer
(cabían los 32 bytes en 128) sino el cortocircuito del dispatcher — resuelto al
hookear la accessor interna.
