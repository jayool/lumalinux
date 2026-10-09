# Design: per-build RVA feed (RVA-first resolution, byte-pattern fallback)

Status: implemented / shipped (v0.18.0; finder RVA-first 2026-09-08, finder
cache layout `cache_root_off`/`cache_nodes_off` 2026-09-22). Re-checked against
the code on 2026-10-09. Modeled on OpenSteamTool's `PatternLoader` (RVA-first, sig
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

*(That was the problem when the feed was designed. DepotKey has since gained a
resolver that reads no prologue at all — by name, `21IClientConfigStoreMap` /
`GetBinary` → `12CConfigStore`, `Rtti::ResolveVtableSlotByName` in
`src/rtti.cpp` — and the other hooks string or anchor rescues; see §7.)*

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
  cached to `~/.cache/lumalinux/.updates.yaml`). The RVA feed copies this
  fetch+cache pattern with its **own** URL
  (`…/main/res/rvas/<sha256>.yaml`) and its **own** cache
  (`~/.cache/lumalinux/rvas/<sha256>.yaml`), `src/rva_feed.cpp` `obtainYaml()` —
  **no new host; offline uses the cache.** `updates.yaml` itself plays no part
  in the feed.
- `tools/check_patterns.py` **already computed** per build, keyed by the file's
  SHA-256: `hooks[name].rvas` (via `scan_rvas`) and `rtti{slot, rva}` (via
  `rtti_derive_slot`, the CConfigStore vtable walk). It just didn't emit them
  (it does since Phase 1, §6).
- The `.so` is pinned to a pattern group (`LUMALINUX_SAFEMODE_VERSION`); the
  baked `src/patterns.hpp` is the built-in fallback.
- `tools/xlate_vaddr.py` (validated live: `0x118c1f0 -> 0xc7bcf1f0`) is the
  Linux-specific piece: a file-vaddr → runtime translation that is correct even
  when segments load at different biases (where OST's naive `base + rva` would be
  wrong).

## 4. Key decisions

1. **Keyed by `steamclient.so` SHA-256.** Self-selecting per build; a v1 Deck
   hashes its own binary and gets v1's RVAs. Already how SafeMode picks.
2. **Fetched like `updates.yaml`, cached — but on its own URL and cache.** Same
   fetch+cache pattern as `update.cpp`, same host; offline → cache; same trust
   model as the whitelist (own repo, HTTPS). A fetched body is accepted (and
   cached) only if it contains `hooks:`.
3. **RVA-first, byte-pattern fallback.** Unlike OST (whose fallback sig is also
   from the feed), our fallback is lumalinux's **own baked `patterns.hpp`** — so
   an empty/stale feed never leaves a hook with nothing.
4. **RVAs are file vaddrs** (what static analysis produces) and are translated to
   runtime addresses via `xlate` (ELF phdrs + `/proc/self/maps` file offsets).
5. **Hash-match ⟹ correct-by-construction.** A feed entry is used only when the
   on-disk hash matches; the cron derived those RVAs from that exact binary and
   validated them (`check_patterns` unique-match) before publishing.

## 5. Feed format — one file per build hash (`res/rvas/<sha256>.yaml`)

**Implemented (Phase 1).** Rather than a `BuildRvas` block inside
`res/updates.yaml`, the feed is **one file per build**, `res/rvas/<sha256>.yaml`,
mirroring steam-monitor's `<sha256>.toml` layout. This was chosen over an inline
block because: it never touches `updates.yaml` (its hand-maintained
`steam_version` / `caps` comments stay intact); `updates.yaml` — fetched by every
Deck on every boot — doesn't grow unbounded with every build's full RVA set; and
each Deck fetches only its own build's small file. RVAs are file vaddrs (image
base 0), hex strings.

The real file for build `bc54101b…` (Steam `1788652215`), as
`emit_rvas_file` writes it (comments after `#` on the keys are added here, not
emitted):

```yaml
# Auto-generated by tools/check_patterns.py --emit-rvas. Do NOT edit by hand.
# Per-build hook RVAs (file vaddrs) keyed by steamclient.so SHA-256,
# consumed RVA-first by the runtime resolver (docs/design/rva-feed-design.md).
# Byte patterns in src/patterns.hpp remain the fallback.
steamclient_sha256: "bc54101b290f9a5b4a0713c9084494a5f05097826f66a661dbe322c469b764a2"
steam_version: 1788652215    # only when --steam-version was passed
hooks:                       # UNIQUE, non-diagnostic hooks only (file vaddr)
  DepotKey: "0x11a4500"
  ShaderDepot: "0x1048840"
  Reconcile: "0x188c950"     # NotifyLicensesUpdated — no-restart reconcile
  GMRC: "0x1371ac0"
depotkey_rtti:               # CI record of the CConfigStore slot; the runtime does not read it
  class: "12CConfigStore"
  slot: 6
  rva: "0x11a4500"
finder:
  cache_global_disp: "0x3b7d4"
  cache_root_off: "0xc58"
  cache_nodes_off: "0xc6c"
  got_rva: "0x2f4a34c"
```

Notes:
- A hook is written only when it resolved **UNIQUE**; a moved/ambiguous hook is
  omitted so the runtime falls back to its byte pattern for that hook alone.
- Hooks in the `DIAGNOSTIC` tier are never written. **BuildDep** is one of them
  (disabled at runtime since SLSsteam 20260714 hooks `BuildDepotDependency`
  first), so it never has a feed entry, even though its `Install()` still asks
  the feed (`depot_dependency_hook.cpp`). LoadPackage is the other diagnostic.
- Not every file carries every key: `steam_version` is omitted when the cron
  did not pass it, and files written before 2026-09-08 / 2026-09-22 lack
  `got_rva` / `cache_root_off`+`cache_nodes_off` (`d0c0ff6e….yaml` has neither).
  A missing key just means that number comes from the scan.
- The runtime reads only `hooks` and `finder` (`RvaFeed`'s `load()`).
  `steamclient_sha256`, `steam_version` and `depotkey_rtti` are for humans and
  CI.
- `finder.cache_global_disp` is **not an RVA** and is not translated like one:
  it is the GOT-relative displacement of `CPackageInfoCache`'s global slot, which
  the finder adds to a GOT base it derives at runtime, landing in `.bss`. It is
  read via `RvaFeed::CacheGlobalDisp()`, not `Resolve()` — `VaddrXlate` and
  `Resolve`'s "must be inside an executable mapping" guard are both right for a code address and
  wrong for this one. Written only when the idiom resolved **UNIQUE**, same rule
  as the hooks. Consumed since 2026-09-08; before that it was published on every
  build and read by nobody, while the finder rescanned ~32 MB per launch to
  recompute it.
- `finder.got_rva` is the finder's **other** number, and it closes the half the
  feed used to leave scanning (published and consumed since 2026-09-08). It is
  the GOT base derived from the GMRC prologue tail, in the same file-vaddr space
  as the hook RVAs — so unlike `cache_global_disp` it **is** an address and
  **is** translated through `VaddrXlate`. But it is not a hook either: it points
  into `.got`, which is mapped and writable and *not executable*, so
  `Resolve()`'s `inSteamclientExec()` guard would reject every correct value.
  It is read via `RvaFeed::GotBase()`, which translates like `Resolve()` and
  then validates against the module's **whole** mapping (`inSteamclientAny()`)
  instead of its `r-x` span. Written only when `gmrc_tail` classified
  **UNIQUE**.
- `finder.cache_root_off` / `finder.cache_nodes_off` (published and consumed
  since 2026-09-22) name the `CPackageInfoCache` layout row — root-index and
  node-array offsets — under which the idiom resolved (`stable-0xc58` →
  `0xc58`/`0xc6c`; `beta-0xf90` → `0xf90`/`0xfa4`, `CACHE_LAYOUTS` in
  `check_patterns.py`, `kCacheLayouts` in `package_zero_finder.cpp`). Plain
  struct offsets, not addresses: no translation, no mapping check, read via
  `RvaFeed::CacheLayout()`. Written only next to a UNIQUE `cache_global_disp`
  (same gate, same row), and both or neither; without them the finder names the
  layout by scanning.
- The two finder fields gate **independently**, each on its own anchor's
  verdict. A build whose idiom is ambiguous can still have an unambiguous GOT,
  and shipping the half we are sure of beats shipping neither — the runtime
  falls back to scanning for whichever half is missing, per number, not
  all-or-nothing. `tools/test_cache_idiom.py` pins that (fold the two `if`s
  back into one and it goes red).
- `hooks.DepotKey`, like every hook, is the address where the cron's byte
  pattern matched uniquely (`scan_rvas`). The **RTTI vtable walk**
  (`rtti_derive_slot`) is a CI constraint on it — exactly one `CConfigStore`
  slot must match the pattern, else BLOCKING — and the **by-name** resolver
  (`rtti_derive_slot_byname`) is a second opinion that blocks if it resolves and
  disagrees. The runtime just uses the number. `depotkey_rtti` records the
  walk's slot/RVA and is **informational only**: nothing reads it at runtime. The
  `vtable[slot]` cross-check / `fencepost` this note once planned was **never
  built** — declined 2026-09-14 (C5a, `docs/nosotros.md` §4.4: the hash key and
  the by-name resolver already cover it).

## 6. CI / cron changes (`check_patterns.py`, `watch-steam.yml`) — done

`check_patterns` already produces `result.hooks[*].rvas`, `result.rtti.slot/rva`,
and the finder disp. Implemented:

- `check_patterns.py --emit-rvas <dir> [--steam-version N]`: on a **non-BLOCKING**
  verdict, writes `<dir>/<sha256>.yaml` (function `emit_rvas_file`).
- `watch-steam.yml` passes `--emit-rvas res/rvas --steam-version <ver>` on the
  validate step and `git add res/rvas` alongside the hash bump, so the RVA file is
  committed with the whitelist entry. A BLOCKING build writes no file (never
  trusted). The same `--emit-rvas` also runs on both re-derivation paths (§16).
  `tools/test_cache_idiom.py` unit-tests the finder half of `emit_rvas_file`
  with synthetic results; no test today covers the omission of moved and
  diagnostic hooks.

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
  // Runtime address for a hook, or 0 if this build has no feed entry for it.
  // The feed loads lazily on the first Resolve() (std::call_once): it hashes the
  // on-disk steamclient.so and fetches/caches res/rvas/<hash>.yaml (same flow
  // as updates.yaml, own URL and cache). No separate load call.
  uintptr_t Resolve(const char* hookName);
  // The finder's numbers (§5), same lazy load, each with its own gate:
  int32_t   CacheGlobalDisp();                         // not translated
  bool      CacheLayout(std::size_t* rootIdxOff, std::size_t* nodesOff);
  uintptr_t GotBase();                                 // translated, whole-mapping check
}
```

Each hook's `Install()` becomes:

```cpp
uintptr_t target = RvaFeed::Resolve("DepotKey");     // 1. RVA-first (translated)
const char* how = "rva";
if (!target) { target = Patterns::FindDepotKeyFunction(); how = "pattern"; }  // 2. fallback
if (!target) { /* 3. per-hook rescue, see below */ }
if (!target) { /* per-hook degrade: disable this hook + notify (#13 Part 2) */ }
Log::Info("Hook install: name=DepotKey method=%s target=0x%lx", how, target);
```

Step 3 is a rescue that reads no byte of the prologue, run only when the two
cheap resolvers came up empty: DepotKey **by RTTI name**
(`Rtti::ResolveVtableSlotByName`, `method=byname(rescue)`); GMRC, ShaderDepot
and BuildDep by **string xref** (`GmrcXref`, `method=xref(rescue)`); Reconcile
by its **callback-125 anchor** (`ReconcileAnchor`, `license_reconcile.cpp`).
The chain is feed → pattern → rescue.

Resolve internals: feed RVA → `VaddrXlate::ToRuntime` → **light check** the result
lands in an executable (`r-x`) `steamclient.so` mapping (`inSteamclientExec`) →
return.

## 8. DepotKey, specifically

- CI publishes DepotKey's pattern RVA only after the CConfigStore **vtable
  walk** (`rtti_derive_slot`) finds exactly one slot matching it and the
  **by-name** resolver does not disagree — both reliable statically (single
  vaddr space, no live relocation/multi-bias chaos).
- Runtime uses that RVA + `xlate`. It **never reads the prologue** → survives a
  recompile. The **slot is checked in CI per build** → a vtable reorder is caught
  centrally (a disagreement or a non-unique slot blocks the build), not by a
  fragile runtime walk — this is the "fixed-slot robustness without the
  fixed-slot risk" we couldn't get at runtime alone. There is no runtime
  `fencepost` / slot cross-check (declined, C5a — §5).

## 9. Trust & safety

- Feed is lumalinux's **own repo over HTTPS** (not a third-party mirror), same as
  the existing whitelist — moon's "mirror we don't control" objection doesn't
  apply.
- **Hash-keyed ⟹ correct-by-construction**: RVAs are used only for the exact
  binary they were derived from and CI-validated against.
- **Runtime light check** (target in an `r-x` mapping of `steamclient.so`)
  catches a gross feed/binary mismatch. A DepotKey vtable mismatch is caught in
  CI (§8), not at runtime.
- **Optional hardening (SFF-style):** sign the feed file with an RSA/Ed25519 key
  and verify it in the `.so`. Defense-in-depth for the fetch path — declined for
  now, see §14.
- Because an RVA controls where we detour, the light check + hash-match + own-repo
  are the floor; signing is the ceiling.

## 10. Offline & performance

- **Offline:** the cached `~/.cache/lumalinux/rvas/<hash>.yaml` is used; no
  feed → byte patterns, then rescues. Never worse than today.
- **Performance: faster, not slower.** On a known build we replace *scan the
  binary for each pattern* with *one map lookup + a few-KB phdr parse +
  arithmetic*. The feed hashes the `.so` (~50 MB today) **a second time**:
  `RvaFeed`'s `load()` calls `Utils::getFileSHA256` on its own, separately from
  SafeMode's hash in `update.cpp`. Only an unknown build scans (as today).

## 11. Robustness matrix

| Failure mode | Covered by |
|---|---|
| Prologue changes on a **published** build | RVA-first (offset, no prologue read) |
| Prologue changes on a build **newer than the cron** | byte-pattern fallback (baked), then the per-hook rescues (§7), for the ~hours until the cron publishes |
| Vtable **reorder** (CConfigStore) | CI checks slot/RVA per build (RTTI walk + by-name; non-unique or disagreeing blocks) |
| **Split-mapping** / multi-bias load | `xlate` (validated live) |
| Feed unreachable / offline | cached `~/.cache/lumalinux/rvas/<hash>.yaml`, then baked patterns, then rescues |
| Bad/hostile feed entry | hash-match + CI validation + `r-x` mapping check (signing declined, §14) |
| Finder anchors move on a **published** build | RVA-first on **both** halves (`cache_global_disp` + `got_rva`), each falling back to its own scan |
| Finder anchors become **ambiguous** | **not covered, by design**: CI blocks and publishes nothing, so there is no feed value to fall back to. Ambiguous means unresolved everywhere — runtime, CI and feed agree to refuse rather than guess |

## 12. Phased rollout

1. **CI emit** (`check_patterns --emit-rvas`) — populate `res/rvas/<hash>.yaml`
   for new builds. No runtime change; pure data. **DONE.**
2. **`vaddr_xlate` C++** + unit test mirroring `tools/xlate_vaddr.py`. **DONE.**
3. **`rva_feed` C++** + wire ONE hook (DepotKey) RVA-first with pattern fallback;
   validate on-device (`method=rva target=…` matches the pattern target). **DONE.**
4. Extend to GMRC / BuildDep / ShaderDepot / Reconcile. **DONE** — all five hooks
   ask the feed first; four have feed entries (BuildDep is diagnostic-tier and
   never published, §5, and off by default at runtime); end-to-end fetch (real HTTPS, hardcoded base, disk cache)
   validated live: `RvaFeed: loaded 4 hook RVA(s)` + `method=rva`. *(The base was
   `LUMA_RVAS_URL`-overridable during that validation; the override was removed
   later — see §15.)*
5. (Optional) `depotkey_vtable` cross-check + feed signing. **Both DECLINED**:
   signing for now, see §14; the runtime cross-check / `fencepost` on
   2026-09-14 (C5a, `docs/nosotros.md` §4.4). Never built.
6. **Finder, RVA-first.** **DONE (2026-09-08)**, both halves.
   `finder.cache_global_disp` via `RvaFeed::CacheGlobalDisp()` and
   `finder.got_rva` via `RvaFeed::GotBase()`, each `ficha → escaneo` and each
   falling back on its own. No revalidation of a feed value against the scan —
   the same rule fixed for DepotKey in `ec9e840`: the feed is a cache of the
   scan's result keyed by the build hash, so re-deriving to compare would cost
   exactly the scan being skipped. On a build with a complete feed entry the
   finder now does **no code scanning at all**; the log line says which halves
   came from where (`got_method=` / `disp_method=`).
   **2026-09-22:** `finder.cache_root_off` / `cache_nodes_off` added
   (`RvaFeed::CacheLayout()`), so the finder also takes its layout row from the
   feed.

Each phase is independently shippable and fails closed to today's behavior.

## 13. Resolved questions

- **Backfill** `res/rvas/` for already-whitelisted hashes? **No** — new builds only.
  The baked byte-pattern fallback already covers in-circulation builds; backfill
  buys little for the maintenance cost.
- Fetch each `res/rvas/<hash>.yaml` on demand (one small file per Deck) vs. bundle:
  **on demand**, keeps the per-boot download tiny.
- Emit RVAs as image-base-0 **file vaddr** (aligned with `check_patterns`
  reporting; `xlate` translates at runtime).

## 14. Feed signing — decision: DECLINED (revisit only for untrusted mirrors)

Signing the feed (Ed25519 detached sig + embedded pubkey) was considered and
**deliberately NOT adopted**, for reasons that stay true until the fetch topology
changes:

- **Signing does not move the trust root — it is still GitHub.** Users get the
  `.so` (and thus the embedded pubkey) from `jayool/lumalinux` releases, and the
  feed from the same repo. An attacker who can poison the feed via a repo/account
  compromise can equally ship a malicious `.so` with a malicious pubkey. A **CI-held
  key** therefore adds almost nothing (same owner, same root). Only an **offline
  key** would help — and that breaks the cron automation that is the whole point.
- **TLS already covers the everyday threat** (passive network MITM of the raw URL).
- **A forged RVA is not RCE.** The runtime already bounds it: `VaddrXlate::ToRuntime`
  must translate and `inSteamclientExec` requires the target to land in an
  executable (`r-x`) mapping of the TLS-delivered steamclient.so. Worst case is a
  hook at the wrong Valve function → malfunction/crash, and a rejected feed falls
  back to byte patterns.
- The SafeMode hash check (advisory in lumalinux, §16) is **self-validating**: the hash is computed locally from the
  real steamclient.so, so a forged `updates.yaml` can only add/remove hashes →
  "the tool breaks", recoverable, not catastrophic.

**Revisit when** the feed gains a mirror we do NOT control (as the GMRC code
cascade already has non-GitHub endpoints), or carries a payload with higher stakes
than a code-mapping-bounded RVA. It is a clean, non-blocking add at that point:
`res/rvas/<hash>.yaml.sig` + a vendored Ed25519 verify (TweetNaCl-style, no
libcrypto — keeps the reaper-safe property) + a 32-byte pubkey in a header. Fail
closed: bad/absent signature ⇒ ignore the feed ⇒ byte patterns.

## 15. Feed source overrides — REMOVED (2026-08-17)

`obtainYaml()` used to accept two runtime overrides of the feed source:

- `LUMA_RVAS_DIR` — read `<dir>/<hash>.yaml` from a local directory instead of fetching.
- `LUMA_RVAS_URL` — replace the base URL, so the fetch could be pointed at a branch or mirror.

Both were for local testing, and both **shipped in release builds**. That is the
problem: this feed decides **where hooks get installed inside Steam's process**.
Anything able to add an env var to Steam's launch — a modified `.desktop`, a
launcher script, another Decky plugin — could point the feed at a server it
controlled and pick our hook targets. It converted "can write a file in `$HOME`"
into "can influence code running inside Steam": a local privilege escalation, not
a remote entry point, but a free one.

Note this is a *different* threat from the one §14 weighs. §14 correctly declines
signing because the feed's trust root is GitHub and a forged RVA is bounded by
`VaddrXlate::ToRuntime` + `inSteamclientExec`. Those bounds still apply here — the
worst case remains a hook at the wrong Valve function inside steamclient's `r-x`
mapping — but the *attacker* is different: not someone who compromised the repo, someone
already on the device. Signing would not have closed it either (a local attacker
can drop an unsigned feed in the cache just as easily). Removing the override is
the proportionate fix, and it costs nothing.

**What changed:** the base URL is now a hardcoded string literal; both `getenv`
calls are gone. Nothing else referenced these vars (no CI workflow, no script, no
test — grepped).

**What we lose:** no runtime way to test the feed against a branch or a local
directory. To test, edit the URL in a local build. Do **not** reintroduce a
runtime override on a release path; if a dev-only override is ever wanted, gate it
at compile time (`#ifdef`), never on a marker file — a marker that *enables* a
dangerous path can be created by the very local attacker it would be guarding
against (unlike `~/.config/lumalinux/no_reconcile`, which only *disables* a
feature and is therefore safe to leave writable).

**Still open, not addressed here:** the disk cache at `~/.cache/lumalinux/rvas/`
is read back without revalidation and is writable by any same-user process. That
is the remaining local vector, and closing it needs in-`.so` verification — i.e.
the §14 "revisit" path. Unchanged priority: low, given the `r-x` mapping bounds.

## 16. Re-derived patterns now publish to the feed too (2026-08-17)

**The gap.** `watch-steam.yml` landed 2026-07-23; the RVA feed landed 2026-08-11,
19 days later. Only the CLEAN path was retrofitted with `--emit-rvas`, so the two
re-derivation paths kept committing `src/patterns.hpp` alone:

```
line 125:  git add res/updates.yaml res/rvas          <- CLEAN (exit 0)
line 251:  git add src/patterns.hpp                   <- non-critical moved (exit 2)
line 398:  git add src/patterns.hpp version.txt updates.yaml   <- critical moved (exit 3)
```

(Line numbers as of before the change. Today all three `git add`s include
`res/rvas`.)

That inverted the feed's whole point. On a CLEAN build the feed is redundant (the
byte pattern resolved — that is *how* the RVA was derived). On a moved-pattern
build, where a data-only fix would actually help, the feed was empty and every
user waited for a rebuild + release.

**The change.** Both re-derivation paths now pass `--emit-rvas res/rvas
--steam-version "$VER"` on the `check_patterns` re-validation they already run,
and commit `res/rvas` alongside `patterns.hpp`. Two lines per path, plus `VER` in
each step's `env`. `check_patterns` self-gates (`if args.emit_rvas and
result["verdict"] != "BLOCKING"`), so a re-validation that fails writes nothing.

**How the patterns are re-derived today.** Both legs (exit 2 and exit 3) run
the same chain, `tools/derive_python_first.sh`: a **Python locator first**
(seconds, deterministic) for each moved constant, and Ghidra headless
(`run_ghidra_derive.sh` → `derive_patterns.py`) **only** for the constants the
Python locators could not derive. A Python result is never overwritten by
Ghidra's; Ghidra's are merged in only when UNIQUE. Then
`apply_derived_pattern.py`, then the `check_patterns` re-validation above.

| Constant | Python locator | Anchor |
|---|---|---|
| `kDepotKeyFnPattern` | `derive_depotkey_byname.py` | RTTI **by name**: `21IClientConfigStoreMap` / `"GetBinary"` → map slot → `12CConfigStore` vtable |
| `kNotifyLicensesUpdatedPattern` (Reconcile) | `derive_reconcile_byanchor.py` | the function that posts **callback 125**, reads a field of `this` and bails on `<= 0` |
| `kGmrcFunctionPattern`, `kShaderCacheDepotPattern`, `kBuildDepotDependencyPattern` | `derive_bytext.py` | the UNIQUE `lea` of its string → function via `.eh_frame_hdr` |

Each Python locator is the runtime's own rescue for that hook (§7), so CI and
the Deck find the function the same way. The pattern itself is fabricated from
the located address by `derive_from_address.py` and grown until UNIQUE.

**Why unreviewed publication is acceptable here** — this was the open question,
and the existing pipeline already answers it. Reaching the commit step requires
`check_patterns` to return CLEAN against the patched header, and CLEAN is not
merely "the pattern matches somewhere":

- The text-anchored hooks are located by **stable Steam strings** —
  `"BuildDepotDependency"`, `"ContentServerDirectory.GetManifestRequestCode#1"`,
  `"shadercachedepot"` — in `derive_bytext.py` as in Ghidra's
  `derive_patterns.py`. Reconcile is located by **what it does** (posting
  callback 125), with no layout number and no prologue shape in the criteria;
  zero or two candidates is a refusal. The anchor *is* the function's identity.
- DepotKey has no in-function anchor, so it is the weak one. Ghidra's path
  (dispatcher → the `"Software\Valve\Steam\Depots\"` KeyValues path → follow
  `vtable[+0x18]`) has never resolved on headless Ghidra (selftest #7,
  2026-09-14), which is why DepotKey is now derived **by name** in Python and a
  DepotKey-only move never starts Ghidra. `check_patterns` then constrains it:
  `rtti_derive_slot` walks `CConfigStore`'s vtable and requires **exactly one
  slot** whose target matches the re-derived pattern, and the by-name check must
  agree with that slot and RVA. `NO_SLOT_MATCH`, `AMBIGUOUS`,
  `VTABLE_WALK_FAILED` and a by-name disagreement all append to `blocking` ⇒
  exit 3 ⇒ `applied=false` ⇒ issue, no PR.

Be precise about what that buys: RTTI takes the **same** `patstr`, so it is a
*constraint* ("the function this pattern finds must be a CConfigStore virtual"),
not an independent second derivation. It catches a pattern derived from a function
outside that vtable — which is the realistic failure — not a mis-derivation
that happens to land on another slot of the same vtable. And when DepotKey was
itself derived by name, the by-name agreement check re-asks the locator that
produced the address, so it confirms consistency, not identity; the identity
comes from the interface-map name and the `"GetBinary"` string.

Note also what CLEAN does **not** prove: it answers "does this pattern match
exactly once?", never "is this the right function?". The confidence comes from the
anchors (strings, callback 125, the RTTI name) and the RTTI constraint, not from
uniqueness alone.

**The behavioural change on the critical path, stated plainly.** The feed is keyed
only by the steamclient.so SHA-256 and is **not scoped by SafeMode group**, so a
deployed `.so` fetches it regardless of which pattern-set it was compiled with.
Publishing on exit 3 therefore means deployed binaries **self-heal on next boot**,
where previously they stayed inert until a Deck-tested release shipped. That is the
intended win, but it does remove a human step that used to exist for criticals, so
it is called out here and in the workflow comment rather than left implicit.

The release is still required: the feed only covers the one hash it was derived
from, while `patterns.hpp` is the fallback for every other build.

**Stale comment fixed in passing.** The exit-3 block claimed deployed `.so`s stay
inert because "SafeMode keeps blocking". lumalinux's hash check is **advisory**
(`main.cpp`, the hash-check block at ~107-126, toasts and proceeds to the pattern
scan); what actually keeps them inert is that their compiled-in patterns no longer
resolve. Same outcome, wrong mechanism, and the difference matters now that the
feed bypasses it. The PR-step comment is fixed; the same phrase survives in the
exit-3 leg's header comment ("SafeMode must keep blocking") and in the body of the
issue it opens ("SafeMode keeps blocking, correctly"), both in `watch-steam.yml`.
The PR and issue bodies also still name only the Ghidra `derive_patterns.py` path.
