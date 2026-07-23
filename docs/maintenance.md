# Maintenance: fixing lumalinux after a Steam or SLSsteam update

Two distinct breakage cases. The log tells them apart, and case A has three
tiers of fix — start with the cheapest.

## Look at the log first

Log: `~/.cache/lumalinux/lumalinux.log`.

The startup toast shows `X/Y hooks active` (e.g. `3/3 hooks active` on current
defaults: DepotKey, GMRC, ShaderDepot — BuildDep is not in the default set). What
to grep for, and what it tells you:

| Grep finds… | Diagnosis | Go to |
|---|---|---|
| `SafeMode` mismatch / `Curl Res` + the hash isn't whitelisted | Steam shipped a new `steamclient.so`; patterns probably still match | **A.1** Hash bump |
| `Hook install: name=<HOOK> … outcome=pattern_miss` | A pattern moved — that hook can't install | **A.2** / **A.3** Re-derive patterns |
| `PKG0_FINDER: cache-access idiom not found` or `GOT not derived yet` | The package-0 finder can't locate its anchors | **C** Finder anchors |
| No `lumalinux … preinit` banner at all from that boot | lumalinux isn't loading — the `LD_PRELOAD` block is gone | **B** SLSsteam launcher regenerated |
| `SLS-ach: could not resolve SLSsteam symbols` / `guard pattern not found` (native cheevos silently off) | SLSsteam was stripped/renamed/re-shaped; the achievement patch fail-closed | **D** SLSsteam in-memory patch |

If install is broken but you can't tell which row from a single line, do this
in order: B → A.1 → A.2/A.3 → C. They're listed by frequency: B and A.1 are
common; A.2 happens occasionally; C is rare but blocking when it hits.

---

## A) Steam client update — three tiers

### A.1 Hash bump (no rebuild, no release)

This handles **the majority of Steam updates**, because Valve usually moves
byte offsets without moving the prologues we anchor on. The hooks would
install fine — the only thing stopping us is lumalinux's SafeMode whitelist
not knowing the new `steamclient.so` hash yet.

How it works (`src/update.cpp`, `res/updates.yaml`, `res/version.txt`):

- `LUMALINUX_SAFEMODE_VERSION` is embedded at build time from
  `res/version.txt` (a timestamp like `20260611150000`).
- On boot, lumalinux fetches
  `https://raw.githubusercontent.com/jayool/lumalinux/main/res/updates.yaml`
  and looks under `SafeModeHashes:` for an entry matching its version. The
  list under it is the set of `steamclient.so` SHA-256 hashes known to be
  compatible with the patterns shipped in this lumalinux build.
- If the current `steamclient.so` hashes to one of those entries → green
  light, hooks install. If not → SafeMode refuses.
- **`updates.yaml` is fetched from `main` every boot.** The deployed lumalinux
  binary on user Decks does NOT need to be replaced for a hash bump to reach
  them — they pick it up automatically on next launch.

> **Build note (0.15.0+).** Releases ship with the gate ON — `build.yml` passes
> `-DLUMA_NO_UPDATE=OFF`. This is only safe because 0.15.0 removed SafeMode's
> hard link on libcurl/libcrypto: `src/sha256.cpp` is now a self-contained
> SHA-256, and `src/curl.cpp` `dlopen()`s libcurl lazily at runtime. The result
> has ZERO curl/openssl `NEEDED` entries, so it can no longer fail to load into
> the game `reaper` (the `CURL_OPENSSL_4` brick that forced the gate OFF by
> default in 0.13.6–0.14.x). **Do NOT re-link libcurl/libcrypto** — keep the
> dlopen/self-hash approach, or the reaper brick comes back. The `LUMA_NO_UPDATE`
> option still defaults ON for gate-less validation builds (`verify-fix.yml`).

Maintainer fix:

1. Get the new `steamclient.so` (from your Deck or from a user log):
   ```sh
   sha256sum ~/.local/share/Steam/linux32/steamclient.so
   ```

2. Verify the existing patterns still cover that binary by running the
   `verify-fix` workflow (`.github/workflows/verify-fix.yml`,
   `workflow_dispatch`). It builds the current lumalinux, downloads the
   pinned test deps, and runs the hooks against a real `steamclient.so` in
   CI. If all required hooks report `outcome=installed`, the patterns still
   match → this is a hash bump, not a re-derivation.

3. Append the new hash under the **current** `SafeModeHashes:` group in
   `res/updates.yaml` (do NOT bump `res/version.txt` for a hash bump — that
   would create a new group and require a new build). Commit and push to
   `main`.

That's it. Users get the fix on next Steam launch, zero action required.

### A.2 Re-derive moved patterns (rebuild + release)

If `verify-fix` shows `outcome=pattern_miss` on **GMRC**, a byte
pattern actually moved; the **string-anchored** hooks re-derive themselves.
(BuildDep is **diagnostic** and disabled by default — a `pattern_miss` on it does
not block; the blocking re-derive triggers are DepotKey + GMRC.)
**DepotKey is different since 2026-07-06**: the shipped hook resolves via RTTI
first (`CConfigStore` slot 6, RESEARCH §15), so its byte pattern is only a
fallback — a DepotKey miss (`method=none`) means BOTH the RTTI walk AND the
pattern failed (rare; read the `RTTI:` log lines first). You only re-derive
DepotKey's pattern to refresh that fallback, using the indirect anchor below.

> **Automated first (`watch-steam.yml`).** You rarely run the steps below by hand.
> When a Steam update moves a **critical** hook, the daily monitor runs the same
> Ghidra derivation and — if it produces a UNIQUE pattern that re-validates
> **CLEAN** — opens a PR that already contains **everything**: the new
> `src/patterns.hpp`, a bumped `res/version.txt` (a fresh `SafeModeHashes` group),
> and this build's hash whitelisted under that new group. So when you see a
> `fix(patterns): re-derived criticals + new group … (auto, needs release)` PR,
> your job is just: **(1) live-install test on a Deck → (2) merge → (3) tag a
> release.** The manual steps below are the fallback for when auto-derivation
> can't produce a clean pattern (it opens a tracking issue instead).
>
> **The order is safe either way — you cannot create the release/updates.yaml
> "fleco" by merging before releasing.** The release build publishes
> `res/version.txt` as an asset (the group-id compiled into that `.so`), and
> LumaDeck's update gate reads the *release's* `version.txt`, **not** `main`'s. So
> even though the merged PR makes `main`'s `updates.yaml` advertise the new build
> immediately, LumaDeck won't offer it until the matching release ships. Deployed
> `.so`s are unaffected too: each keys on its **own compiled** group
> (`src/update.cpp`: `clientHashMap[VERSION]`) and never sees the new group's hash,
> so SafeMode keeps blocking that build — correctly — until the user updates.

1. Grab the new `steamclient.so`:
   ```sh
   cp ~/.local/share/Steam/linux32/steamclient.so /tmp/
   ```

2. Re-derive using the Ghidra postScript (RESEARCH §8.1):
   ```sh
   # one-time import (~5–15 min):
   analyzeHeadless <proj> sc -import /tmp/steamclient.so

   # then, any time, re-derive:
   analyzeHeadless <proj> sc -process steamclient.so \
       -noanalysis -scriptPath tools -postScript derive_patterns.py
   ```

   - **BuildDep** anchors on the string `"BuildDepotDependency"` → auto
     (informational only now — the hook is disabled by default, so re-deriving
     BuildDep doesn't block installs).
   - **GMRC** anchors on
     `"ContentServerDirectory.GetManifestRequestCode#1"` → auto.
   - **DepotKey** (only its fallback pattern — runtime uses RTTI, §15) tries an
     *indirect* anchor: the dispatcher refs
     `"Software\Valve\Steam\Depots\"`, the script then follows the
     `CALL [reg+0x18]` to reach the inner accessor (RESEARCH §12.5). When the
     vcall walk resolves (best-effort — depends on Ghidra's analysis on the
     new build), the pattern is auto-derived; otherwise it falls back to
     validating the current pattern and points you at A.3.
   - **LoadPackage** (since v0.13.1) is diagnostic-only; the script flags it
     as such, and a `pattern_miss` here does NOT block installs (the package-0
     finder injects).
   - **ShaderDepot** (since v0.14) anchors on the string `"shadercachedepot"`,
     which `GetShaderCacheDepot` references directly — so it's **auto-derived**
     by the script like BuildDep/GMRC (since v0.14.1). `extract_pattern`
     wildcards the `mov eax,[picbase+0x2b758]` global offset for you (it shifts
     per build, RESEARCH §13.10). A `pattern_miss` here does NOT block installs —
     only the per-game shader skip is lost; the global `DisableShaderCache` is a
     manual stop-gap until you paste the fresh pattern.
   - **Package-0 finder anchors** (§13.5) — the script also verifies them as
     part of this run; see C if either says NOT FOUND.
   - **NotifyLicensesUpdated** (since v0.16.15, the no-restart licence reconcile,
     `src/license_reconcile.cpp`) — the **LEAST urgent** of all: it is
     **non-load-bearing** and **unique-match-or-no-op**. A miss on this build
     (`FindNotifyLicensesUpdatedFunction` logs `N match(es), need exactly 1`)
     just disables the no-restart path → **Add Game falls back to needing a Steam
     restart** (the old behaviour). It NEVER blocks installs and NEVER crashes.
     Default ON since v0.16.16 (kill-switch `LUMA_NO_RECONCILE`), so a break
     silently falls back to the restart path. Re-derive via the RTTI anchor: the
     string `"17LicensesUpdated_t"`
     (the callback type's `type_info` name) is referenced right before the
     function posts callback `0x7d`; find that xref, walk to the enclosing
     function prologue, and mask the volatile bytes (get_pc_thunk rel, PIC add
     imm, frame size, the `mov edi,[eax+0x1bXX]` CUser member offset that drifts,
     the spill offset) — exactly moon's pattern in `patterns.hpp`
     (`kNotifyLicensesUpdatedPattern`). Ported from slsteam-moon; keep the
     unique-match requirement so a re-derive that isn't unique bails safely.

3. Paste the printed `UNIQUE` patterns into `src/patterns.hpp`.

4. Rebuild:
   ```sh
   ./tools/fetch_libmem.sh   # only if not already present
   mkdir -p build && cd build && cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
   ninja
   ```

5. **`res/version.txt` + a fresh group** — *the auto PR already did this step*; on
   the manual path, bump `res/version.txt` to a new timestamp (a real bump this
   time — the pattern set changed) and start a new `SafeModeHashes:` group with the
   verified hash(es) for the new binary:
   ```sh
   tools/whitelist_hash.py <sha256> --create-group --steam-version <ver> \
       --caps "shader=ok reconcile=ok"
   ```
   Then **tag a release**: `build.yml` uploads the rebuilt `.so` **and
   `res/version.txt`** to the GitHub release. That `version.txt` asset is the
   group-id compiled into the `.so`, and LumaDeck's update gate reads it (from the
   **release**, not `main`) to decide which builds the shipping binary can hook —
   so never omit it. Users that re-run `install.sh` (or LumaDeck's reinstall
   action) pick up the new binary.

   > Only a **critical** move bumps the group (per `CMakeLists.txt`). A
   > non-critical re-derive (ShaderDepot / Reconcile) keeps the **same** group —
   > the build already works, so its hash stays whitelisted where it is, just with
   > a `# caps:` note; that release does not bump `version.txt`.

### A.3 Manually re-derive an anchorless hook (DepotKey)

DepotKey has no anchor string, so `derive_patterns.py` only auto-**validates**
it: if its current `kDepotKeyFnPattern` still matches uniquely in the new
binary, the script says "keep it"; if not, it flags it for manual work.

Manual re-derivation in Ghidra:

1. Locate the cache function (its own pattern) and follow its virtual call
   to the inner accessor — same chain documented in RESEARCH §12.5:
   `this = *(global)+0xd60; vtable = *this; fn = *(vtable+0x18)`.
2. Dump the prologue (~28 bytes) of that function. Wildcard the
   get_pc_thunk `call` rel32 and the following PIC `add` immediates as
   described in RESEARCH §8.1 ("the wildcarding gotcha" — DepotKey uses
   the `0x81` form for the `add`, GMRC uses `0x05`).
3. Verify the pattern is **unique** in the new binary.
4. Paste into `src/patterns.hpp`, then continue with A.2 step 4 onwards.

Note (v0.13.1+): the **LoadPackage** hook is opt-in (`LUMA_LOADPKG_DEBUG=1`).
Its pattern doesn't need to keep matching for installs to work — the
package-0 finder doesn't depend on it. If you only use LoadPackage for
diagnostics, leave a broken pattern flagged in `derive_patterns.py` output
and revisit only if you need the diagnostic.

---

## B) SLSsteam launcher regenerated (no lumalinux banner at all)

**Symptom**: Steam starts with no `lumalinux v… preinit (…)` banner in the
log from that session. Toast doesn't show up either. lumalinux isn't being
loaded at all.

**Cause**: **SLSsteam** is the piece that actually does ownership / licensing
work; it ships with its own updater called **Headcrab** (the
`h3adcr-b-modul3s` repo, installed by `enter-the-wired`). When Headcrab
runs, it **regenerates** `~/.local/share/Steam/steam.sh` from a fresh
upstream copy. The `LD_PRELOAD=…liblumalinux.so` block that lumalinux's
`install.sh` had inserted goes with it. The deployed `.so`, the `keys.txt`,
and SLSsteam itself are untouched — only the launcher wrapper is.

**Fix**: re-run lumalinux's installer to re-insert the block.

```bash
curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/install.sh | bash
```

> This happens on every Headcrab Updater run. Until upstream Headcrab grows
> a hook point for third-party `LD_PRELOAD` additions (or until LumaDeck's
> "Install / Reinstall Dependencies" action wraps both Headcrab and
> lumalinux's installer in sequence), the re-run is manual.

---

## C) Package-0 finder lost its anchors (rare but blocking)

**Symptom**: hooks install fine (`3/3 hooks active`), but installs hang at
"0 target depots" / "Fully Installed" with 0 bytes. The log shows the finder
giving up:

```
PKG0_FINDER: cache-access idiom not found in r-x — class layout changed?
```

or

```
PKG0_FINDER: GOT not derived yet (GMRC prologue tail not located)
```

**Cause**: the finder doesn't use a byte pattern. It derives the address of
the live package cache at runtime from two stable anchors (RESEARCH §13.5):

- the cache-access idiom `lea r1,[GOT+X] ; mov r2,[r1] ; mov r3,[r2+0xc58]`,
  anchored on the **`0xc58` root-offset** of `CPackageInfoCache`;
- the **GMRC prologue tail** (`05 add eax,imm32` plus the
  `55 89 E5 …` that follows).

If Steam moves `0xc58` (a class-layout change in `CPackageInfoCache`) or the
GMRC prologue tail (`55 89 E5 …` register choreography differs), the
runtime derivation fails and the finder never seeds package 0. **No hook
pattern is broken**, but installs are still dead.

**Diagnose** (since v0.13.1, automated by `derive_patterns.py`):

Run the same Ghidra postScript used for hook patterns (§A.2). It now verifies
the two finder anchors and prints PRESENT / NOT FOUND. The expected output on
a healthy binary is:

```
---- package-0 finder anchors (§13.5) ----
  [a] cache-access idiom (anchored on tree-root offset 0xc58):
      PRESENT @ <addr>   disp32=0x<X>  (cache_global = GOT + disp32)
      OK — finder's cache locator will work on this binary.
  [b] GMRC prologue tail (survives the hook detour):
      PRESENT @ <addr>   (unique)
      OK — finder's GOT derivation will work on this binary.
```

If `[a]` says NOT FOUND → `0xc58` moved (root offset of `CPackageInfoCache`).
If `[b]` says NOT FOUND → the GMRC prologue tail changed.

**Fix** (manual code edits, no patterns.hpp involved):

1. Diff `CPackageInfoCache` in the new binary against the old one to find
   the new root-offset (very likely still around `0xc5n` / `0xc6n`).
2. Update `kCacheRootIdxOff` / `kCacheNodesOff` (and the related node
   offsets if anything else moved) at the top of
   `src/hooks/package_zero_finder.cpp`.
3. Update the literal `0xc58` inside `FindCacheGlobalDisp` — it's the anchor
   that confirms the idiom match.
4. If the prologue tail changed, update the `tail` byte array inside
   `DeriveGotBase`.
5. Rebuild + release as in A.2 step 4–5.

Worth opening an issue with the failing log line so the diagnostic landing
in the next release is sharper.

---

## D) SLSsteam in-memory patch (native achievements)

lumalinux patches one thing *inside* `SLSsteam.so` at startup — the only place it
modifies SLSsteam rather than just coexisting with it: the native-achievement guard
(`sls_achievement_unblock`, RESEARCH §17). It is **fail-closed** — if its
anchor/symbols don't resolve exactly, it no-ops and logs a warning; the feature
degrades (native cheevos → off) but Steam never crashes.

> There used to be a second patch, `sls_update_unblock` (RESEARCH §16), that
> neutralised SLSsteam's update-clear. It was **removed in v0.16.18**: SLSsteam changed
> that mechanism to a config-gated `GetUpdateInfo` hook (`20260714131044`), so the
> instruction it anchored on is gone. Update-unblocking is now a LumaDeck config
> concern (`DisableUpdates: no`). See `docs/slssteam-analysis.md` §7.6.

**Native achievements silently off.** Grep `SLS-ach`:

- `SLS-ach: could not resolve SLSsteam symbols` → SLSsteam was stripped or renamed
  its `CUser::isSubscribed` / `CConfig::isAddedAppId` / `g_config` /
  `sendAndRecvGet*Stats` symbols. Update the mangled names in
  `src/sls_achievement_unblock.cpp` to match the new SLSsteam.
- `SLS-ach: guard pattern not found exactly once` → SLSsteam's codegen shifted the
  guard shape (`call isSubscribed; add esp,imm; test al,al; jne`). Re-derive it.
- Either way it's a no-op, not a crash. `LUMA_NO_SLS_ACH_UNBLOCK=1` force-disables;
  `LUMA_SLS_ACH_TRACE=1` prints the per-call guard trace.

**⚠️ Live-patching safety — learn from the v0.16.7 OOBE.** When you touch any code
that writes into *live, multithreaded* SLSsteam/steamclient memory (`WriteRel32`,
the achievement guard's `rel32` write, any future in-memory patch), two rules are
non-negotiable:

1. **Keep `PROT_EXEC` set during the write** — `mprotect(R|W|X)`, not `R|W`.
   Dropping execute makes the whole page non-executable for the write window; any
   thread fetching an instruction from it faults.
2. **Make the operand store atomic** — a single naturally-aligned ≤8-byte store
   (`__atomic_store_n`), never a byte-wise `memcpy`. A concurrent thread executing
   the instruction must read either the whole old operand or the whole new one,
   never a torn mix.

Skipping either caused the v0.16.7 OOBE: a byte-wise, non-executable-window write
of the achievement guard's `rel32` let a Steam thread read a half-written call
target and jump to a garbage address → `SIGILL` → 5 fast exits → gamescope wiped
`~/.local/share/Steam`. Full postmortem in RESEARCH §17.3–17.4. (The since-removed
`sls_update_unblock`'s immediate write was hardened the same way in v0.16.8; the
same discipline applies to any future in-memory patch.)

---

## TL;DR — which fix to try, in order

1. **No banner in the log** → B (re-run `install.sh`).
2. **`SafeMode` mismatch but `verify-fix` is green** → A.1 (hash bump in
   `updates.yaml`, no rebuild).
3. **`outcome=pattern_miss` on DepotKey/GMRC** → A.2 / A.3
   (re-derive patterns, rebuild, new release).
4. **`PKG0_FINDER: ... not found`** → C (re-derive finder anchors by hand).
5. **`SLS-ach: could not resolve…` / native cheevos off** → D (update SLSsteam
   symbols/pattern; it's fail-closed, not a crash).
