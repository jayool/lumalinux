# Running lumalinux side-by-side with CloudRedirect

CloudRedirect (Selectively11/CloudRedirect) targets the **cloud-save RPC
path** in `steamclient.so` via vtable swaps; lumalinux targets the
**content / depot path** via inline byte hooks. The two hooked sets are
disjoint, the mechanisms don't overlap, and they're designed to run side
by side.

> **Update (CloudRedirect v2.5.x, 2026-07).** CloudRedirect's Linux surface
> shrank: it dropped its Linux achievement injection, RecvPkt hook, and schema
> fetch (commit `31454e3`; SLSsteam now owns achievement-schema fetching) and
> removed the achievement-sync UI (`1095306`), leaving only its cloud-save and
> playtime hooks plus a native-stats export for SLSsteam. So the disjoint-surface
> premise holds and strengthens. The RecvPkt inline-detour rewrite (`71e0544`) and
> the manifest-endpoint override (`f3d9dfe`) in the same window are **Windows-only**
> and do not touch the Linux side or our GMRC path. Its LD_PRELOAD log fix
> (`0925cc2`: a global `std::mutex`/`std::string` used before C++ static init in an
> LD_PRELOAD constructor) also likely resolves the `cloud_redirect.so ->
> __backtrace -> ld.so` teardown `SIGSEGV` seen in a lumalinux coredump, so
> updating CloudRedirect to v2.5.4+ is worthwhile. lumalinux's own logger is not
> affected by that bug: its `g_logMutex` is a constexpr-constructed `std::mutex`
> (constant-initialized before any constructor) and it builds the log path with a
> local, not global, string.

## How CloudRedirect tracks Steam builds (RVA → signature resolver)

CloudRedirect used to be rigidly tied to a single Steam build, then stopped
being so. This matters for any "is CR compatible with the build I run?" gate:
the answer is **no longer** a lookup in a hardcoded list.

Timeline, traced from the `SC_RVA_GLOBAL_ENGINE` constant in
`src/common/steam_kv_injector.cpp` across release tags:

| Release | `SC_RVA_GLOBAL_ENGINE` | What the release did |
| --- | --- | --- |
| v2.1.8 | `0x17A70E8` | hardcoded offset |
| v2.1.9 | `0x17A70E8` (same) | that build didn't move the function |
| v2.2.1 | `0x17CC738` (**changed**) | new build → **re-hardcode the offset by hand** |
| **v2.2.2** | `0x17CC738` (same) | **added the resolver** (see below) |
| v2.2.3 | `0x17CC738` (same) | "official support for 1782437068" **without touching the RVA** |
| v2.6.2 | *(removed)* | **deleted the hardcoded RVAs entirely** |

- **≤ v2.2.1 — per-build treadmill.** Supporting a new build meant hardcoding
  new RVAs; when Steam moved a function (v2.2.1: `0x17A70E8`→`0x17CC738`) they
  had to ship a release with the new numbers. This is the "requires an update
  for every Steam client update" era.
- **v2.2.2 — the pivot ("CR no longer requires an update for every Steam client
  update").** They added a signature/RTTI scanner (`sig_scanner.cpp`,
  `sc_resolver`) and routed every address through
  `OvResolve(override, base, rva)` / `SC_RESOLVE(field, rva)` = **prefer the
  signature-scanned address; fall back to the hardcoded RVA only if the scan
  fails.** The version gate also became non-blocking (the patcher logs the
  detected build and continues instead of refusing). RVAs stayed as a safety
  net.
- **v2.2.3 — proof it works.** "Official support for the June 26th update
  `1782437068`" shipped with the RVA unchanged (`0x17CC738`) — the scanner
  resolved the new build on its own. The only concrete change was adding the
  build number to the cosmetic `SupportedSteamVersions` array.
- **v2.6.2 — full migration.** Hardcoded RVAs deleted; the resolver is now
  mandatory (`Configure()` / "resolver addresses required"), no fallback.

**Consequence for compatibility reasoning.** From v2.2.2 on, CR resolves its
hooks by signature at runtime — the same model SLSsteam and lumalinux already
use. "Does CR version *C* support Steam build *X*?" is therefore a
**signature-resolution** question (retrocompatible across builds; only breaks
if Steam changes the *shape* of the code, which needs a signature update, not a
per-build release), **not** a lookup in `SupportedSteamVersions`. That array is
a non-blocking, cosmetic (and drift-prone — it did not change in v2.6.2) layer;
do not treat its contents as an authoritative supported-builds list.

**The Linux `.so` is even more build-tolerant — it's RTTI-anchored, not
offset-based.** The RVA→resolver timeline above is the *Windows* path
(`src/platform/win/`, `steamclient64.dll` RVAs). The Linux hook
(`src/platform/linux/vtable_hook.cpp`) never used offsets at all: it locates
`steamclient.so` in memory via `/proc/self/maps`, finds the Itanium-ABI RTTI
type-name string for the target class
(`RTTI_NAME = "30CClientUnifiedServiceTransport"`), walks to its `typeinfo`,
then scans for the vtable whose header is `[offset_to_top=0, typeinfo_ptr]` and
swaps the function pointers. There is **no** hardcoded `steamclient` offset, **no**
`SupportedSteamVersions` check, and **no** build comparison anywhere in the Linux
path. Because it resolves the vtable by the C++ **class name** — a semantic
identifier that survives recompilation, refactors, and offset moves — a normal
Steam build update does not affect it. It breaks only on a *structural* change:
Valve renaming the class, altering its vtable layout (add/remove/reorder virtual
methods), or stripping RTTI (they don't). So on Linux the version lag is moot:
the 2.6.1/2.6.2 work was Windows RVA/resolver plumbing that the RTTI-based `.so`
does not need — CloudRedirect at 2.6.0 keeps resolving new builds by class name
regardless.

For a LumaDeck component-update design this means CloudRedirect does **not** fit
the hash-allowlist model the way SLSsteam/lumalinux `updates.yaml` does (CR
publishes no per-build hash list), and it does **not** need to: on Linux the
CloudRedirect Flatpak self-updates via `flatpak update` against its own OSTree
remote (`checkForFlatpakUpdate` / `applyFlatpakUpdate` in `ui-linux/backend.cpp`),
and the `.so` + Steam pin are curated upstream by Headcrab/Selectively11. The
Linux release channel currently tops at **2.6.0**; 2.6.1/2.6.2 are Windows-only.

## Install order

`setup.sh` installs the whole stack in one run and the wrapper loads whichever
`.so`s are present, so there's no manual "CR before lumalinux" ordering to get right
any more.

1. Run the installer, from **desktop mode** (its Steam restarts can trip Game Mode's
   crash-loop detector). It fetches SLSsteam + `library-inject` + **CloudRedirect**
   (`cloud_redirect.so` + `cloud_redirect_cli` + the `org.cloudredirect.CloudRedirect`
   Flatpak) + netsock + `liblumalinux.so`, writes the injection wrapper, and wires
   coverage:

   ```bash
   curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/setup.sh | bash
   ```

2. **CloudRedirect is enabled for you.** `setup.sh` sets `DisableCloud: no` in
   `~/.config/SLSsteam/config.yaml` (CloudRedirect ships as a core component — the
   `.so` download is fatal if it fails, so cloud is never left on without it). No
   manual config edit needed.

3. **Open the CloudRedirect Flatpak app once** to sign into your cloud provider
   (switch to desktop for this). CloudRedirect is inert until a provider signs in.

## How they coexist in the wrapper

Injection now comes from the wrapper at `~/.local/share/SLSsteam/path/steam` (not a
patched `steam.sh`). Before `exec`ing the real Steam, the wrapper builds the env from
whichever `.so`s are on disk (paraphrased):

```sh
# LD_AUDIT: library-inject first, then SLSsteam
export LD_AUDIT="$SLS_DIR/library-inject.so:$SLS_DIR/SLSsteam.so"

# LD_PRELOAD: prepend CloudRedirect then lumalinux (so lumalinux ends up FIRST),
# each only if its .so exists; libextest is appended after, Wayland only.
for _p in "$CR_SO" "$LL_SO"; do
    [ -f "$_p" ] && LD_PRELOAD="$_p${LD_PRELOAD:+:$LD_PRELOAD}"
done
export LD_PRELOAD
exec "$STEAM_BIN" "$@"
```

Final result inside the Steam process (both present):

```
LD_PRELOAD=/home/<user>/.local/share/lumalinux/liblumalinux.so:/home/<user>/.local/share/CloudRedirect/cloud_redirect.so
```

Both `.so`s load. lumalinux comes first in the chain so its symbols shadow anything
CR also provides, but the two hook disjoint functions in `steamclient.so`, with no
symbol-resolution conflicts observed. If CloudRedirect's `.so` isn't on disk the loop
simply skips it and Steam runs with lumalinux only — no `steam.sh` block-insertion
and no LD_PRELOAD-clobber to work around any more.

## Verifying both loaded

After restarting Steam:

- **lumalinux**: `~/.cache/lumalinux/lumalinux.log` ends with
  `3/3 hooks active` (or `4/4` if you set `LUMA_LOADPKG_DEBUG=1`).
- **CloudRedirect**: `~/.config/CloudRedirect/cloud_redirect.log` ends
  with `cloud_redirect.so active in process 'steam' (pid=…)`.

You can also read the live env of the running `steam` process:

```bash
PID=$(pgrep -f 'Steam/ubuntu12_32/steam ' | head -1)
tr '\0' '\n' < /proc/$PID/environ | grep -E '^LD_(PRELOAD|AUDIT)='
```

`LD_PRELOAD` should contain both `liblumalinux.so` and
`cloud_redirect.so`, in that order.

## When only one of the two appears

If lumalinux loads but CloudRedirect doesn't (or vice-versa), the missing `.so`
isn't on disk — the wrapper only adds a preload for a file that exists. Re-run the
installer to re-fetch it; the other component is untouched.

```bash
curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/setup.sh | bash
```

(If *neither* loads — no banner at all — that's the wrapper not being reached, not a
missing `.so`; see `maintenance.md` case B.)
