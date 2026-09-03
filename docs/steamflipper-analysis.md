# SteamFlipper vs the LumaDeck stack — exhaustive analysis

*In progress. **§1–§2 complete** (provenance and inventory). §3 (gate-by-gate),
§4 (surviving a Steam update), §5 (trust and risk) and §6 (verdict + adversarial
pass) are pending and are marked as such below. Nothing here is a recommendation
to change LumaDeck or lumalinux. **Read §0 first**: nothing was executed, and the
evidence tags are load-bearing.*

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

1. **Axes and weights are fixed before the comparison sections are written** (§6,
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

Three things follow, and only the first is settled.

1. **These did not come from public OST and were not written for Linux.** The
   `rundll32`/HKCU/`.dll` vocabulary is not a porting artefact — it is code that
   was authored against Windows and carried in.
2. **Their actual origin is unresolved.** Three possibilities are open: an
   unpublished or private OpenSteamTool branch; another downstream of OST (note
   that `slsdeck-analysis.md` §5.7 documents a *Tokeer* activation path in
   SLSDeckUniversal — the name is the same, the relationship is not established);
   or authorship by the fork. Nothing read so far distinguishes them. **This is
   an open question, not a finding.**
3. **The `lua.tools` mirror is a link to infrastructure we have already
   catalogued** (`docs/luatools-desktop-app/`, `docs/luatools-windows/`). Whether
   that means anything is a §5 question.

Group C is where the trust analysis will concentrate: a ticket-minting service
backed by "pool accounts", a URI-scheme handler driven by a public website, and a
self-updater that replaces the loaded module from a third-party mirror are three
of the highest-consequence surfaces a tool like this can have. **§5 has not been
written; nothing above is a risk assessment.**

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
  the repository for us**, and §4 is where it gets tested rather than admired.
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

Weighing that properly belongs to §4 and §6, not here.

---

## §3 Gate-by-gate comparison — *pending*

Planned spine: the six gates of `method.md` §1 — ownership, PICS appinfo, depot
surfacing, manifest pinning, depot keys, GMRC — with their message-layer approach
(`Hooks_NetPacket.cpp`, 1.565 L) set against our function-layer hooks, plus the
injection mechanism (`Bootstrap/Linux/sf_bootstrap.c`, the `libXtst` proxy).

## §4 Surviving a Steam client update — *pending*

Planned: `gen_linux_patterns.py` (VProf + `.eh_frame`) against our RVA feed and
`derive_patterns.py`, including running their script over the `steamclient.so`
hashes already recorded in `res/rvas/` to see whether it recovers our anchors.

## §5 Trust and risk — *pending*

Planned: group C (§2.3) — the Tokeer backend and its pool accounts, the `bst://`
handler, the self-updater and its `lua.tools` mirror — plus network surface,
what is written to `config.vdf`, and privilege hygiene.

## §6 Verdict and adversarial pass — *pending*

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
  `src/Bootstrap/Linux/{sf_bootstrap.c,shared.h,README.md}`;
  `src/SFPlatform/{CMakeLists.txt,Linux/*}`;
  `src/Hook/Hooks_NetPacket.cpp`;
  `src/Utils/{Tokeer,Tickets,Update,SteamMetadata}/*`;
  `tools/{gen_linux_patterns.py,install_linux.sh,sync_depot_keys.py}`;
  `steamflipper.example.toml`.
- OpenSteamTool (`OpenSteam001/OpenSteamTool` @ `2a08b0b` and 12 branch tips):
  `src/OSTPlatform/Linux/.gitkeep` (the placeholder confirming upstream never
  implemented a Linux backend), and the counterparts of every file above.
- Ours: `docs/opensteamtool-findings.md` (the ruling corrected in §2.4),
  `docs/method.md` (the six gates, §3's spine), `docs/maintenance.md` §A.2
  (the derivation workflow §4 will compare against), `docs/RESEARCH.md`,
  `docs/slsdeck-analysis.md` (the template this document follows),
  `res/rvas/`, `tools/derive_patterns.py`, `.github/workflows/watch-steam.yml`.
