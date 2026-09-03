# SteamFlipper vs the LumaDeck stack — exhaustive analysis

*In progress. **§1–§4 complete** (provenance and inventory; the injection model;
the gate-by-gate comparison). §5 (surviving a Steam update), §6 (trust and risk)
and §7 (verdict + adversarial pass) are pending and are marked as such below. Nothing
here is a recommendation to change LumaDeck or lumalinux. **Read §0 first**:
nothing was executed, and the evidence tags are load-bearing.*

---

## §0 Scope, method, evidence rules

### What is being compared

| Side | Components |
|---|---|
| **Ours** | lumalinux (`.so`) + SLSsteam (stock, unforked) + LumaDeck (Decky plugin) + CloudRedirect |
| **Theirs** | SteamFlipper (one `.so`, injected into the 32-bit Steam client) |

SteamFlipper is a single in-process module that spans what our stack splits
across three components. Like OpenSteamTool before it (`opensteamtool-findings.md`),
its ownership/PICS/DLC layer is **SLSsteam's** territory, its keys/pin/GMRC layer
is **lumalinux's**, and its tickets/cloud/stats layer is **LumaDeck's**. Unlike
OST, it targets **our platform and our binary**: 32-bit `steamclient.so`, GCC-built,
Linux.

### Out of scope, by decision

- **Coexistence.** Whether SteamFlipper's injection can run alongside ours is not
  investigated here. This document analyses how it works, not how it interacts
  with us.
- **Execution.** Static analysis only. Nothing was built, installed or run — see
  §0 "Limits", and treat every behavioural claim accordingly.

### Frozen references

Every claim below is against these exact revisions.

| Project | Ref | Date | Size |
|---|---|---|---|
| SteamFlipper | `e505aef` | 2026-09-02 | 192 files, 20.139 L C/C++ |
| OpenSteamTool | `2a08b0b` (`main` tip) | 2026-07-06 | 154 files, 15.602 L C/C++ |
| lumalinux | `4e0b7a8` | 2026-09-02 | 5.590 L C++ |
| LumaDeck | `67ff43f` | 2026-09-01 | 13.682 L Python, 5.920 L TS |

Both counterpart repositories were read from **full clones**: SteamFlipper's
entire history (6 commits, §1.1), and OpenSteamTool's `main` plus the tips of all
12 other public branches.

### Evidence rules

Every factual claim carries one of three tags:

- **[read]** — verified by reading the code at the cited `file:line`.
- **[measured]** — produced by a script over both trees; the method is stated
  where the number appears and in §9.1.
- **[their claim]** — asserted by their README or code comments and *not*
  independently verified.

### Method note on bias

This document is written by the author of a competing stack. Two counter-measures
apply, and the reader should hold the document to them:

1. **Axes and weights are fixed before the comparison sections are written** (§7,
   pending).
2. **Every axis where they win, or where the two are equal, is stated explicitly.**
   §2.6 already records one place where this fork does something our stack does
   not, and §2.4 corrects a claim in our own `opensteamtool-findings.md`.

### Limits

Nothing was executed. No binary was built, no Steam client was launched, no hook
was observed firing. Everything below is a reading of source code and of git
metadata. In particular, claims about what SteamFlipper *does at runtime* are
inferences from code, and its README's claims about behaviour are tagged
`[their claim]` and are not verified.

---

## §1 What SteamFlipper is

### §1.1 The history is one squashed commit

SteamFlipper's complete history is **6 commits, all dated 2026-09-02, all by
`IPedrax <pedragamesoficial@gmail.com>`** [measured]:

| Commit | Time | Change |
|---|---|---|
| `c3943c9` | 14:38 | *SteamFlipper: standalone Linux port of OpenSteamTool* — **193 files, 25.073 insertions, 0 deletions** |
| `654728d` | 14:56 | README: point clone instructions at the standalone repo (2 lines) |
| `b1991e0` | 14:58 | Drop upstream release workflow (−91 lines) |
| `4ba8237` | 15:24 | Detour: uninstall per handle and roll back abandoned transactions (+49/−23) |
| `e5f2e19` | 15:25 | Patterns: derive `GetPackageInfo` from `CheckAppOwnership`'s call site (+126/−14) |
| `e505aef` | 15:25 | Installer: remember the Millennium choice across reinstalls (+24/−3) |

The entire port — 25.073 lines including a from-scratch Linux platform backend,
a bootstrap injector, a pattern-derivation tool and three translated READMEs —
arrives as **one commit with no parents in this repository**, followed by five
small edits inside 47 minutes.

**What this does and does not tell us.** It does not tell us the work was done
in 47 minutes; a squash import of work developed elsewhere looks exactly like
this. What it does mean is that **there is no development history to read**: no
incremental commits, no bisectable sequence, no record of what was tried and
reverted. For a project whose core difficulty is deriving hook addresses against
a stripped binary, the reasoning behind each derivation is unavailable — the
opposite of what `RESEARCH.md` exists to preserve on our side. Every judgement
about *why* a given address or pattern was chosen has to come from the code and
its comments alone.

This is the same history shape recorded for SLSDeckUniversal in
`slsdeck-analysis.md` §1.2, and the same caveat applies: it constrains what can
be verified, not what can be true.

### §1.2 The fork base is OpenSteamTool `main`, and upstream is quiet

SteamFlipper's `src/**` files were compared line-for-line against the tip of every
public OpenSteamTool branch [measured]. `main` is the closest ancestor by a clear
margin:

| OST branch tip | Files paired | SF `src` lines present verbatim |
|---|---|---|
| **`origin/main`** (`2a08b0b`, 2026-07-06) | **113** | **87 %** (13.171 / 15.092) |
| `origin/feature/opensteamtool-stats-api` | 111 | 85 % |
| `origin/refactor/platform-pipe-denuvo` | 109 | 84 % |
| `origin/refactor/ipc` | 35 | 83 % |
| the other 8 branches | 31–33 | 68–78 % |

The fork carries upstream's most recent `main` work, including the Cloud RPC
vtable-hook change from `2a08b0b` [read: `src/Utils/CloudRedirect/CloudRedirectHost.cpp`].
So the base is `main` at or very near its current tip, not an older or divergent
branch.

Worth noting for later sections: **OpenSteamTool's public `main` has not moved
since 2026-07-06** — roughly two months before the fork, and three months before
this writing. Whatever maintenance cadence upstream had, the fork cannot inherit
it from a dormant branch.

Also worth noting: that tip commit is authored by **`Selectively11`** — the author
of CloudRedirect, which our own stack ships as a component. The Windows and Linux
sides of this ecosystem share more contributors than the repository boundaries
suggest.

### §1.3 What they say it is

> *"A standalone Linux port of OpenSteamTool. […] the hook addresses are
> re-derived against the GCC-built i386 `steamclient.so`, injection goes through a
> `libXtst.so.6` proxy that only the Steam client resolves, and the parts Linux
> does differently (signal-based traps, `dlopen` handles, depot decryption keys)
> are fixed rather than stubbed. This is a **standalone port**, not a patch set."*
> — `README.md` [their claim]

The "standalone port, not a patch set" half of that is **supported by measurement**
(§2.1): every one of OST's 154 tracked files has a counterpart here, and nothing
functional was pruned. The "Linux port" framing is accurate for what was *added*
but incomplete for what was *kept* — see §2.3 and §2.5.

---

## §2 Inventory and provenance

### §2.1 The measurement

Every SteamFlipper text file was paired with its OpenSteamTool counterpart
(applying the one directory rename, `src/OSTPlatform/` → `src/SFPlatform/`) and
compared after normalising line endings and trailing whitespace. The similarity
figure is *matching lines ÷ the longer file*, and the class thresholds were fixed
before the results were read. Method detail and caveats: §9.1.

| Class | Similarity | Files | Lines (SF side) | Share |
|---|---|---|---|---|
| **IDENTICAL** | ≥ 98 % | 47 | 5.057 | 21,0 % |
| **LIGHT** | 85–97 % | 51 | 9.504 | 39,5 % |
| **MODERATE** | 50–84 % | 28 | 3.520 | 14,6 % |
| **HEAVY** | 15–49 % | 8 | 863 | 3,6 % |
| **NEW** (no counterpart) | — | 38 | 5.140 | 21,3 % |
| **Total** | | **172** | **24.084** | |

Read at the top level: **60,5 % of the tree is upstream OpenSteamTool essentially
verbatim**, and 75,1 % is still recognisably upstream once MODERATE is included.
The 21,3 % that is new is analysed in §2.2.

Nothing functional was dropped. The only OST files with no counterpart here are
three CI workflows, `build.bat`, five logo assets, and two files that were renamed
rather than removed (`opensteamtool.example.toml`, `OpenSteamToolBuildInfo.h.in`)
[measured]. The consequence of the first item is recorded in §2.7.

### §2.2 What is actually new — and only two thirds of it is the port

The 5.140 new lines split into four groups [measured]:

| Group | Files | Lines | What it is |
|---|---|---|---|
| **A — Linux runtime** | 20 | 2.490 | `SFPlatform/Linux/*` (14 backends: `Detour`, `Trap`, `PE`, `Memory`, `Process`, `Http`, `ByteSearch`, `DirectoryWatch`, `DynamicLibrary`, `Hash`, `RemoteProcess`, `SteamCredentialStore`, `Thread`, `Dialog`), `Bootstrap/Linux/` (the `libXtst` proxy, 426 L), the `Funchook`/`Linux32` CMake modules |
| **B — Linux tooling** | 4 | 1.048 | `gen_linux_patterns.py` (462), `install_linux.sh` (376), `sync_depot_keys.py` (152), `build_linux.sh` (58) |
| **C — features absent from public OST** | 11 | 914 | `Utils/Tokeer/` (255), `Utils/Tickets/EticketClient` (293), `Utils/Tickets/LegacyCDKey` (73), `Utils/Update/AppUpdater` (223), `Utils/SteamMetadata/Mirror` (70) |
| **D — docs / config** | 3 | 688 | `WALKTHROUGH.md`, `STEAMFLIPPER_INTEGRATION.md`, `steamflipper.example.toml` |

Groups A and B — **3.538 lines, 14,7 % of the tree** — are the Linux port proper.
That is the honest size of the engineering claim in the README, and it is not a
small one: a hooking backend, a memory/PE/ELF layer, an injector and a pattern
deriver are exactly the hard parts.

One deduction from group A, made in §3.1: **426 of those lines are the injection
bootstrap, and it is Millennium's code (MIT), not the fork's** — properly
disclosed, but it means the headline mechanism of the README is adopted rather
than invented. Net original Linux work is closer to **3.112 lines, 12,9 %**.

Group C is a different matter, and §2.3 is about it.

### §2.3 Group C is Windows code of unexplained provenance

The 914 lines in group C are **not Linux port work and not present in any public
OpenSteamTool branch tip** [measured, across all 13 branches]. Their own comments
place them squarely on Windows:

- `Utils/Tokeer/TokeerBridge.h` — *"A public website opens `bst://redeem/<code>`;
  **Windows** launches the **OST DLL** as the registered handler (`rundll32` →
  `TokeerUri` export) […] Register the `bst://` URI scheme under **HKCU** (no
  admin)"* [read]. A `bst://` protocol handler registered in the Windows registry,
  in a repository whose selling point is that it is the Linux port.
- `Utils/Tickets/EticketClient.h` — an on-demand encrypted-app-ticket mint:
  *"we POST `{app_id, nonce}` to the **Tokeer backend**, which mints a FRESH ticket
  from an **owning pool account** with `userdata=nonce`"* [read].
- `Utils/Update/AppUpdater.h` — a self-updater that downloads and stages a
  replacement module, *"The current (loaded) DLL is renamed to `<name>.old`"* [read].
- `Utils/SteamMetadata/Mirror.h` — the delivery chain the updater reads from:
  *"the built-in mirror chain (github-raw → jsDelivr → **lua.tools**)"* [read].

Three things follow.

1. **These did not come from public OST and were not written for Linux.** The
   `rundll32`/HKCU/`.dll` vocabulary is not a porting artefact — it is code that
   was authored against Windows and carried in.
2. **The origin is partly resolved, and it is not this repository.** Reading the
   installer and the delivery code closes most of the gap [read]:
   - `tools/install_linux.sh` migrates `<Steam>/ubuntu12_32/opensteamtool/` →
     `steamflipper/` and `opensteamtool.toml` → `steamflipper.toml`, *"pre-rename
     data"*, and its proxy-detection regex is `SF_RUNTIME_PATH|BST_RUNTIME_PATH`
     where `BST_` is called *"the pre-rename marker"*. There was therefore a
     **deployed predecessor in the field under a different name** before this
     repository existed on 2026-09-02. `BST_` and the `bst://` scheme of
     `TokeerBridge` are the same three letters.
   - The update mirror chain does not point at this repository at all. It points
     at `raw.githubusercontent.com/**madoiscool**/SteamFlipper`,
     `cdn.jsdelivr.net/gh/**madoiscool**/SteamFlipper` and
     `git.lua.tools/luatools/SteamFlipper` [read: `Utils/SteamMetadata/Mirror.cpp:17-19`],
     with the pattern feed likewise on `madoiscool/steam-monitor` beside
     upstream's `OpenSteam001/steam-monitor`.

   So group C is prior work from a related project whose release infrastructure
   lives under a **different GitHub account and on LuaTools' own git host**, not
   code written for this port. What that relationship *is* — same author under
   two handles, a team, or a downstream of a downstream — is still not
   established, and this document does not guess. The `slsdeck-analysis.md` §5.7
   *Tokeer* activation path shares the name; the relationship remains unproven.
3. **Only part of group C is reachable on Linux**, which narrows the surface
   considerably and is stated here rather than left for §6 [read]:

   | Feature | Linux |
   |---|---|
   | `TokeerBridge::RegisterUriScheme` (`bst://` handler) | **dead** — call site is `#ifdef _WIN32` (`dllmain.cpp:134`) |
   | `AppUpdater` (self-replacing module) | **dead** — call site is `#ifdef _WIN32` (`dllmain.cpp:145-163`), disabled because no Linux artifact is published |
   | `EticketClient` (Tokeer ticket mint) | **live** — called unguarded from `Hooks_IPC_ISteamUser.cpp:125` and `Hooks_NetPacket.cpp:508`, but inert unless an endpoint is configured (`SF_ETICKET_URL` defaults to empty; `seteticketurl()` in the Lua config overrides) |
   | `LegacyCDKey::Resolve` | **live** — `Hooks_NetPacket.cpp:1224` |

Group C is still where the trust analysis will concentrate — a ticket mint backed
by "pool accounts" and a self-updater fed from a third-party mirror are among the
highest-consequence surfaces a tool like this can have — but the honest Linux
reading is that two of the four are compiled-in dead code and a third is off by
default. **§6 has not been written; nothing above is a risk assessment.**

### §2.4 This invalidates our own architecture-constraint ruling — for this fork

`opensteamtool-findings.md` closes its Context section with a ruling we have since
reused twice:

> *"any OST mechanism implemented as a network/IPC/message hook is **not portable
> to lumalinux** […] This rules out a large fraction of OST's cleverness for us."*

That ruling had two legs: OST is Windows/x64/MSVC, and OST is one in-process thing
that hooks the message layer where SLSsteam already lives. **SteamFlipper removes
the first leg entirely.** Its hook addresses are derived against the same
GCC-built i386 `steamclient.so` we hook, by a script we can run
(`tools/gen_linux_patterns.py`), against binaries whose hashes are already in our
`res/rvas/`.

The second leg still stands — we run vanilla SLSsteam and it owns
`CProtoBufMsgBase` — so "message-layer hooks collide with SLSsteam" remains true.
But "not portable because the platform is different" is now **false for anything
in this fork**, and `opensteamtool-findings.md` should carry a pointer to that
correction once this document has a verdict. That is a documentation fix, deferred
to §8 rather than made now.

### §2.5 The fork is not Linux-only

The Windows implementation is retained in full: `SFPlatform/Windows/` (14
backends, 2.914 L), the `dwmapi` and `xinput1_4` proxy DLLs, and a live `if(WIN32)`
branch in `src/SFPlatform/CMakeLists.txt` that still pulls Detours, `winhttp` and
`Bcrypt` [read]. The build is cross-platform; the *installer, tooling and
documentation* are Linux-only.

The rename is also incomplete in a way that is diagnostic rather than important:
`src/SFPlatform/CMakeLists.txt` still names its variables `OSTPLATFORM_COMMON_SOURCES`
and `OSTPLATFORM_PLATFORM_SOURCES` [read]. Small, harmless, and consistent with a
mechanical port of a tree the author did not write.

### §2.6 Where they do something we do not

Stated here rather than buried, per §0:

- **`tools/gen_linux_patterns.py` derives hook RVAs from a stripped
  `steamclient.so` with no disassembler.** It uses the VProf scope strings GCC
  leaves in the binary — a profiled function opens a scope naming itself, so the
  name string's only code reference lands inside that function — and takes the
  function's bounds from the `.eh_frame` FDE covering that reference. Output is
  keyed by the binary's SHA-256 [read: `tools/gen_linux_patterns.py:1-25`]. Our
  equivalent (`tools/derive_patterns.py` + the Ghidra workflow in `maintenance.md`
  §A.2) needs Ghidra and a human. If their technique holds, it is scriptable and
  CI-able where ours is neither. **This is the single most consequential thing in
  the repository for us**, and §5 is where it gets tested rather than admired.
- **No wrapper, no `LD_PRELOAD`, no `PATH` changes.** The module is installed as
  `<Steam>/ubuntu12_32/libXtst.so.6` and resolved by the client's own loader
  [read: `STEAMFLIPPER_INTEGRATION.md` §2]. Whatever its costs, it is a genuinely
  different answer to the injection problem than the three we have catalogued
  (our wrapper + `LD_PRELOAD`, SLSsteam's `LD_AUDIT`, moon's reverted `steam.sh`
  shim). §3 covers the mechanism.
- **A documented integration contract for third-party tools**
  (`STEAMFLIPPER_INTEGRATION.md`, 248 L): the paths, the file formats, and the
  explicit statement that *"It has no IPC/RPC surface. The only contract […] is
  files on disk plus when Steam is restarted"* [read]. We have nothing equivalent
  written down for tools that want to drive lumalinux.

### §2.7 No CI, and what that costs them

OpenSteamTool's three GitHub workflows (`ci.yml`, `main.yml`, `tools.yml`) were
dropped, one of them deliberately (`b1991e0`, *"Drop upstream release workflow"*).
What remains under `.github/` is five issue templates [measured]. **Nothing builds,
compiles or runs on push.**

The comparison is not flattering to them, and it is worth being precise about why
it matters here specifically rather than as a generic process complaint: this is a
project whose central artefact is a set of byte offsets into a binary that Valve
rewrites without warning. Our side runs `build.yml` (the full 32-bit build on every
push to `main`), `watch-steam.yml` (detects new `steamclient.so` builds) and
`verify-fix.yml`. A fork with a hand-run derivation script and no automation has to
notice a Steam update the way its users do.

Weighing that properly belongs to §5 and §7, not here.

---

## §3 The injection model

This is the axis the README leads with — *"no `LD_PRELOAD`, no wrapper script, and
no launcher to remember"* — and it is a genuinely different answer from the three
this project has already catalogued (our wrapper + `LD_PRELOAD`, SLSsteam's
`LD_AUDIT`, moon's reverted `steam.sh` shim, `slsteam-moon-findings.md` M7).

### §3.1 The proxy is Millennium's, and they say so

`src/Bootstrap/Linux/sf_bootstrap.c` (308 L) carries Project Millennium's MIT
header verbatim and is *"derived from Millennium's
`src/bootstrap/linux/libmillennium_bootstrap.c` […] with a `load_steamflipper()`
added next to its own core load"* [read: `src/Bootstrap/Linux/README.md`;
`MILLENNIUM_LICENSE` is shipped alongside]. The functions, the logging macros, the
`HOOK_FUNC` forwarding macro and the constructor's name
(`libmillennium_bootstrap_init`) are all upstream's.

The attribution is clean and the licence is shipped — this is a credit note, not a
complaint. But it means the mechanism the README presents as the port's defining
idea is **adopted from an existing Linux Steam mod**, and the fork's contribution
to it is one `dlopen` and the process gate around it. §2.2 is corrected
accordingly.

### §3.2 The load path, end to end

1. `tools/install_linux.sh` compiles the bootstrap `-m32` and installs it **over
   `<Steam>/ubuntu12_32/libXtst.so.6`**, backing the stock library up to
   `libXtst.so.6.sf-orig` exactly once — guarded by a marker regex
   (`SF_RUNTIME_PATH|BST_RUNTIME_PATH`) so it can never back up its own proxy over
   the real library, a mistake that *"once left a system with no working libXtst
   at all"* [read: `install_linux.sh:32-40, 211-223`].
2. Steam's 32-bit client links `libXtst.so.6` directly, so the loader maps the
   proxy at client start. Its constructor runs.
3. **Gate 1** — `is_steam_process()`: `realpath("/proc/self/exe")` must end in
   `/ubuntu12_32/steam` [read: `sf_bootstrap.c:130-155`]. A suffix match, chosen
   deliberately over the upstream `$HOME/.steam/steam/...` comparison, which broke
   silently on Flatpak, `~/.steam/root` layouts and relocated `STEAMROOT`.
4. `setup_hooks()` `dlopen`s the **real** library from
   `<parent>/steam-runtime/usr/lib/i386-linux-gnu/libXtst.so.6` with `RTLD_GLOBAL`,
   so the five re-exported XTest entry points forward to it.
5. `load_steamflipper()` `dlopen`s `~/.local/lib/steamflipper/32/SteamFlipper.so`
   with **`RTLD_LOCAL`** — deliberately, because the module statically links
   protobuf, lua and spdlog and *"putting that surface in the global scope would
   interpose on Steam's own copies"* [read]. `SF_DISABLE=1` skips it;
   `SF_RUNTIME_PATH` overrides the path.
6. **Gate 2** — the module's own constructor re-checks the host:
   `GetMainExecutablePath().filename() == "steam"` [read: `dllmain.cpp:170-183`].
7. All real work runs on a detached thread: load `steamclient.so` and `steamui.so`,
   read the pattern files, parse `config/stplug-in/*.lua`, start the file watchers,
   install the hooks, then optionally CloudRedirect.

Two gates for one decision is sound design, and the second one is the module's own
so it survives being loaded any other way.

### §3.3 Their case against `LD_PRELOAD` is correct, and it applies to us

Stated plainly because §0 requires it:

> *"`LD_PRELOAD` is inherited by every process Steam spawns, steamwebhelper, the
> runtime launcher, reaper, Proton and the games themselves. The module gates on
> the host process so it does nothing in those children, but it still gets mapped
> into every one of them, which is a needless anti-cheat risk inside a game."*
> — `src/Bootstrap/Linux/README.md` [read]

They are describing their own previous design, and they have no idea we exist
(SteamFlipper mentions SLSsteam, lumalinux, LumaDeck, Decky and `LD_AUDIT` exactly
zero times, §2). But the argument is **our model, verbatim**: our wrapper exports
`LD_PRELOAD` for lumalinux and CloudRedirect, and lumalinux's answer is exactly the
gate they describe — an allowlist on `/proc/self/comm` of `steam` and
`steamwebhelper`, returning *"immediately from the constructor with zero side
effects"* [read: `lumalinux/src/main.cpp:58-92`]. Identical reasoning, identical
residue: **the gate stops the work, not the mapping.**

Two things narrow it, and neither disposes of it:

- Both modules are 32-bit, so the loader skips them for 64-bit children — which is
  most modern games and most of Proton. The exposure is 32-bit children only.
- `LD_AUDIT` (SLSsteam) has the same inheritance property, so no stack that runs
  SLSsteam is free of this.

Their design removes the residue entirely, because nothing outside the client ever
resolves that library. **On this axis they are ahead of us**, and the honest
version of our position is "gated, and 32-bit only", not "not an issue".

### §3.4 What it costs them: the injection point is a Valve-owned file

The proxy lives **inside Steam's own install tree**, replacing a library Valve
ships. Their installer states the consequence in its own comments: *"A Steam update
overwrites `ubuntu12_64/libXtst.so.6`"*, and the uninstall path warns that after
removing the proxy with no backup present, *"Steam will restore it on next update,
or verify files"* [read: `install_linux.sh:43-44, 97-101`]. The pattern files have
the same property by design: *"regenerated here because a Steam update invalidates
them"* [read: `install_linux.sh:19-20`].

So a Steam client update can take out **both halves at once** — the injection point
and the hook addresses — and the recovery is *the user re-running the installer by
hand*. There is no guardian, no re-affirm, no crash-loop fail-safe; the state file
(`~/.local/lib/steamflipper/install-state`) exists only so a manual re-run
remembers the Millennium choice [read].

This is the trade our stack made in the opposite direction, and it is worth being
precise rather than triumphant about it, because our reasons are on record. We
moved *off* editing files Steam owns — `steam.sh` and `/usr/bin/steam` stay vanilla
— to a wrapper reached by patched `.desktop` files, a PATH drop-in and a systemd
drop-in, with a `--user` guardian that re-affirms coverage after a self-update
(`RESEARCH.md` §5, `README.md` "After a Steam update"). moon tried the in-tree
shim and reverted it (`slsteam-moon-findings.md` M7). SteamFlipper has walked into
the same slot from a different direction: not `steam.sh`, but `libXtst.so.6` —
different file, same landlord.

The slot is also **contended**: Millennium's own installer writes its bootstrap to
that exact path, so *"Reinstalling Millennium overwrites `ubuntu12_32/libXtst.so.6`
with its own bootstrap, so re-run the install step afterwards"* [read]. Two mods,
one file, last writer wins.

Whether that trade is worth it is a §5/§7 question. What is settled here is that
"no wrapper, no launcher to remember" is not free: it is paid for with durability
against the exact event this whole class of tool has to survive.

### §3.5 Five things reading the bootstrap turned up

None is fatal; together they are a fair sample of the code's maturity.

1. **The Millennium co-load path is unreachable.** `libmillennium_bootstrap_init()`
   ends with `load_steamflipper(); return;` and then continues for another 25 lines
   — `b_has_loaded_millennium = 1`, `load_and_start_millennium()`, the `atexit`
   registration and a second `load_steamflipper()` — all after the `return` [read:
   `sf_bootstrap.c:296-323`]. Since `b_has_loaded_millennium` can therefore never
   be set, the destructor's `stop_and_unload_millennium()` is dead too, as are
   `load_and_start_millennium`, `stop_and_unload_millennium` and
   `proxy_at_exit_handler`. Inert, but it is unreachable code left in a file that
   gets compiled every install.
2. **Two comments in that function contradict each other.** One block says
   *"SteamFlipper-only by default; Millennium is opt-in via `MILLENNIUM_ENABLE=1`"*;
   the next says *"`MILLENNIUM_ENABLE` is retired"* [read]. The code implements the
   second.
3. **A claimed property the code does not have.** A comment asserts XTest
   forwarding is *"established above, before this branch, so it is intact in either
   mode"* — but `setup_hooks()` is called *after* the `is_steam_process()` early
   return, so any process that resolves this library and is not the 32-bit client
   leaves `h_xtst` NULL, and every `HOOK_FUNC` stub then returns 0 [read:
   `sf_bootstrap.c:157-168` vs `shared.h:38-51`]. `XTestQueryExtension` returning 0
   reports the extension as unavailable. The blast radius is small — only 32-bit
   binaries resolving `ubuntu12_32/libXtst.so.6` — but the comment describes a
   safety property that is not there, and the same comment records that this exact
   bug (the call sitting inside a branch that the default path did not take) had
   already killed Big Picture's virtual keyboard once.
4. **The Steam root is `getcwd()`, not the executable's directory.**
   `InitializeSteamComponents()` derives `steamclient.so`, `steamui.so`,
   `config/stplug-in` and `steamflipper.toml` from
   `DynamicLibrary::GetCurrentDirectoryPath()`, which on Linux is a plain `getcwd()`
   [read: `dllmain.cpp:50-53`; `SFPlatform/Linux/DynamicLibrary.cpp:72-78`]. Their
   own integration guide tells third-party tools the opposite: *"Paths are derived
   from the **32-bit client executable's directory**"* [their claim:
   `STEAMFLIPPER_INTEGRATION.md` §2]. It works in practice because Steam's launcher
   `chdir`s to the Steam root, and `GetMainExecutablePath()` — which would be
   correct — exists in the same file and is used for the process gate two functions
   away.
5. **`steamui.so` is a hard dependency that is deliberately never hooked.**
   `InitializeSteamComponents()` returns `false` — aborting all initialisation — if
   `steamui.so` fails to load [read: `dllmain.cpp:84-89`], while the installer
   states *"`steamui.so` is intentionally not generated: hooking it segfaults the
   client on startup, reproduced across several address sets including
   independently verified ones"* [read: `install_linux.sh:298-300`]. The module is
   required to load, forbidden to hook.

### §3.6 What we would take from this: the diagnosis, not the mechanism

Nothing here is portable to us, and not because of the architecture constraint
(§2.4) — because the mechanism is one we deliberately walked away from. Putting
lumalinux's injection point inside `ubuntu12_32/` would trade a durability property
we spent releases building for a purity property we do not need.

What does survive the comparison is **§3.3's diagnosis**. Our `README.md` and
`RESEARCH.md` §5 present `LD_PRELOAD` as settled — correct against `LD_AUDIT`, which
was the alternative under consideration — and never state the cost they name: the
`.so` is mapped into every 32-bit child Steam spawns, gate or no gate. That is a
true statement about our stack that our own documentation does not make. Whether it
warrants any action beyond writing it down is a §7 question; writing it down is
already worth the section.

## §4 Gate-by-gate

The spine is `method.md` §1. The finding that governs the whole section is that
**most of SteamFlipper's hooks do not resolve on Linux**, so the code you read is
not the code that runs.

### §4.1 Twenty-eight names asked for, eight answered

The hooks request 28 distinct function names from `PatternLoader::FindPattern`
across `steamclient.so` and `steamui.so` [measured, from the `RESOLVE_*` /
`INSTALL_HOOK_*` / `ARM_CAPTURE_*` / `CAPTURE_THIS_FUNC` call sites]. The Linux
address generator can produce **nine** [read: `tools/gen_linux_patterns.py`,
`WANTED` + `VERIFIED` + `REQUIRED`], and they are split two ways:

- **Three by VProf scope, on any build** — `CheckAppOwnership`,
  `BuildDepotDependency`, `GetOrAddAppData`. GCC leaves a self-naming profiling
  scope in the binary; the string's only code reference lands inside the function,
  and the `.eh_frame` FDE gives its bounds (§5 examines the technique itself).
- **Six pinned to one exact SHA-256** — `GetPackageInfo`, `CUtlMemoryGrow`,
  `MarkLicenseAsChanged`, `ProcessPendingLicenseUpdates`,
  `CPackageInfoCacheGlobal` (steamclient `bc54101b…`) and
  `CSteamUIAppControllerRunFrame` (steamui). *"They CANNOT be re-derived by the
  generic path below […] If the hash does not match, these are skipped with a
  warning rather than guessed"* [read].

Of the nine, the four `steamui.so` entries are refused unless `--allow-steamui` is
passed, because hooking them *"segfaults the client on startup, reproduced across
several address sets"* [read]. `GetOrAddAppData` resolves but its
`INSTALL_HOOK_C` is commented out [read: `Hooks_Misc.cpp`]. **Seven addresses are
doing all the work.**

Their own `WALKTHROUGH.md` states this plainly under "Known limits", and the count
matches ours exactly — *"Nine functions are located by RVA. Only three are
recoverable automatically […] the other six […] are pinned to the SHA-256 of a
specific Steam build"* [their claim, and [measured] agrees]. Credit where it is
due: this is disclosed, not hidden, and the generator *"fails safe: given a binary
it does not recognise it emits nothing rather than a stale address"*.

One mechanism detail worth recording, because it has a cost. The locally generated
file is written to `<Steam>/ubuntu12_32/steamflipper/pattern/<component>/<sha>.toml`
— which is exactly `RemoteToml`'s *cache* path, so the generator masquerades as a
previously-downloaded remote file [read: `gen_linux_patterns.py --install` vs
`RemoteToml.cpp:100-103`]. Neat, but `RemoteToml::Fetch` tries the **network
first** and only falls back to the cache: five mirrors
(`OpenSteam001/steam-monitor`, `madoiscool/steam-monitor` and
`git.lua.tools`, over raw.githubusercontent and jsDelivr), each keyed by the
SHA-256 of a *Linux* `.so` that those Windows-oriented feeds cannot contain. So
every Steam launch makes five outbound requests that must 404 before the local
file is read, twice over (steamclient and steamui). Functionally harmless,
gratuitous as network behaviour; §6 picks up the surface.

What does **not** resolve is everything else: the entire message layer
(`BBuildAndAsyncSendFrame`, `RecvPkt`, `PchMsgNameFromEMsg`), the entire IPC layer
(`IPCProcessMessage`, `GetPipeClient`), the depot-key hook
(`ConfigStoreGetBinary`), and the misc/spawn hooks. `Hooks_NetPacket.cpp` is the
single largest file in the repository at 1.565 lines, and on Linux **none of it
executes**.

### §4.2 The six gates

| # | Gate | Their mechanism | Live on Linux? | Ours |
|---|---|---|---|---|
| 1 | Ownership | package-0 `AppIdVec` injection (token `10660652434190618804`) + `CheckAppOwnership` forcing `PackageId=0`, `bOwnsLicense=true` | **yes** — 1 VProf + 4 pinned | SLSsteam |
| 2 | PICS appinfo | outbound `CMsgClientPICSProductInfoRequest` (8903) `access_token` injection, from `addtoken()` | **no** — NetPacket unresolved; `addtoken()` is parsed and stored but nothing reads it [read: only consumer is `Hooks_NetPacket.cpp`] | SLSsteam (PICS tokens) |
| 3 | Depot surfacing | same package-0 injection: every depot id from the Lua goes into the fake licence's `AppIdVec` | **yes** — same pinned set | lumalinux package-0 finder |
| 4 | Manifest pinning | `BuildDepotDependency` post-hook rewriting each `DepotEntry`'s `ManifestGid`/`ManifestSize` from `setManifestid()` | **yes** — VProf | SLSsteam `ManifestIds` (our BuildDep hook is off by default) |
| 5 | Depot key | `ConfigStore::GetBinary` hook answering `…\<DepotId>\DecryptionKey` from the Lua | **no** — unresolved; compensated **out of process** by `tools/sync_depot_keys.py` writing `config.vdf` with Steam closed | lumalinux DepotKey hook **and** `steamidra_lite` writing `config.vdf` |
| 6 | Manifest request code | intercept the outbound `ContentServerDirectory.GetManifestRequestCode#1` job (EMsg 151), fetch the code from the same three providers we use, inject it into the response (147) | **no** — NetPacket unresolved | lumalinux GMRC hook + provider cascade + `gmrc_store` |

Four of six by hook; gate 5 relocated to a file; **gate 6 not open at all**.

### §4.3 Gate 6 is the one that matters, and they know it

`method.md` §1 is explicit that gate 6 is the only gate that cannot be faked
locally — the request code is validated server-side by Valve. SteamFlipper has a
complete, working implementation of it (`ManifestClient` + the NetPacket
interception, same three providers: `opensteamtool` → `wudrm` → `steamrun`), and
on Linux it is unreachable code.

What that leaves is a **bring-your-own-manifest** design, and it is exactly what
their documentation instructs: the user drops `.manifest` files into
`<Steam>/depotcache/` themselves, and `STEAMFLIPPER_INTEGRATION.md` lists that
directory as written by *"your app"*, not by SteamFlipper. With the manifest
already cached, the client does not need to fetch it, so gate 6 never comes up for
the initial install.

The consequence worth flagging is what happens **after** the initial install
[inferred, and the clearest testable prediction in this document]: a game that
updates gets a new manifest GID that is not in `depotcache`, so the client must
request a code for it. Ours serves that request; theirs cannot. So on Linux a
SteamFlipper install should be expected to work once and then not update itself —
unless the user re-seeds `depotcache` by hand for every new build. Nothing was
executed, so this is an inference from the code path, not an observation. It is
the first thing I would test if we ever move to §0's excluded execution track.

### §4.4 Function layer vs message layer — and why the choice bit them here

`opensteamtool-findings.md` recorded that OST hooks freely at the message/wire/IPC
layer while lumalinux must use function-layer seams, because SLSsteam already owns
`CProtoBufMsgBase` on our side. We framed that as *our* constraint — a limitation
imposed by not forking SLSsteam.

This port inverts the reading. On Windows the message layer is cheap because the
`steam-monitor` pattern database publishes addresses for `steamclient64.dll`
keyed by hash, so `BBuildAndAsyncSendFrame` and `IPCProcessMessage` are just
lookups. On Linux there is no such database, the binary is stripped, and the
message-layer entry points carry **no VProf scope** — so the whole layer is
unreachable, and with it gates 2 and 6.

The function-layer seams, meanwhile, survived the port: `CheckAppOwnership` and
`BuildDepotDependency` are VProf-scoped and come out of a stripped binary
automatically. **Our constraint turned out to be the portable choice**, though it
is worth being honest that this is a fact about GCC's profiling instrumentation
rather than foresight on our part.

### §4.5 Same binary, independent agreement

Their six pinned addresses are keyed to steamclient `bc54101b…` — **the same build
lumalinux whitelisted in `4e0b7a8`**, for which we ship
`res/rvas/bc54101b….yaml`. Two of their numbers can therefore be checked against
ours directly, and both agree [measured]:

| Value | SteamFlipper | lumalinux `res/rvas/bc54101b….yaml` |
|---|---|---|
| `ProcessPendingLicenseUpdates` | `0x188C950` | `Reconcile: 0x188c950` |
| `CPackageInfoCache` owner GOT displacement | `0x3b7d4` (from their decoded call site `lea eax,[GOT+0x3b7d4]`) | `finder.cache_global_disp: 0x3b7d4` |

Two independent derivations, two different methods, same addresses. That is the
strongest confirmation either project has that its numbers are right, and it is
worth recording in both directions — our licence-reconcile hook and their fake
licence refresh call the *same function*, and our package-0 finder walks from the
*same global* they pin.

It also sharpens §5: their pinned six are what our RVA feed produces automatically
per build. They pinned to one hash what we regenerate.

### §4.6 The seam they could not find is one we resolve by a different technique

`ConfigStoreGetBinary` — the depot-key seam — is the one they gave up on:
*"`ConfigStoreGetBinary` is unresolved, so decryption goes through `config.vdf`
instead of the hook"* [their claim, and consistent with the generator]. They add
that the file route *"is the more robust route anyway: it needs no signature and
survives client updates"*, which is fair.

We resolve it. `res/rvas/bc54101b….yaml` carries
`depotkey_rtti: {class: "12CConfigStore", slot: 6, rva: "0x11a4500"}` — the
DepotKey hook located by **RTTI vtable slot** (`RESEARCH.md` §15), on the same
class they could not locate. SteamFlipper contains no RTTI-based resolution at all
[measured: no `typeinfo`/RTTI machinery anywhere in the tree].

The two techniques are **complementary, not competing**, and that is the useful
conclusion:

- **VProf scopes** recover functions Valve chose to profile — including
  `CheckAppOwnership` and `BuildDepotDependency`, which we do *not* currently
  derive automatically.
- **RTTI vtable slots** recover virtual functions of classes carrying type info —
  including `CConfigStore::GetBinary`, which they cannot derive at all.

Neither reaches the message layer, which is why gate 6 is closed for them and open
for us only because we hook `BYieldingGetManifestRequestCode` as a *function*
(`GMRC: 0x1371ac0` in the same feed).

§5 takes this further: whether their VProf technique, run against binaries we
already have, recovers anchors our own workflow needs a human and Ghidra for.

## §5 Surviving a Steam client update — *pending*

Planned: `gen_linux_patterns.py` (VProf + `.eh_frame`) against our RVA feed and
`derive_patterns.py`, including running their script over the `steamclient.so`
hashes already recorded in `res/rvas/` to see whether it recovers our anchors.
§3.4 is the other half of this axis and feeds into it.

## §6 Trust and risk — *pending*

Planned: group C (§2.3) — the Tokeer ticket mint, the `madoiscool`/`lua.tools`
delivery chain, the `bst://` handler — plus the full network surface, what is
written to `config.vdf`, and privilege hygiene.

## §7 Verdict and adversarial pass — *pending*

## §8 Findings list — *pending*

Accumulating as the sections land. Two are already fixed, and neither is a code
change:

- **F1 — documentation, `opensteamtool-findings.md`.** Its architecture-constraint
  ruling loses its platform leg for this fork (§2.4). The message-layer leg still
  holds. Wording to be proposed once §4 has read their hooks.
- **F2 — documentation, `RESEARCH.md` §5 / `README.md`.** Our `LD_PRELOAD` write-up
  argues the choice against `LD_AUDIT` and never states its cost: the `.so` is
  mapped into every 32-bit child Steam spawns, and the `/proc/self/comm` allowlist
  gates the work, not the mapping (§3.3). Worth one honest paragraph.
- **F3 — candidate, `tools/`.** VProf-scope derivation and our RTTI slot
  derivation recover disjoint sets of functions (§4.6). VProf reaches
  `CheckAppOwnership` and `BuildDepotDependency`, which we do not derive
  automatically today. Whether it is worth adding as a second automatic path is
  §5's question, and it is evidence-gated: do not implement before §5 runs their
  generator against binaries we hold.
- **F4 — cross-check, `res/rvas/`.** Their pinned `ProcessPendingLicenseUpdates`
  and cache-owner displacement match ours exactly on the same build (§4.5). Worth
  a line in `rva-feed-design.md` recording that an independent derivation agrees;
  it is the only external confirmation our feed has.

---

## §9 Appendices

### §9.1 Method — how the provenance numbers were produced

Both repositories were cloned in full. Each SteamFlipper text file
(`*.cpp *.h *.c *.py *.sh *.toml *.txt *.cmake *.md *.def *.in *.proto *.steamd`,
172 files) was paired with the OpenSteamTool file at the same path after applying
the single directory rename `src/SFPlatform/` → `src/OSTPlatform/`. Both sides were
normalised (CR stripped, trailing whitespace removed) before diffing; without that
normalisation the numbers are badly wrong, because upstream commits CRLF for some
files and the fork's `.gitattributes` forces LF.

Similarity is *unchanged lines under `diff` ÷ `max(lines_SF, lines_OST)`*. Two
properties of that metric matter when reading §2.1:

- It is a **lower bound** on real similarity. Code that was moved or reindented
  scores as changed, so a file can be more upstream than its class suggests.
- Very small files produce unstable percentages; the 8 HEAVY files total 863 lines
  and several are headers under 20 lines.

Class thresholds (98 / 85 / 50 / 15) were fixed before results were inspected.

The branch comparison in §1.2 uses the same normalisation over `src/**` only,
against each branch tip fetched at depth 1 — so it compares **tip trees**, not
branch histories. A file that existed on a branch and was later deleted would not
be seen. This is the reason §2.3 says the group-C origin is *unresolved* rather
than *absent from upstream*.

### §9.2 References

- SteamFlipper (`IPedrax/SteamFlipper` @ `e505aef`): `README.md`,
  `STEAMFLIPPER_INTEGRATION.md`, `WALKTHROUGH.md`;
  `src/dllmain.cpp`; `src/Bootstrap/Linux/{sf_bootstrap.c,shared.h,README.md,MILLENNIUM_LICENSE}`;
  `src/SFPlatform/{CMakeLists.txt,Linux/DynamicLibrary.cpp,Linux/*}`;
  `src/Hook/*` (all nine hook units, notably `Hooks_NetPacket.cpp`,
  `Hooks_Package.cpp`, `Hooks_Manifest.cpp`, `Hooks_Decryption.cpp`,
  `HookManager.cpp`);
  `src/Utils/SteamMetadata/{PatternLoader,RemoteToml,IPCLoader,ManifestClient}.cpp`;
  `src/Utils/Config/LuaConfig.cpp`;
  `src/Utils/{Tokeer,Tickets,Update,SteamMetadata}/*` (notably `Mirror.cpp`,
  `EticketClient.h`, `TokeerBridge.h`, `AppUpdater.h`);
  `tools/{gen_linux_patterns.py,install_linux.sh,sync_depot_keys.py}`;
  `steamflipper.example.toml`.
- Millennium (`SteamClientHomebrew/Millennium`): `src/bootstrap/linux/libmillennium_bootstrap.c`,
  the origin of §3.1's proxy — read here only through the copy SteamFlipper ships
  and its MIT header.
- OpenSteamTool (`OpenSteam001/OpenSteamTool` @ `2a08b0b` and 12 branch tips):
  `src/OSTPlatform/Linux/.gitkeep` (the placeholder confirming upstream never
  implemented a Linux backend), and the counterparts of every file above.
- Ours: `docs/opensteamtool-findings.md` (the ruling corrected in §2.4),
  `docs/method.md` (the six gates, §3's spine), `docs/maintenance.md` §A.2
  (the derivation workflow §5 will compare against), `docs/RESEARCH.md`,
  `docs/slsdeck-analysis.md` (the template this document follows),
  `res/rvas/bc54101b….yaml` (the shared-build cross-check in §4.5–§4.6),
  `tools/derive_patterns.py`, `.github/workflows/watch-steam.yml`,
  `docs/rva-feed-design.md`, `RESEARCH.md` §15 (RTTI resolution).
