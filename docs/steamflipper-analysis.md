# SteamFlipper vs the LumaDeck stack — exhaustive analysis

*Complete. Provenance and inventory (§1–§2), the injection model (§3), the
gate-by-gate comparison (§4), surviving a Steam update (§5), trust and risk (§6),
and the verdict (§7). **§7.5 is the adversarial pass** over the conclusions that
favour our own stack: it revised or narrowed four of them, and §7.1 records that
§0's pre-registration of the axes was not honoured. Nothing here is a
recommendation to change LumaDeck or lumalinux — §8 is a findings list and all
five entries are documentation or evidence-gated. **Read §0 first**: nothing was
executed, coexistence is out of scope by decision, and the evidence tags are
load-bearing.*

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

1. **Axes and weights are fixed before the comparison sections are written.**
   ⚠️ **This was not honoured — see §7.1**, which says so plainly and states what
   was done instead.
2. **Every axis where they win, or where the two are equal, is stated explicitly.**
   Honoured: §2.6, §3.3, §5.6, §6.5 and §6.6 each record a place where they lead or
   where our own documentation was wrong, and §7.2 loses three of ten axes.

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

> **Narrowed in §7.5.4** — our advantage is on the *coverage* half. A moved
> pattern still needs a human on both sides, so the gap is not immunity.

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

#### §4.2.1 Gate 5's relocation lands on a dead end we already measured

Their gate-5 fallback — `tools/sync_depot_keys.py` writing `DecryptionKey`
entries into `config/config.vdf` with Steam closed — is a route this project
tried, measured and rejected. `RESEARCH.md` §6, in the dead-ends list:

> *"**config.vdf depot keys get pruned.** Writing `DecryptionKey` into
> `config.vdf` works for owned depots, but Steam **deletes** the entries for
> unowned depots on shutdown (`grep -c DecryptionKey` dropped from 3 to 1 across
> a restart). → keys must be served at runtime (DepotKey hook), not via config."*

If that behaviour reproduces under their stack, their only Linux depot-key path
**erases itself on every Steam shutdown**, and the user has to re-run the script
before each session in which a new depot is involved. Their documentation does
not mention it: the README warns that Steam *"rewrites config.vdf on exit and
would discard edits made [while it is running]"* — which is about editing
underneath a live client, not about losing entries already written [read].

**Stated as a prediction, with its uncertainty.** Our measurement was taken with
our own stack, and the prune plausibly keys on whether the client considers the
depot owned at shutdown. SteamFlipper also fakes ownership (gate 1 is live for
them), so it is entirely possible Steam treats those depots as owned and keeps
the entries. Nothing here settles that — §0 excluded execution.

Two things make it worth recording anyway. It is one of the few claims in this
document that rests on **our own measurement** rather than on reading their code;
and it is cheap to falsify — write keys, restart Steam, `grep -c DecryptionKey`.
It joins §4.3's pinning prediction as the second item on the list for the day
execution is back in scope.

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

> **Revised in §7.5.1** — "installs cannot update" overstates it. `setManifestid()`
> is a supported workflow, so the accurate statement is that installs are
> effectively *pinned*, and updating means re-seeding `depotcache` by hand.

### §4.4 Function layer vs message layer — and why the choice bit them here

`opensteamtool-findings.md` recorded that OST hooks freely at the message/wire/IPC
layer while lumalinux must use function-layer seams, because SLSsteam already owns
`CProtoBufMsgBase` on our side. We framed that as *our* constraint — a limitation
imposed by not forking SLSsteam.

This port complicates the reading, and the first draft of this section got the
conclusion wrong. It claimed the message layer is "unreachable on Linux". **It is
not**, and our own documentation says so:

- SLSsteam hooks `CProtoBufMsgBase::Send` and `::InitFromPacket` on this exact
  i386 binary, by **detour on a byte-pattern-scanned address** (`DetourHook`,
  `hooks.cpp:203`) [read: `slssteam-analysis.md` §1.5, §2 inventory].
- slsteam-moon builds on that to hook the depot-key **protobuf message**
  (`CMsgClientGetDepotDecryptionKey`), which is why `slsteam-moon-findings.md`
  Finding 3 records the approach as *more* update-resilient than ours — the wire
  format barely changes.

So on Linux the message layer is reachable, in production, today. The correct
statement is narrower and about them, not about the platform:

> **SteamFlipper cannot reach the message layer because neither of its two
> derivation techniques gets there.** VProf scopes do not cover
> `BBuildAndAsyncSendFrame`, `RecvPkt` or `IPCProcessMessage`, and nobody has
> hand-pinned them. A byte pattern would reach them — SLSsteam proves it — but
> they have no Linux byte-pattern source: `steam-monitor` publishes Windows DLL
> hashes only (§4.1), and their generator emits a `sig` solely for the functions
> VProf already found.

That leaves three projects on one binary, and the three-way split is the actual
finding:

| | Message layer | Why |
|---|---|---|
| **SLSsteam / moon** | **uses it** | byte-pattern detour on the protobuf dispatch |
| **lumalinux** | **refuses it** | SLSsteam already owns that seam; double-wrapping it is a coexistence hazard (`slsteam-moon-findings.md` Finding 3: *"do not move lumalinux's DepotKey to the message layer while we run alongside vanilla SLSsteam"*) |
| **SteamFlipper** | **wants it, cannot find it** | tooling gap, not platform |

Our position is therefore unchanged and unvindicated: we avoid the message layer
for the reason we always did — coexistence — not because it is unavailable. The
function-layer seams did survive the port, and `CheckAppOwnership` and
`BuildDepotDependency` do come out of a stripped binary automatically, but
**"our constraint turned out to be the portable choice" was self-flattery**: the
portable choice on Linux is a byte pattern, which is what SLSsteam uses on the
seam we declined and what we use on the seams we took.

What this *does* say about them is sharper than the wrong version was. Their two
gates are not closed by anything about Linux. They are closed because a
derivation toolchain limited to VProf scopes plus hand-pinned addresses cannot
reach functions that carry neither — and the fix is a technique that has existed
in this ecosystem all along.

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

## §5 Surviving a Steam client update

The decisive axis for anything that hooks `steamclient.so`. §3.4 covered the
injection half (their proxy sits in a file Valve overwrites); this is the address
half.

**The empirical test proposed for this section was not run.** Running their
generator against binaries we hold falls under §0's excluded execution track.
§5.9 states exactly what to run if that is ever reopened, and what it would
settle. Everything below is from source.

### §5.1 Both projects copied the same idea, in opposite directions

Worth stating before any comparison, because it makes the section awkward to
write in our favour: `docs/rva-feed-design.md` opens by naming its prior art —
*"Modeled on OpenSteamTool's `PatternLoader` (RVA-first, sig fallback) and
OpenSteam001/steam-monitor's per-DLL-hash TOML feed"* [read]. Our RVA feed **is**
an adaptation of the mechanism SteamFlipper inherited by descent.

So this is not our design against theirs. It is the same design — *precompute the
RVA once per build, key it by the binary's SHA-256, resolve RVA-first* — reaching
Linux twice: they ported the consumer and wrote a Linux producer; we ported the
concept into our CI (`check_patterns.py --emit-rvas` → `res/rvas/`) and added the
Linux-specific runtime translation. The differences below are in the producer and
the delivery, not the idea.

### §5.2 What their generator does on a build it has never seen

Better than §4.1 suggests in isolation, and the last two commits in the repo are
both about exactly this [read: `tools/gen_linux_patterns.py`]:

1. **Three by VProf scope, on any build.** `CheckAppOwnership`,
   `BuildDepotDependency`, `GetOrAddAppData`.
2. **Three more by structural derivation, on any build.** `derive_from_call_site`
   reads `CheckAppOwnership`'s own body for the byte sequence
   `add eax,imm32 ; push eax ; call rel32`, which yields the `CPackageInfoCache`
   subobject offset and `GetPackageInfo`'s address, then walks backwards through
   the frame slot to the `lea reg,[GOT+disp32]` that produced the owner global.
   *"Both addresses and the subobject offset fall out of those bytes, so they do
   not need pinning to a SHA and survive a Steam update"* [read]. The call target
   is rejected unless it is a real FDE start — it refuses rather than guesses.

That is careful work and it deserves saying plainly: deriving a private global and
a subobject offset out of a call site, from a stripped binary, with an FDE
sanity-check, is the same class of thing `RESEARCH.md` §13 documents on our side.

3. **Three still unresolved on a new build**: `CUtlMemoryGrow`,
   `MarkLicenseAsChanged`, `ProcessPendingLicenseUpdates`.

### §5.3 One function decides whether their tool works at all

Of those three, one is load-bearing, and they say so in the code:

> *"`CUtlMemoryGrow` is load-bearing: without it the injected package cannot grow
> its app list, so ownership silently does nothing. […] Installing without it
> would give a hook set that looks healthy and unlocks nothing. Refusing."*
> [read: `gen_linux_patterns.py`]

The generator exits non-zero and the installer stops, unless `--partial` is
passed. **On any Steam build other than the one pinned, SteamFlipper does not
install** — deliberately, and with a correct diagnosis. Refusing to ship a hook
set that looks healthy and unlocks nothing is the right call; we made the opposite
one for a while and `main.cpp` still carries the comment about it (per-hook
`FAILED` status instead of a hard abort), which works for us only because our
failure surfaces in a toast and `status.json` that LumaDeck reads.

The asymmetry worth recording: **we do not need `CUtlMemoryGrow` at all.** Their
ownership path grows Steam's `AppIdVec` by calling Steam's own vector-growth
helper; our package-0 finder walks the cache and seeds the per-depot licence
filter from a worker thread instead (`RESEARCH.md` §13.4), and never calls a
growth function [read: `src/hooks/package_zero_finder.cpp`]. Their single blocking
dependency is one our design has no use for. That is not cleverness on our part —
it is a consequence of having solved gate 3 by walking rather than by injecting —
but on this axis it is the difference between "needs a human before it works on a
new build" and "does not".

### §5.4 There is no delivery channel for Linux

This is where the gap stops being about technique.

Their six pinned addresses live in a **Python dict inside the repository**
(`VERIFIED`, keyed by SHA-256) [read]. So the recovery path after a Steam update
that moves them is: someone re-derives the addresses by hand (*"each independently
re-derived by a second analyst before being written down"*), commits them to the
repo, and every user pulls, re-runs `install_linux.sh`, and restarts Steam. There
is no release artifact for Linux, no feed, and the self-updater is compiled out on
this platform (§2.3). The remote pattern mirrors they *do* consult
(`steam-monitor`, on both `OpenSteam001` and `madoiscool`) publish Windows DLL
hashes, so on Linux those five requests always 404 (§4.1).

Ours ships the addresses as data. `res/rvas/<sha256>.yaml` is fetched at runtime
over the same path as `res/updates.yaml`, with a `~/.cache/lumalinux` fallback for
offline, so **merging a PR propagates to every Deck on next boot with no release
and no user action** [read: `docs/rva-feed-design.md`, `src/rva_feed.cpp`,
`docs/maintenance.md` §A.1]. And that PR is opened by a bot: `watch-steam.yml`
runs daily, detects a new Steam client, fetches its 32-bit `steamclient.so`, and
branches on `check_patterns.py`'s exit code — clean → open the hash-bump PR;
ShaderDepot moved → PR plus an issue; a critical hook or finder anchor moved →
issue, and explicitly *do not whitelist* [read].

They have no CI at all (§2.7). The comparison on this axis is not close, and it is
the axis that decides how long a tool stays broken after a Tuesday Steam update.

### §5.5 Three resolution mechanisms against one and a half

| | lumalinux | SteamFlipper |
|---|---|---|
| Per-build RVA, hash-keyed | `res/rvas/*.yaml`, fetched at runtime, CI-generated | pattern TOML, generated locally at install |
| Byte patterns | **compiled into the binary** (`src/patterns.hpp`), scan any build | `sig` field — but see below |
| RTTI vtable slot | `src/rtti.cpp`, `depotkey_rtti` in the feed | none [measured: no RTTI machinery in the tree] |
| Xref rescue | GMRC anchor cross-check, rescues a pattern miss and logs drift | none |

**Their `sig` fallback cannot survive an update, by construction.** The generator
does emit a byte signature next to each RVA, wildcarding PIC thunk calls and
GOT-delta immediates — genuinely thoughtful. But the file it goes into is named
`<sha256>.toml` and `PatternLoader` only ever loads the file matching the *current*
binary's hash. On a new build there is no file, so there is no sig to fall back to;
on the build it was generated from, the RVA already resolves. The fallback is
reachable only in the case where it is not needed.

Ours are compiled in, so they are always present and always apply to whatever
build is running. `docs/maintenance.md` §A.1 records the empirical reason this
matters: *"Valve usually moves byte offsets without moving the prologues we anchor
on — so the hooks keep installing on their own"*, and an unknown hash stops
nothing because SafeMode is advisory. That is the majority case, and on that
majority case we need no human and no new data at all while they need both.

### §5.6 Where they are not wrong, correcting an implication in our own docs

`docs/rva-feed-design.md` says our `xlate_vaddr` is *"the Linux-specific piece […]
correct even when segments load at different biases (where OST's naive `base +
rva` would be wrong)"*. Reading their Linux backend, that criticism **does not
carry over to this port**: `DynamicLibrary` resolves the module through
`dlinfo(RTLD_DI_LINKMAP)` and returns `link_map::l_addr`, with the comment *"l_addr
is the load bias; the ELF's link-time vaddrs start at 0 for a .so"* [read:
`SFPlatform/Linux/DynamicLibrary.cpp:58-65`], and `PatternLoader` adds the RVA to
that. Load bias plus link-time vaddr is the correct primitive, not the naive
first-mapping base.

Ours is still strictly more general — `VaddrXlate::Translate` goes file vaddr →
file offset → runtime address via the file-offset column of `/proc/self/maps`, so
it is right even if `l_addr + p_vaddr` does not hold for every segment [read:
`src/vaddr_xlate.hpp`]. Whether that case actually occurs on any shipped
`steamclient.so`, and therefore whether their approach would ever misresolve, is
**not established here**: our tool was written and validated against a live
translation, but nothing in this analysis shows the simpler form failing. Treat
the generality as insurance, not as a demonstrated defect on their side.

### §5.7 The two derivation techniques do not overlap

Restating §4.6 as the conclusion of this axis, because it is the one place the
comparison produces something we might actually use:

- **VProf scopes** reach functions Valve profiles. That includes
  `CheckAppOwnership` and `BuildDepotDependency` — which our CI does *not* derive
  automatically today — and it needs no disassembler, only the string table and
  `.eh_frame`. It is scriptable and CI-able; our `derive_patterns.py` path needs
  Ghidra and a human (`maintenance.md` §A.2).
- **RTTI vtable slots** reach virtual functions of classes with type info,
  including `CConfigStore::GetBinary`, which they could not locate at all and
  worked around by writing `config.vdf` from a Python script.
- **Neither** reaches the message layer, which is why their gates 2 and 6 are
  closed (§4.3) and why our GMRC hook exists as a *function* hook.

The honest summary of the axis: **they lose it decisively on delivery and
process, and they have one producer-side technique we do not have.**

### §5.8 What their failure looks like, and what ours looks like

Both projects thought about this, and the designs are near-opposites.

**Theirs, fail-closed:** the generator refuses to emit, the installer exits
non-zero, `PatternLoader` shows a popup naming the unsupported build with four
suggested actions, and hooks for that module are disabled while the rest keeps
running. Nothing is patched at a guessed address. The cost is that the user is
stopped until an update to the repository arrives.

**Ours, fail-degraded:** the hash check is advisory, hooks install per-hook with
`ok`/`disabled`/`FAILED` reported into a toast and `status.json`, the wrapper has a
crash-loop fail-safe that boots vanilla Steam, and `maintenance.md` sorts the
outcomes into three tiers. The cost is that a partially-resolved hook set can look
alive while unlocking nothing — precisely what their `CUtlMemoryGrow` refusal is
designed to prevent — and what protects us there is that DepotKey and GMRC failing
is loud rather than silent.

Neither is wrong. Theirs suits a tool a user runs by hand from a clone; ours suits
one that has to keep working on a Deck in Game Mode with no terminal.

### §5.9 The test that was not run

If §0's execution exclusion is ever lifted, this is the experiment, and it is
cheap:

0. Restart-prune check for §4.2.1: write depot keys into `config.vdf` the way
   `sync_depot_keys.py` does, start and stop Steam, and re-count
   `DecryptionKey` entries. Cheapest test in this document and it decides
   whether their only Linux depot-key path survives a session.
1. Fetch the two `steamclient.so` builds we already have RVA files for
   (`bc54101b…`, `d0c0ff6e…`) with `tools/fetch_steamclient.py`.
2. Run `tools/gen_linux_patterns.py <binary>` (no `--install`) against each.
3. Compare its output for `bc54101b…` against `res/rvas/bc54101b….yaml` — §4.5
   already shows two values agree; this would extend that to everything both sides
   name.
4. The real question is `d0c0ff6e…`, which is **not** in their `VERIFIED` dict:
   how much does the VProf + call-site path recover on a build they never
   calibrated against, and does `derive_from_call_site` hold there?

> **Revised in §7.5.2** — the `sig` claim in §5.5 is narrowed: dead in the
> automated path, alive in the manual rescue path their own popup documents.

That fourth step is the one that decides F3. If the derivation holds across a
build they have never seen, the technique is worth adding to `check_patterns.py`
as a second automatic path. If it only works on the build it was written against,
it is a curiosity. **Nothing should be implemented before that runs.**

## §6 Trust and risk

Two notes before the findings. First, most of what looked alarming in §2.3 is
**dead code on Linux**, and this section says so rather than reusing the scare.
Second, the one thing that is genuinely serious here is **inherited from
OpenSteamTool, not invented by the fork** — the fork's contribution to it is two
new entry points. Both facts are load-bearing for §7.

### §6.1 Where the untrusted data comes from

One input matters: the `.lua` manifest. Users do not write these — they come in
zips from manifest sources, the same ecosystem our own stack draws on. The file
lands in `<Steam>/config/stplug-in/`, and `LuaFileWatcher` picks up new ones
**while Steam is running** (the README sells this as "Live manifest reload").

So the question for the whole section is: what can a hostile `.lua` do?

### §6.2 The Lua VM runs unsandboxed, with unrestricted HTTP

`LuaConfig::Initialize` calls **`luaL_openlibs(g_lua_state)`** — the complete
standard library — and then registers the config verbs [read:
`src/Utils/Config/LuaConfig.cpp:499-527`]. Nothing is stripped: `os.execute`,
`io.open`, `package.loadlib`, `load` and `dofile` are all reachable. On top of
that it registers **`http_get` and `http_post` with no host restriction of any
kind** [read: `LuaConfig.cpp:132-163`] — arbitrary method, arbitrary URL,
caller-supplied headers.

A `.lua` in a manifest zip is therefore **arbitrary code execution inside the
32-bit Steam client process, with the user's privileges, at the moment the file
is dropped in**, plus a general-purpose HTTP client to exfiltrate with.

We have already written the comparison for this, against a third project.
`lumacore-findings.md` Finding 8 records that **LumaCore hardens exactly this
surface**: no `luaL_openlibs` (only `_G`/`table`/`string`/`math`), `dofile`/`load`/
`require` stripped, and `lcHttpGet`/`lcHttpPost` gated to a hardcoded five-host
allowlist *"against a malicious script exfiltrating via HTTP"*. Three projects,
three positions:

| | Lua interpreter | Standard library | HTTP from Lua |
|---|---|---|---|
| **LumaCore** (Windows) | yes | trimmed to four tables | allowlisted to 5 hosts |
| **OpenSteamTool / SteamFlipper** | yes | **full `luaL_openlibs`** | **unrestricted** |
| **Ours** | **no** — four regexes in `steamidra_lite.py` | n/a | n/a |

Two things must be said in the fork's favour. This is **upstream's design**: OST's
`LuaConfig.cpp` calls `luaL_openlibs` and registers the same two unrestricted HTTP
functions [read: `OpenSteamTool@2a08b0b`]. The fork removed no sandbox, because
there was none. And on the same evidence, LumaCore's hardening shows the ecosystem
knows this is a problem — so the gap is upstream's to close, and the fork inherited
it along with everything else in §2.1's 60 %.

What the fork *did* add is four new Lua-callable verbs — `setlegacycdkey`,
`addprocess`, `forcedenuvo` and **`seteticketurl`** [measured: diff against OST].
The last one is the one that matters, and §6.4 picks it up.

A note for our own users rather than about theirs, and deliberately shallow because
coexistence is out of scope (§0): LumaDeck copies the `.lua` into `stplug-in/`
*"for ecosystem interop"*. On a machine that also runs any SteamTools-lineage
interpreter, a file we wrote is a file something else executes. We do not execute
it; that is not the same as it never being executed.

### §6.3 The network surface, live vs dead on Linux

| Path | Endpoint | Live on Linux? | What leaves |
|---|---|---|---|
| `RemoteToml` pattern fetch | 5 mirrors (`OpenSteam001/steam-monitor`, `madoiscool/steam-monitor`, `git.lua.tools`) × 2 components, **every Steam start** | **yes** | the SHA-256 of your `steamclient.so`/`steamui.so` in the URL path; always 404s (§4.1) |
| Lua `http_get` / `http_post` | anything the `.lua` names | **yes** | anything the `.lua` chooses |
| `EticketClient` mint | `seteticketurl()` from the `.lua`, or `SF_ETICKET_URL` at compile time; **empty by default** | **yes, if configured** | `{app_id, nonce, existing_steam_id}` — including the SteamID already in the local ticket store |
| `ManifestClient` GMRC | `opensteamtool` → `wudrm` → `steamrun` | no — NetPacket unresolved (§4.2) | — |
| `StatsClient` donor lookup | `https://stats.steamflipper.com/<appid>` | no — only called from NetPacket | — |
| `AppUpdater` / `Mirror` | `madoiscool/SteamFlipper`, jsDelivr, `git.lua.tools` | no — `#ifdef _WIN32` (§2.3) | — |
| `TokeerBridge` `bst://` | Tokeer code server | no — `#ifdef _WIN32` | — |
| CloudRedirect | `dlopen`s `<Steam>/cloud_redirect.so` if the user supplies it | yes, if present | nothing of its own |

Two observations. The built-in endpoints are mostly inert on Linux, so **the
network risk is not in their endpoint list — it is in §6.2**, where the endpoint
list is written by whoever wrote the manifest. And TLS is not a finding: the Linux
HTTP backend `dlopen`s libcurl and never touches `CURLOPT_SSL_VERIFYPEER` or
`VERIFYHOST`, so verification is on at curl's defaults [read:
`SFPlatform/Linux/Http.cpp:127-174`]. Worth saying because disabled verification is
the common failure in this class of tool, and it is not present here.

### §6.4 The Tokeer ticket mint

`EticketClient` POSTs `{app_id, nonce, existing_steam_id}` to a configured endpoint
and receives an `appticket` plus an `eticket` *"minted from an owning pool
account"*, so that strict Denuvo titles which bind their encrypted app ticket to a
launch-time nonce get a ticket carrying that exact nonce [read:
`Utils/Tickets/EticketClient.{h,cpp}`]. The backend answers 409 with
`foreign_account` when the local ticket belongs to an account it does not operate,
and the client caches per app, evicting when the local SteamID changes.

Three things are true at once and all three belong in the record:

1. **It is off by default.** `SF_ETICKET_URL` is empty unless set at compile time,
   and with no URL the code *"never touch[es] the network"* [read].
2. **It is enabled from the manifest.** `seteticketurl()` is a Lua verb, so the
   party who supplies the `.lua` chooses the endpoint. Given §6.2 that is barely an
   escalation — a `.lua` that can call `os.execute` does not need a dedicated
   exfiltration verb — but it means the ticket path's endpoint is untrusted input
   by design, not configuration the user sets.
3. **What it implies operationally is a pool of real Steam accounts** owning the
   titles, minting tickets on demand for strangers. That is the same shape as the
   Tokeer activation path recorded in `slsdeck-analysis.md` §5.7. Whether the two
   are the same service is still unproven (§2.3); the name and the `bst://` scheme
   are the only link, and neither is evidence.

For a Linux user who never sets `seteticketurl`, none of this executes. For one who
installs a manifest that sets it, their SteamID goes to a host they did not choose.

### §6.5 The `config.vdf` writer is careful — credit where due

`tools/sync_depot_keys.py` is the piece that compensates for the unresolved depot-key
hook (§4.2), and it is the best-engineered file in the repository [read]:

- refuses to run while Steam is up (`pgrep -x steam`), because *"Steam rewrites
  config.vdf on exit and would discard edits"*;
- reads and writes **byte-exact**, explicitly avoiding `errors="replace"` so it
  cannot corrupt account names or install paths it does not understand;
- sanity-checks the result (*"refusing to write: brace count did not change as
  expected"*) and bails rather than writing a malformed VDF;
- `--dry-run` to preview.

It touches the file that holds every depot key Steam has ever cached, and it
handles it the way that file deserves.

### §6.6 Privilege hygiene — they win this one outright

*"Everything lands under `$HOME` — nothing system-wide, no root, no `PATH`
changes"* [their claim], and it holds: **no `sudo` anywhere in
`install_linux.sh`** [measured]. Every write is under `$HOME`, including the Steam
tree; `/usr/lib/millennium` is only ever read, on the opt-in path. The module runs
as the user, in the Steam client process.

Ours does not compare well. `slsdeck-analysis.md` §4.4 already recorded that
LumaDeck, like every Decky plugin, **runs its backend as root** — and that backend
is 13.682 lines of Python that parses third-party manifest zips. lumalinux itself
runs as the user, so the engine layers are comparable; the plugin layer is not.
This is a real asymmetry in their favour and it is not narrowed by anything in this
document.

### §6.7 Supply chain

No CI, no signing, no reproducible build (§2.7). The install path is
`git clone` → `gcc` on the user's machine, which at least means users compile what
they can read; there is no prebuilt Linux binary to trust or verify. The delivery
infrastructure that *does* exist points at accounts the repository never names —
`madoiscool/SteamFlipper`, `madoiscool/steam-monitor`, `git.lua.tools` (§2.3) — and
on Linux the only one reached is the pattern feed, which 404s.

The Windows self-updater validates a staged DLL by size, MZ header and SHA-256
against `latest.toml` from that same mirror chain [read: `AppUpdater.h`] — which is
integrity against corruption, not authenticity: whoever controls the mirror
controls both the hash and the payload. It is compiled out on Linux, so it is not a
Linux finding. It is a fair description of the trust posture the project would have
if a Linux artifact ever shipped.

### §6.8 What this axis does and does not say

It does **not** say SteamFlipper is malicious. Nothing read here exfiltrates
anything on its own; the dangerous capability is a general one, inherited from
upstream, that a hostile *manifest* would have to exploit — and the same hostile
manifest reaching our stack would be defused by `steamidra_lite.py` parsing it
instead of running it.

It does say that the trust boundary sits in a different place for the two projects.
Ours treats the `.lua` as **data**; theirs treats it as **a program**. Every other
difference on this axis — the endpoints, the ticket mint, the mirrors — is smaller
than that one.

> **Narrowed in §7.5.3** — the distinction holds, the safety conclusion does not.
> Their VM runs as the user; our parser runs inside a backend that runs as root and
> extracts the same untrusted zip. "Parsing beats executing" is true; "our blast
> radius is smaller" does not follow.

## §7 Verdict and adversarial pass

### §7.1 The pre-registration in §0 did not happen

§0 promised: *"Axes and weights are fixed before the comparison sections are
written."* **They were not.** §1–§6 were written first and the axes below were
derived from them afterwards, which is exactly the procedure that lets an author
pick the axes his own project wins. The counter-measure failed, and pretending
otherwise would be worse than admitting it.

Two things partially compensate, and the reader should weigh them as partial:

- Every axis below is one **both** projects visibly spend effort on — none was
  invented to have something to win. The one axis a hostile reader would expect to
  be missing (privilege) is present and lost.
- §7.5 runs an adversarial pass specifically over the conclusions that favour us,
  and it **revises three of them**.

### §7.2 Axes and scores

Scored 1–5 for the **Linux Steam client** specifically. Weight reflects how much
the axis decides whether the tool works and keeps working.

| # | Axis | W | Ours | Theirs | Why |
|---|---|---|---|---|---|
| A1 | Gate coverage (does it install a game?) | 5 | 5 | 2 | 6/6 vs 4/6 by hook; gate 6 closed on Linux (§4.2) |
| A2 | Address delivery after a Steam update | 5 | 5 | 1 | runtime feed + bot-opened PR vs a dict in the repo, no CI (§5.4) |
| A3 | Injection durability | 4 | 4 | 2 | wrapper outside Valve's files + guardian vs a proxy Steam overwrites (§3.4) |
| A4 | Trust boundary on manifest data | 4 | 4 | 1 | parsed by regex vs executed by an unsandboxed Lua VM (§6.2) — but see §7.5.3 |
| A5 | Address derivation (producer side) | 3 | 4 | 3 | three mechanisms + CI vs VProf + structural derivation; theirs is scriptable where ours needs Ghidra (§5.7) |
| A6 | Privilege hygiene | 3 | 2 | 5 | LumaDeck's backend runs as root; theirs needs no root at all (§6.6) |
| A7 | Injection blast radius | 2 | 2 | 5 | `LD_PRELOAD` maps us into every 32-bit child; their proxy resolves only in the client (§3.3) |
| A8 | Process and verifiability | 3 | 5 | 1 | 3 workflows, test suites, `RESEARCH.md` vs no CI and one squashed commit (§1.1, §2.7) |
| A9 | Documentation honesty | 2 | 3 | 3 | their "Known limits" is candid and accurate; both sides drift (§3.5, and F1/F2/F5 are ours) |
| A10 | Deck / Game Mode fit | 3 | 5 | 1 | QAM plugin vs a CLI install and Millennium; they do not target it |

Weighted: **ours 4.12, theirs 2.15** (Σ w·s ÷ Σ w; Σw = 34, ours 140, theirs 73).

### §7.3 The aggregate is close to meaningless, and that is the finding

Three reasons not to quote that number:

1. **The axes were chosen after the fact** (§7.1). A different honest reader would
   pick a different set.
2. **The two projects are not aimed at the same person.** Ours is a four-component
   stack for a Steam Deck in Game Mode. Theirs is one `.so` for a desktop Linux
   user who wants no plugin, no root and no launcher. A1 and A10 encode a target
   they never claimed.
3. **The comparison is between a project with three months of update-response
   machinery and a repository that is one day old.** SteamFlipper's entire public
   history is 2026-09-02. Scoring A2 and A8 against ours is scoring a first commit
   against a maintained project, which is fair as a snapshot and unfair as a
   prediction.

The useful two-level view: **on the engine layer we are ahead where it decides
whether a game installs and keeps installing (A1, A2, A3), and behind on how
cleanly the thing sits on the user's machine (A6, A7).** That second pair is not
cosmetic and §7.5 refuses to let it be minimised.

### §7.4 Verdicts by profile

- **"I want games on my Deck in Game Mode."** Ours, and it is not close — they do
  not target Game Mode, install from a clone with a compiler, and cannot open
  gate 6 (§4.3).
- **"Desktop Linux, I want the smallest thing that works and no root."** Theirs is
  a reasonable pick *if* the Steam build matches their pinned hash and you bring
  your own manifests. Otherwise it will not install at all (§5.3), which is at
  least a loud failure.
- **"I care what runs in my Steam process."** Theirs maps nothing into your games
  (§3.3) and needs no root (§6.6); ours does the opposite on both. But theirs
  executes manifest `.lua` files as programs (§6.2). If you write your own
  manifests, theirs is the cleaner posture; if you download them, ours is.
- **"I have to fix it when it breaks."** Ours. `RESEARCH.md`, `maintenance.md`'s
  three tiers, a watch bot and a feed that ships fixes without a release, against a
  repository with no history explaining any of its addresses.
- **"Denuvo."** Neither, on Linux: their `EticketClient` path is off unless a
  manifest turns it on (§6.4) and the ticket layer it feeds sits behind the
  unresolved IPC hooks.

### §7.5 Adversarial pass

Five conclusions that favour us, challenged.

**§7.5.1 REVISED — "gate 6 closed means installs cannot update" (§4.3).**
The challenge: SteamFlipper ships `setManifestid()`, and pinning is a *supported
workflow*, not a workaround. A user who pins every depot to the manifest they hold
never needs a request code, and the game simply stays at that version — which is
also what LumaDeck's own per-game auto-update toggle does deliberately. So the
correct statement is not "installs break on update"; it is **"installs are
effectively pinned, and updating means re-seeding `depotcache` by hand"**. That is
a real limitation against our native auto-update, but it is a coherent design, not
a defect. §4.3's phrasing overstated it.

**§7.5.2 REVISED — "their `sig` fallback is dead weight" (§5.5).**
The challenge: `PatternLoader`'s own failure popup tells the user to *"Drop a
matching TOML at `<Steam>/steamflipper/pattern/<component>/<sha256>.toml`"*. A user
who copies a previous build's file under the new hash gets exactly the case the
sig is for: right function, moved address, RVA now wrong, signature still matches.
So the sig is dead in the **automated** Linux path, and alive in the **manual
rescue** path the software explicitly documents. Narrowed accordingly.

**§7.5.3 NARROWED — "we treat the `.lua` as data, they treat it as a program"
(§6.8).**
The claim survives but the safety conclusion drawn from it does not, and this is
the sharpest counter in the pass. Their Lua VM runs **as the user**. Our parser
runs inside a LumaDeck backend that runs **as root** (§6.6), and that backend
downloads the same third-party zip and calls `archive.extractall()` on it before
anything is parsed [read: `LumaDeck/backend/downloads.py:956-958`].

To be precise about what that does and does not mean: Python's `zipfile`
sanitises member paths and never creates symlinks, so classic zip-slip is **not**
a finding here. What remains is that we process attacker-supplied archives with
root privileges, and they process attacker-supplied *code* with user privileges.
"Parsing beats executing" is true; "therefore our blast radius is smaller" does
not follow. On a Deck, a bug in our root-side handling of a hostile zip is worse
than `os.execute` in theirs.

**§7.5.4 NARROWED — "injection durability, ours decisively" (§3.4, A3).**
The challenge: our advantage is real for the *coverage* half — our wrapper is not
a file Valve overwrites, and a guardian re-affirms it — but the *address* half
needs a human on both sides when a pattern moves (`maintenance.md` §A.2/§A.3), and
our own README tells users to reapply components after a Steam update. The gap is
"they lose injection **and** addresses on every client update, we usually lose
neither and sometimes lose addresses", not "we are immune". A3 stays 4 vs 2; the
prose in §3.4 was closer to claiming immunity than the code supports.

**§7.5.5 HELD — provenance is measured, not scored.**
The challenge: §2.1's 60 %-verbatim figure reads as a knock, and it should not —
reusing upstream is the *correct* choice for a port, and our own RVA feed is
copied from OST's design by our own documentation's admission (§5.1). Held
because no axis in §7.2 scores originality: provenance appears in the document to
establish *what was written for Linux and by whom* (which decides who can fix it,
A8), not as a quality judgement. §2.2's framing already says the 3.538 lines of
port work "is not a small one".

### §7.6 What would overturn this

- **A1/A2 flip if** they publish Linux pattern files per build and locate the
  message-layer functions. Nothing about that is impossible — `BBuildAndAsyncSendFrame`
  is findable with a disassembler; it just is not automated.
- **A5 flips toward them if** §5.9's experiment shows `derive_from_call_site` and
  the VProf pass holding on `d0c0ff6e…`, a build they never calibrated against.
  That is the single test most likely to change something in our own tree (F3).
- **The whole comparison changes if** OpenSteamTool's `main` moves again. It has
  been static since 2026-07-06 (§1.2), and this fork inherits its cadence.
- **Nothing here is an observation.** Nothing was executed (§0). A field report of
  either project working or failing on a real Deck outweighs the entire document,
  as `slsdeck-analysis.md` §12.3 found the hard way.

## §8 Findings list

Five, and **none is a code change**. Three are corrections to our own
documentation that this analysis turned up; two are evidence-gated candidates that
must not be implemented before §5.9's experiment runs.

- **F1 — documentation, `opensteamtool-findings.md`.** Its architecture-constraint
  ruling loses its platform leg for this fork (§2.4). The message-layer leg still
  holds — but for the original reason only (coexistence with vanilla SLSsteam),
  **not** because the layer is unavailable on Linux. §4.4 corrects a claim this
  document made in an earlier revision: SLSsteam hooks `CProtoBufMsgBase` by byte
  pattern on this same binary and moon hooks the depot-key protobuf on top of it.
  So the restatement is "not portable to us while we run beside stock SLSsteam",
  and nothing more.
- **F2 — documentation, `RESEARCH.md` §5 / `README.md`.** Our `LD_PRELOAD` write-up
  argues the choice against `LD_AUDIT` and never states its cost: the `.so` is
  mapped into every 32-bit child Steam spawns, and the `/proc/self/comm` allowlist
  gates the work, not the mapping (§3.3). Worth one honest paragraph.
- **F3 — candidate, `tools/`.** VProf-scope derivation and our RTTI slot
  derivation recover disjoint sets of functions (§4.6). VProf reaches
  `CheckAppOwnership` and `BuildDepotDependency`, which we do not derive
  automatically today. Whether it is worth adding as a second automatic path is
  §5's question, and it is evidence-gated: §5.9 names the exact experiment, which
  was **not run** (execution is out of scope). Do not implement before it does.
- **F4 — cross-check, `res/rvas/`.** Their pinned `ProcessPendingLicenseUpdates`
  and cache-owner displacement match ours exactly on the same build (§4.5). Worth
  a line in `rva-feed-design.md` recording that an independent derivation agrees;
  it is the only external confirmation our feed has.
- **F5 — documentation, `lumacore-findings.md` Finding 8.** That finding treats
  the hardened-Lua-VM threat model as LumaCore-specific and therefore not ours.
  §6.2 shows the *unhardened* version is the ecosystem norm — OST and this fork
  both run `luaL_openlibs` with unrestricted HTTP — so the finding is worth
  widening from "LumaCore does this" to "we are the only one of four that does not
  execute the `.lua` at all". No code change; it is our strongest trust position
  and it is currently recorded as a footnote.
- **F6 — review candidate, `LumaDeck/backend/downloads.py`.** Raised by the
  adversarial pass, not by their code (§7.5.3). We call `archive.extractall()` on a
  third-party manifest zip inside a backend that runs as root. Python's `zipfile`
  sanitises member paths and creates no symlinks, so there is **no known
  vulnerability here** — this is a note that the privileged side of our pipeline
  handles untrusted archives, and that A6 is the axis we lose. Worth a look, not an
  alarm.

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
