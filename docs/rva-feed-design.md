# Design: per-build RVA feed (RVA-first resolution, byte-pattern fallback)

Status: implemented / shipped (v0.18.0). Modeled on OpenSteamTool's `PatternLoader` (RVA-first, sig
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

## 5. Feed format — one file per build hash (`res/rvas/<sha256>.yaml`)

**Implemented (Phase 1).** Rather than a `BuildRvas` block inside
`res/updates.yaml`, the feed is **one file per build**, `res/rvas/<sha256>.yaml`,
mirroring steam-monitor's `<sha256>.toml` layout. This was chosen over an inline
block because: it never touches `updates.yaml` (its hand-maintained
`steam_version` / `caps` comments stay intact); `updates.yaml` — fetched by every
Deck on every boot — doesn't grow unbounded with every build's full RVA set; and
each Deck fetches only its own build's small file. RVAs are file vaddrs (image
base 0), hex strings.

```yaml
# res/rvas/dd3ca9…af19.yaml  — auto-generated, do not edit
steamclient_sha256: "dd3ca9f549224da3e5514fecd9887d3bf3a8a219ff123d50fe3f7b5af888af19"
steam_version: 1785187029
hooks:                    # UNIQUE, non-diagnostic hooks only (file vaddr)
  DepotKey: "0x118c1f0"
  GMRC: "0x4d9f00"
  ShaderDepot: "0x1b3e90"
  Reconcile: "0x5a11c0"    # NotifyLicensesUpdated — no-restart reconcile
                           # (BuildDep is emitted only when it resolves
                           #  uniquely on the build; omitted otherwise)
depotkey_rtti:            # CConfigStore vtable slot (reorder-drift detection)
  class: "12CConfigStore"
  slot: 6
  rva: "0x118c1f0"
finder:
  cache_global_disp: "0x3967c"
```

Notes:
- A hook is written only when it resolved **UNIQUE**; a moved/ambiguous hook is
  omitted so the runtime falls back to its byte pattern for that hook alone.
- `hooks.DepotKey` is the accessor RVA the cron derived via the **RTTI vtable
  walk** (`rtti_derive_slot`) — robust-to-prologue at derive time; the runtime
  just uses the number. `depotkey_rtti.slot` enables a later `vtable[slot]`
  cross-check / fencepost (a `depotkey_vtable.vtable_rva` + `fencepost` can be
  added when that cross-check lands).

## 6. CI / cron changes (`check_patterns.py`, `watch-steam.yml`) — done

`check_patterns` already produces `result.hooks[*].rvas`, `result.rtti.slot/rva`,
and the finder disp. Implemented:

- `check_patterns.py --emit-rvas <dir> [--steam-version N]`: on a **non-BLOCKING**
  verdict, writes `<dir>/<sha256>.yaml` (function `emit_rvas_file`).
- `watch-steam.yml` passes `--emit-rvas res/rvas --steam-version <ver>` on the
  validate step and `git add res/rvas` alongside the hash bump, so the RVA file is
  committed with the whitelist entry. A BLOCKING build writes no file (never
  trusted). Unit-tested with a synthetic result (moved + diagnostic hooks omitted).

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
  // on-disk steamclient.so and fetches/caches res/rvas/<hash>.yaml (like
  // updates.yaml). No separate load call — Resolve() is the whole public surface.
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

1. **CI emit** (`check_patterns --emit-rvas`) — populate `res/rvas/<hash>.yaml`
   for new builds. No runtime change; pure data. **DONE.**
2. **`vaddr_xlate` C++** + unit test mirroring `tools/xlate_vaddr.py`. **DONE.**
3. **`rva_feed` C++** + wire ONE hook (DepotKey) RVA-first with pattern fallback;
   validate on-device (`method=rva target=…` matches the pattern target). **DONE.**
4. Extend to GMRC / BuildDep / ShaderDepot / Reconcile. **DONE** — all five hooks
   resolve RVA-first; end-to-end fetch (real HTTPS, hardcoded base, disk cache)
   validated live: `RvaFeed: loaded 4 hook RVA(s)` + `method=rva`. *(The base was
   `LUMA_RVAS_URL`-overridable during that validation; the override was removed
   later — see §15.)*
5. (Optional) `depotkey_vtable` cross-check + feed signing. **Signing: DECLINED
   for now** — see §14.

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
  must translate and `inSteamclientExec` requires the target to land in the
  TLS-delivered steamclient.so `.text`. Worst case is a hook at the wrong Valve
  function → malfunction/crash, and a rejected feed falls back to byte patterns.
- The SafeMode gate is **self-validating**: the hash is computed locally from the
  real steamclient.so, so a forged `updates.yaml` can only add/remove hashes →
  "the tool breaks", recoverable, not catastrophic.

**Revisit when** the feed gains a mirror we do NOT control (as the GMRC code
cascade already has non-GitHub endpoints), or carries a payload with higher stakes
than a `.text`-bounded RVA. It is a clean, non-blocking add at that point:
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
worst case remains a hook at the wrong Valve function inside steamclient's `.text`
— but the *attacker* is different: not someone who compromised the repo, someone
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
the §14 "revisit" path. Unchanged priority: low, given the `.text` bounds.
