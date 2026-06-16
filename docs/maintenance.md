# Maintenance: fixing lumalinux after a Steam or SLSsteam update

Two distinct breakage cases. The log tells them apart, and case A has three
tiers of fix — start with the cheapest.

## Look at the log first

Log: `~/.cache/lumalinux/lumalinux.log`.

The startup toast shows `X/Y hooks active` (e.g. `3/3 hooks active` on v0.13.1
defaults). What to grep for, and what it tells you:

| Grep finds… | Diagnosis | Go to |
|---|---|---|
| `SafeMode` mismatch / `Curl Res` + the hash isn't whitelisted | Steam shipped a new `steamclient.so`; patterns probably still match | **A.1** Hash bump |
| `Hook install: name=<HOOK> … outcome=pattern_miss` | A pattern moved — that hook can't install | **A.2** / **A.3** Re-derive patterns |
| `PKG0_FINDER: cache-access idiom not found` or `GOT not derived yet` | The package-0 finder can't locate its anchors | **C** Finder anchors |
| No `lumalinux … preinit` banner at all from that boot | lumalinux isn't loading — the `LD_PRELOAD` block is gone | **B** SLSsteam launcher regenerated |

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

If `verify-fix` shows `outcome=pattern_miss` on one of the install-path hooks
(**DepotKey**, **BuildDep**, **GMRC**), a byte pattern actually moved. The
**string-anchored** hooks re-derive themselves; the **anchorless** one
(DepotKey) needs more attention.

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

   - **BuildDep** anchors on the string `"BuildDepotDependency"` → auto.
   - **GMRC** anchors on
     `"ContentServerDirectory.GetManifestRequestCode#1"` → auto.
   - **DepotKey** tries an *indirect* anchor: the dispatcher refs
     `"Software\Valve\Steam\Depots\"`, the script then follows the
     `CALL [reg+0x18]` to reach the inner accessor (RESEARCH §12.5). When the
     vcall walk resolves (best-effort — depends on Ghidra's analysis on the
     new build), the pattern is auto-derived; otherwise it falls back to
     validating the current pattern and points you at A.3.
   - **LoadPackage** (since v0.13.1) is diagnostic-only; the script flags it
     as such, and a `pattern_miss` here does NOT block installs (the package-0
     finder injects).
   - **Package-0 finder anchors** (§13.5) — the script also verifies them as
     part of this run; see C if either says NOT FOUND.

3. Paste the printed `UNIQUE` patterns into `src/patterns.hpp`.

4. Rebuild:
   ```sh
   ./tools/fetch_libmem.sh   # only if not already present
   mkdir -p build && cd build && cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
   ninja
   ```

5. **Bump `res/version.txt`** to a new timestamp (a real bump this time —
   the pattern set changed) and start a fresh `SafeModeHashes:` group with
   the verified hash(es) for the new binary. Tag a release; the CI workflow
   uploads the rebuilt `.so` to the GitHub release. Users that re-run
   `install.sh` (or LumaDeck's reinstall action) pick up the new binary.

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

## TL;DR — which fix to try, in order

1. **No banner in the log** → B (re-run `install.sh`).
2. **`SafeMode` mismatch but `verify-fix` is green** → A.1 (hash bump in
   `updates.yaml`, no rebuild).
3. **`outcome=pattern_miss` on DepotKey/BuildDep/GMRC** → A.2 / A.3
   (re-derive patterns, rebuild, new release).
4. **`PKG0_FINDER: ... not found`** → C (re-derive finder anchors by hand).
