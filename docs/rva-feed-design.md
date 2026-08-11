# Design: per-build RVA feed (RVA-first resolution, byte-pattern fallback)

Status: proposal. Modeled on OpenSteamTool's `PatternLoader` (RVA-first, sig
fallback) and OpenSteam001/steam-monitor's per-DLL-hash TOML feed (`pattern` and
`ipc` branches), adapted to Linux / `steamclient.so`.

## 1. Problem

lumalinux locates its hook targets in `steamclient.so` by **byte pattern** (the
function prologue). A prologue is exactly what a Steam recompile perturbs, so a
major update can break a pattern even though the function is unchanged — the
"next update and we're stuck" risk. The RTTI path (#19) helps for DepotKey but
(a) still identifies the slot by the prologue signature, so it also dies on a
prologue change, and (b) breaks on split-mapping builds (see
`tools/xlate_vaddr.py` and the #19 follow-up).

## 2. Prior art we're copying (both verified working on Windows)

- **OpenSteamTool** `src/Utils/SteamMetadata/PatternLoader.cpp` — `FindPattern`:
  *Priority 1: RVA direct offset (`module_base + rva`); Priority 2: byte-signature
  scan.* Metadata is a per-DLL-hash TOML keyed by `Fnv1aHash(funcName)`.
- **steam-monitor** publishes, per build (file named `<sha256>.toml`):
  - `pattern` branch: `nameHash -> {rva, sig}` (incl. `ConfigStoreGetBinary` =
    our DepotKey).
  - `ipc` branch: per interface `vtable_rva` + per method `method_index`
    (the slot), `wrapper_rva`, `funcHash`, `fencepost` (reorder checksum).

The insight both encode: **don't fight resolution at runtime — precompute the
RVA once per build in CI, ship it, and resolve RVA-first.** An RVA is a direct
offset, so it is robust to prologue changes (it never reads the prologue).

## 3. Why lumalinux already has ~90% of this

- `res/updates.yaml` is **already fetched at runtime with a cache fallback**
  (`src/update.cpp` → `raw.githubusercontent.com/jayool/lumalinux/main/...`,
  cached to `~/.cache/lumalinux/.updates.yaml`). The RVA feed rides this exact
  path — **no new network dependency; offline uses the cache.**
- `tools/check_patterns.py` **already computes** per build, keyed by the file's
  SHA-256: `hooks[name].rvas` (via `scan_rvas`) and `rtti{slot, rva}` (via
  `rtti_derive_slot`, the CConfigStore vtable walk). It just doesn't emit them.
- The `.so` is pinned to a pattern group (`LUMALINUX_SAFEMODE_VERSION`); the
  baked `src/patterns.hpp` is the built-in fallback.
- `tools/xlate_vaddr.py` (validated live: `0x118c1f0 -> 0xc7bcf1f0`) is the
  Linux-specific piece: a file-vaddr → runtime translation that is correct even
  when segments load at different biases (where OST's naive `base + rva` would be
  wrong).

## 4. Key decisions

1. **Keyed by `steamclient.so` SHA-256.** Self-selecting per build; a v1 Deck
   hashes its own binary and gets v1's RVAs. Already how SafeMode picks.
2. **Fetched via the existing `updates.yaml` mechanism, cached.** No new network
   surface; offline → cache; same trust model as the whitelist (own repo, HTTPS).
3. **RVA-first, byte-pattern fallback.** Unlike OST (whose fallback sig is also
   from the feed), our fallback is lumalinux's **own baked `patterns.hpp`** — so
   an empty/stale feed never leaves a hook with nothing.
4. **RVAs are file vaddrs** (what static analysis produces) and are translated to
   runtime addresses via `xlate` (ELF phdrs + `/proc/self/maps` file offsets).
5. **Hash-match ⟹ correct-by-construction.** A feed entry is used only when the
   on-disk hash matches; the cron derived those RVAs from that exact binary and
   validated them (`check_patterns` unique-match) before publishing.

## 5. Feed format (extend `res/updates.yaml`)

Add a top-level `BuildRvas` map. It is **additive**: `update.cpp` today reads
only `SafeModeHashes`, so old runtimes ignore `BuildRvas`; the new resolver reads
it. RVAs are file vaddrs (image base 0), hex strings.

```yaml
SafeModeHashes:            # unchanged — the allow-list, grouped by pattern set
  20260611150000:
    - dd3ca9f549224da3e5514fecd9887d3bf3a8a219ff123d50fe3f7b5af888af19
    # ...

BuildRvas:                 # NEW — per exact steamclient.so hash
  dd3ca9f549224da3e5514fecd9887d3bf3a8a219ff123d50fe3f7b5af888af19:
    steam_version: 1785187029
    hooks:                 # hook name -> file vaddr (RVA)
      DepotKey:    "0x118c1f0"
      GMRC:        "0x4d9f00"
      BuildDep:    "0xfe1bf0"
      ShaderDepot: "0x1b3e90"
    # optional enrichment (steam-monitor `ipc` style): lets the runtime
    # cross-check DepotKey and detect a vtable reorder without reading a prologue.
    depotkey_vtable:
      class:      "12CConfigStore"
      vtable_rva: "0x2e6cbac"
      slot:       6
      fencepost:  "0x...."   # e.g. FNV of {slot-1, slot, slot+1} target RVAs
    finder:                  # optional: the package-0 finder's per-build disp (X)
      cache_global_disp: "0x3967c"
```

Notes:
- `hooks.DepotKey` is the accessor RVA the cron derived via the **RTTI vtable
  walk** (`rtti_derive_slot`), i.e. robust-to-prologue at derive time; the
  runtime just uses the number.
- `depotkey_vtable` is optional; if present the runtime can verify
  `vtable[slot]` translates to `hooks.DepotKey` and the fencepost matches.

## 6. CI / cron changes (`check_patterns.py`, `watch-steam.yml`)

`check_patterns` already produces `result.hooks[*].rvas`, `result.rtti.slot/rva`,
and the finder disp. Add:

- A `--emit-rvas <updates.yaml>` mode that, for the analyzed build's hash, writes
  the `BuildRvas[<hash>]` block (hooks RVAs + depotkey_vtable + finder disp),
  computing `fencepost` from the neighboring slot RVAs.
- `watch-steam.yml` calls it right after the existing whitelist/caps step and
  commits `updates.yaml` (same flow that appends hashes today). BLOCKING exit (3)
  still gates: no RVA block is written for a build whose criticals didn't validate.

No new analyzer is needed — this reuses the derivation lumalinux already runs.

## 7. Runtime resolver (new `src/rva_feed.{hpp,cpp}` + `src/vaddr_xlate.{hpp,cpp}`)

`vaddr_xlate` — port of `tools/xlate_vaddr.py`:

```cpp
namespace VaddrXlate {
  // Build a per-segment map from steamclient.so's ELF phdrs (read from disk)
  // and /proc/self/maps file offsets. Correct under split/multi-bias loading.
  bool Init(const char* steamclientPath);
  // file vaddr (RVA) -> live runtime address, or 0 if not in a file-backed seg.
  uintptr_t ToRuntime(uintptr_t fileVaddr);
}
```

`rva_feed`:

```cpp
namespace RvaFeed {
  // Parse the (already-fetched) updates.yaml, hash the on-disk steamclient.so,
  // select BuildRvas[hash]. Cheap: the SafeMode hash is computed anyway.
  bool LoadForCurrentBuild(const char* steamclientPath, const std::string& yaml);
  // Runtime address for a hook, or 0 if this build has no feed entry for it.
  uintptr_t Resolve(const char* hookName);
}
```

Each hook's `Install()` becomes:

```cpp
uintptr_t target = RvaFeed::Resolve("DepotKey");     // 1. RVA-first (translated)
const char* how = "rva";
if (!target) { target = Patterns::FindDepotKey(); how = "pattern"; }  // 2. fallback
if (!target) { /* per-hook degrade: disable this hook + notify (#13 Part 2) */ }
Log::Info("Hook install: name=DepotKey method=%s target=0x%lx", how, target);
```

Resolve internals: feed RVA → `VaddrXlate::ToRuntime` → **light check** the result
lands in an executable `steamclient.so` mapping → return. (Optional DepotKey
extra: verify `vtable[slot]` == this address and fencepost matches.)

## 8. DepotKey, specifically

- CI derives DepotKey's RVA via the CConfigStore **vtable walk** (already in
  `check_patterns`), which is reliable statically (single vaddr space, no live
  relocation/multi-bias chaos), and publishes it.
- Runtime uses that RVA + `xlate`. It **never reads the prologue** → survives a
  recompile. The **slot is decided in CI per build** → a vtable reorder is handled
  centrally (and flagged by `fencepost`), not by a fragile runtime walk — this is
  the "fixed-slot robustness without the fixed-slot risk" we couldn't get at
  runtime alone.

## 9. Trust & safety

- Feed is lumalinux's **own repo over HTTPS** (not a third-party mirror), same as
  the existing whitelist — moon's "mirror we don't control" objection doesn't
  apply.
- **Hash-keyed ⟹ correct-by-construction**: RVAs are used only for the exact
  binary they were derived from and CI-validated against.
- **Runtime light check** (target in `.text` of `steamclient.so`) catches a gross
  feed/binary mismatch; `fencepost` catches a DepotKey vtable mismatch.
- **Optional hardening (SFF-style):** sign `BuildRvas` with an RSA/Ed25519 key and
  verify in `update.cpp`. Defense-in-depth for the fetch path; a later add-on.
- Because an RVA controls where we detour, the light check + hash-match + own-repo
  are the floor; signing is the ceiling.

## 10. Offline & performance

- **Offline:** the cached `updates.yaml` is used (already the case for SafeMode);
  no feed → byte-pattern fallback. Never worse than today.
- **Performance: faster, not slower.** On a known build we replace *scan ~48 MB
  for each pattern* with *one map lookup + a few-KB phdr parse + arithmetic*. The
  48 MB SHA-256 is already paid by SafeMode. Only an unknown build scans (as
  today).

## 11. Robustness matrix

| Failure mode | Covered by |
|---|---|
| Prologue changes on a **published** build | RVA-first (offset, no prologue read) |
| Prologue changes on a build **newer than the cron** | byte-pattern fallback (baked), for the ~hours until the cron publishes |
| Vtable **reorder** (CConfigStore) | CI re-derives slot/RVA per build; `fencepost` flags drift |
| **Split-mapping** / multi-bias load | `xlate` (validated live) |
| Feed unreachable / offline | cached `updates.yaml`, then baked patterns |
| Bad/hostile feed entry | hash-match + CI validation + `.text` check (+ optional signing) |

## 12. Phased rollout

1. **CI emit** (`check_patterns --emit-rvas`) — start populating `BuildRvas` for
   new builds. No runtime change; pure data.
2. **`vaddr_xlate` C++** + unit test mirroring `tools/xlate_vaddr.py`.
3. **`rva_feed` C++** + wire ONE hook (DepotKey) RVA-first with pattern fallback;
   validate on-device (`method=rva target=…` matches the pattern target).
4. Extend to GMRC / BuildDep / ShaderDepot.
5. (Optional) `depotkey_vtable` cross-check + feed signing.

Each phase is independently shippable and fails closed to today's behavior.

## 13. Open questions

- Backfill `BuildRvas` for already-whitelisted hashes, or new builds only?
- Put `BuildRvas` under the pattern-group key (like `SafeModeHashes`) or flat by
  hash? Flat is simpler; RVAs are build-exact and don't depend on the group.
- Emit RVAs in `.text`-relative form or image-base-0 file vaddr? File vaddr keeps
  it aligned with `check_patterns`/`experiment_rtti_depotkey` reporting; `xlate`
  handles it.
