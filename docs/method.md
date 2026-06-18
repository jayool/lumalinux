# How an unowned-game install actually works

This document explains, end to end, the method behind installing and playing a
Steam game you don't own — what Steam checks, which checks fail for an unowned
title, who clears each one, and how the three tools in this space (lumalinux,
LumaCore/SteaMidra, SteamTools/OpenSteamTool) implement it.

It is written from cross-reading three codebases:
- **lumalinux** (this repo) — Linux.
- **LumaCore + SteaMidra** (`github.com/Midrags/SFF`) — Windows.
- **OpenSteamTool** (`github.com/OpenSteam001/OpenSteamTool`) — a
  reverse-engineering of SteamTools, Windows.

Where a claim was verified against source it says so; where it's inferred it
says so too.

---

## 1. The six gates

Pressing **Install** runs Steam through a chain of checks. For a game you own,
all pass automatically. For one you don't, six of them are closed and must be
opened:

| # | Gate | What Steam checks | Why it fails when unowned |
|---|------|-------------------|---------------------------|
| 1 | **Ownership** | Does the account hold a licence for this app? | You don't → Steam won't even offer Install |
| 2 | **PICS appinfo** | The app's product info (depot list, manifest ids) | Valve won't return the access token without a licence |
| 3 | **Depot surfacing** | Are the content depots in a package the user owns? | If not → "0 target depots" → 0-byte "Fully Installed" |
| 4 | **Manifest pinning** | Which exact version (manifest GID) of each depot to fetch | Must be pinned to the version we have keys for |
| 5 | **Depot key** | The 32-byte (AES-256) key to decrypt the depot's chunks | Valve won't hand it over for an unowned depot |
| 6 | **Manifest request code** | A per-manifest token that **authorises the manifest download from the CDN** | Valve **denies** it for unowned content |

Gates 1–5 can be faked **locally** — you only have to convince *your own* Steam.
Gate 6 is the hard one: the manifest request code is validated **server-side by
Valve**, so it cannot be fabricated locally. It has to be obtained from a
**third-party service** (e.g. `gmrc.wudrm.com`) that derives it from accounts
that genuinely own the game.

Note: the request code authorises fetching the **manifest** (the file listing).
Once you have the manifest, the actual content **chunks** are fetched from the
CDN by SHA and decrypted with the depot key (gate 5) — the per-manifest code is
not required again per-chunk.

---

## 2. Where the bytes come from — always Steam's CDN

There is no third-party content mirror. The game's bytes always come from
**Steam's own CDN** (`*.steamcontent.com`). What differs between tools is **who
talks to the CDN** and downloads the content:

### Model A — the Steam client downloads (native client download)

The **Steam client itself** downloads the content chunks. The tool's job is to
make the client *able* to: fake ownership, surface the depots, serve the depot
keys, and supply the manifest request code(s). Disk layout is identical to a
legitimate install.

- **lumalinux** (Linux) — the package-0 finder + DepotKey/BuildDep/GMRC hooks +
  SLSsteam.
- **SteamTools / OpenSteamTool** (Windows) — hooks `steamclient64.dll` and
  supplies the manifest code to the client (`fetch_manifest_code_ex`). Verified:
  OpenSteamTool ships **no depot chunk downloader** — Steam does the download.

### Model B — a standalone downloader

A separate program implements Steam's CDN content protocol and downloads
everything itself (manifests, content chunks, fetching keys + request codes),
decrypts, writes the files to `steamapps/common/<game>/`, and writes an `.acf`
so Steam treats the game as already installed. Steam never downloads.

- **DepotDownloader / DepotDownloaderMod / ACCELA**.
- SteaMidra's `sff/depot_downloader.py` (the DepotDownloaderMod path) is the
  opt-in Model-B alternative.

### Where SteaMidra actually sits (important correction)

SteaMidra's **default** path is **Model A**, not B. Verified in source:

- `sff/manifest/downloader.py` downloads **only the manifests** (every function
  is `download_manifest(s)`; there is no content-chunk loop). It uses
  `steam.client.cdn.CDNClient` (the ValvePython library) to talk to Steam's real
  CDN, fetching the manifest request code via `get_gmrc()` and hitting
  `…/depot/<depot>/manifest/<mid>/5/<req_code>` directly.
- `sff/ui.py:process_lua_full` then seeds those manifests into `depotcache/`,
  writes the `.acf`, and registers the app with SLSsteam (`sls_man.add_ids`).
- The **Steam client then downloads the content chunks natively**, finding the
  manifests already in `depotcache` and the keys/ownership faked by the in-Steam
  layer.

So "SteaMidra downloads natively from Steam's CDN" is correct — it pre-downloads
the **manifests** from the CDN and lets the **Steam client** pull the content.

---

## 3. The native flow, phase by phase (this repo's stack)

Concretely, with **lumalinux + SLSsteam + steamidra_lite/LumaDeck** on Linux:

**Phase 0 — on-disk setup (`tools/steamidra_lite.py`, before Steam runs):**
1. Read the Hubcap `.zip` (one `.lua` + the `.manifest` files).
2. Copy the `.manifest` files into `depotcache/` and `config/depotcache/`
   (pre-seeding, so Steam doesn't have to request them).
3. Write `~/.config/lumalinux/keys.txt`: per depot →
   `depot;parent_app;manifest_gid;size;AES_key`.
4. Add the AppID to SLSsteam's `config.yaml` `AdditionalApps`.
5. Mirror the keys into `config.vdf` (a belt-and-suspenders backup).
6. Write a clean `.acf` stub so Steam shows **Install** (not "No internet").

**Phase 1 — Steam start (hooks load):**
7. `steam.sh` exports `LD_AUDIT=…SLSsteam.so` and `LD_PRELOAD=…liblumalinux.so`.
8. **SLSsteam** clears **gates 1 + 2**: fakes ownership and supplies PICS access
   tokens → the game appears owned, with an Install button.
9. **lumalinux** installs the DepotKey / BuildDep / GMRC hooks and starts the
   **package-0 finder** (see RESEARCH §13).

**Phase 2 — you press Install (the runtime chain):**
10. **package-0 finder** clears **gate 3**: finds Package 0 in memory and
    appends the game's depot ids to its `AppIdVec` → the depots survive the
    per-depot licence filter.
11. **BuildDep** clears **gate 4**: patches each surfaced depot's manifest
    GID/size to the pinned version.
12. **DepotKey** clears **gate 5**: serves each depot's AES key from `keys.txt`
    when Steam asks.
13. **GMRC** clears **gate 6**: when Steam asks Valve for a manifest request code
    and is denied, the hook fetches it from `gmrc.wudrm.com` and returns it.
    **This is the load-bearing piece** — without it nothing downloads. (It's
    still needed despite the pre-seeded manifests in Phase 0, because Steam
    re-requests codes at runtime for content manifests it re-validates and for
    the shader-pre-cache manifest, which isn't pre-seeded — see RESEARCH §11.5.)
14. The **Steam client** downloads the content chunks from the CDN (authorised
    by the code from step 13), decrypts them (key from step 12), and commits
    them to `steamapps/common/<game>/`.
15. `.acf` → `StateFlags=4` → **Fully Installed** → playable.

---

## 4. Ecosystem side-by-side

| Gate / concern | **lumalinux** (Linux) | **LumaCore + SteaMidra** (Windows) | **SteamTools / OpenSteamTool** (Windows) |
|---|---|---|---|
| **Injection** | `LD_PRELOAD` in `steam.sh` (added by Headcrab) | `dwmapi.dll` proxy → `LumaCore.dll` → copies `steamclient64.dll` to `bin\lcoverlay.dll` and hooks the copy | `dwmapi.dll`+`xinput1_4.dll`+`OpenSteamTool.dll` proxies; hooks `steamclient64.dll` + `steamui.dll` |
| **1+2 Ownership / PICS** | **SLSsteam** (separate, `LD_AUDIT`): `CheckAppOwnership`, subscribed apps, PICS tokens | **LumaCore itself**: `PackagePatch::CheckAppOwnership` patch, forged AppTicket/ETicket (`CmdUser`/`IPCBus`, for Denuvo), `SteamCapture::NotifyLicenseChanged` (in-memory licence inject, no restart) | Forged AppTicket/ETicket, ConfigStore ticket reuse, SteamStub vuln |
| **3 Depot surfacing** | package-0 **finder** (active cache walk, §13) | `PackagePatch::LoadPackage` (hook, injects appids into Package 0's `AppIdVec`) | KeyValues / manifest patching |
| **4 Manifest pinning** | **BuildDep** hook (`BuildDepotDependency`) | `ManifestBind::BuildDepotDependency` (identical) | manifest patching |
| **5 Depot key** | **DepotKey** hook from `keys.txt` | `DepotKeys::LoadDepotDecryptionKey` from `.lua` | `addappid(depot,0,"hexkey")` in lua |
| **6 Manifest request code** | **GMRC** hook at runtime (`gmrc.wudrm.com`) | **SteaMidra Python** pre-fetches it (`get_gmrc`) to pre-download manifests — **LumaCore.dll has no runtime GMRC hook** (verified) | `fetch_manifest_code_ex(...)` via `opensteamtool`/`steamrun`/`wudrm`, supplied to the client at runtime |
| **Who downloads content** | Steam client (Model A) | Steam client; manifests pre-seeded by SteaMidra (Model A) | Steam client (Model A) |
| **Lua location** | `keys.txt` + `config/stplug-in/<appid>.lua` (interop) | `config/stplug-in/<appid>.lua` | `config/lua/` |
| **Extras** | — | family-share bypass (`PacketRouter`), achievements (`setStat`), rich-presence, CD-key | achievements/stats spoof |

---

## 5. The manifest request code — the one piece that needs a third party

Every tool has to solve gate 6, and they do it in two distinct ways:

- **Runtime hook (lumalinux, SteamTools/OpenSteamTool):** hook
  `GetManifestRequestCode`. When the Steam client asks Valve for the code and is
  denied, fetch it from a GMRC service and return it to the client. The client
  then fetches the manifest from the CDN itself.
- **Pre-fetch (SteaMidra Python):** fetch the code with `get_gmrc()` *before*
  Steam runs, download the manifest from the CDN with it, and seed it into
  `depotcache/`. Steam then finds the manifest already present and downloads
  the content chunks without needing the code for those manifests.

Both end with the **Steam client** doing the content download.

### Note on RESEARCH §11.5 vs the actual LumaCore source

RESEARCH §11.5 describes a LumaCore `GMRC hook (ManifestBind::FetchSteamRun)`
fetching from `manifest.steam.run`. The **current** LumaCore source in SFF has
**no such hook** — `ManifestBind.cpp` only does `BuildDepotDependency`, and the
only `GetManifestRequestCode` reference anywhere is the protobuf message
definition in `proto/steam_messages.proto`. On Windows the request code is
fetched by the **SteaMidra Python layer** (`sff/http_utils.py:get_gmrc`), not by
LumaCore.dll at runtime. lumalinux's runtime GMRC hook is therefore **unique to
the Linux native-download path** — it's the piece that lets the *Steam client*
(rather than a pre-fetch step) obtain the code mid-download. Treat §11.5's
LumaCore column as describing an older/assumed LumaCore, not the one in SFF
today.

---

## 6. Game lifecycle: install, update, and manifest pinning

Once a game is installed, its life is governed by the **manifest GID** each depot
carries in `keys.txt` (`depot;parent;gid;size;key`) and how BuildDep acts on it.
`steamidra_lite` has two modes:

- **No-pin (default)** — writes `gid=0`. BuildDep does **not** patch, so Steam
  follows Valve's current manifest and the game **auto-updates** like a legit
  owner. See "Auto-update by unpinning" below; validated in RESEARCH §14.
- **`--pin`** — writes the zip's GID. BuildDep patches Steam's download plan to
  it, so the game **stays frozen** on that exact version (e.g. a modded game that
  only works on a specific build).

### Why a pin exists — and what auto-update risks

The pin is **not** what enables the download; it's what **prevents an
auto-update**. Without a pin (the default), a typical update downloads on its
own:

1. Steam queries PICS → gets the **new** manifest GID.
2. The **GMRC** hook fetches the request code for that new GID (the GMRC service
   has it — it derives codes from accounts that own the game, which are on the
   latest version).
3. Steam downloads the new manifest + the changed chunks from the CDN.
4. The depot AES key is **per-depot and stable across versions**, so the existing
   key decrypts the new chunks.

So an unpinned game just updates itself — that's the **default**. `--pin` is the
**opt-in** for the two cases where an auto-update can break:

1. The update **adds a new depot** whose key you don't have → `Missing decryption
   key` on that depot (cf. the Balatro case, §13.7 in RESEARCH — note §13.7's
   update that this can be transient and self-recover).
2. Valve **rotates the depot key** (rare) → your old key stops decrypting.

Both are uncommon and **non-destructive** — Steam stages updates and only commits
on success, so the installed version keeps playing — and both recover the same
way: re-deploy a fresh Hubcap zip (which carries the new depot/key). `--pin` is
for when you'd rather not risk even that — e.g. a modded install you want frozen
on a known-good build. It's **per depot**: a depot with `gid=0` follows Valve, a
depot with a GID is frozen.

### Moving a `--pin`'d game to a new version

A no-pin game updates itself; nothing to do. To move a **`--pin`'d** game forward
(or recover a game whose auto-update needs new keys) you need the **new Hubcap
zip**:

- Re-run `steamidra_lite --pin` with the new zip (LumaDeck's **"Re-download
  Manifest"** does exactly this). It rewrites `keys.txt` with the **new** GID
  (plus any new depots/keys the update added), pre-seeds the new manifest into
  `depotcache`, and re-stubs the `.acf`.
- BuildDep then pins the **new** version → Steam downloads the delta.

To **freeze a running game on its current version** without a zip, LumaDeck's
"pin to installed manifests" reads the gids from the `.acf` `InstalledDepots` and
writes them into `keys.txt` (gid set) — the inverse of unpinning. This is
implemented by `steamidra_lite`'s zip-less modes, which LumaDeck shells out to:

- `--pin-installed APPID` — read `appmanifest_<APPID>.acf` `InstalledDepots` →
  write those gids into the app's content-depot lines in `keys.txt` (freeze).
- `--unpin APPID` — set those gids to 0, comment `setManifestid`, purge the
  app's depotcache (back to auto-update).
- `--pin-status APPID` — print `{appid, pinned, depots}` (so the UI toggle knows
  which state to show).

These short-circuit early (like `--accela-mark`): no zip, no full deploy.

### Auto-update by unpinning (validated 2026-06)

The "would download even unpinned" claim above is **not theoretical** — it was
validated end-to-end on Linux (Balatro and Vampire Survivors, native client,
with a visible delta download). This is the Linux equivalent of the SteaMidra
Windows trick of commenting out the `setManifestid` lines in the stplug-in `.lua`
(`--setManifestid(...)`) to make the game **auto-update like a legit owner**.

On Linux/lumalinux, `keys.txt` — not the stplug-in `.lua` — is the source of
truth, so "unpinning" a depot means three concrete steps:

1. **`keys.txt`: set the depot's `manifest_gid` (and size) to `0`**, keeping the
   key: `depot;parent;0;0;key`. With `gid=0`, `GetDepotsForApp` excludes the
   depot, so **BuildDep does not patch it** → Steam uses Valve's real current
   manifest gid (logs `BuildDep: app … none required patching`, never
   `BuildDep: PATCH`). The depot is still injected into `PackageId=0`
   (`GetAllDepotIds` returns all depots regardless of gid) and its key is still
   served (DepotKey hook), so the new content still decrypts.
2. **Comment `--setManifestid` in `config/stplug-in/<appid>.lua`** — parity with
   the Windows method; interop only, since lumalinux reads `keys.txt`.
3. **Nuke the depot's entries in `depotcache/` (and `config/depotcache/`)** —
   **required**. If the old pinned manifest stays cached, Steam reuses it instead
   of fetching the current one. (Mirrors the Discord advice to "nuke your
   depotcache folder".)

Then restart Steam: it detects the installed manifest ≠ Valve's current,
re-fetches the new manifest (GMRC supplies its request code at runtime — see
below), downloads the delta, decrypts with the same per-depot key, and updates —
**automatically, no click**. Verified live: Vampire Survivors went
`1794681 6531…→7054…` and `1794685 7577…→7852…` on its own after unpinning.

So pinning vs unpinning is a per-depot policy choice in `keys.txt`: a non-zero
gid pins (exact version, always decryptable); `gid=0` follows Valve (auto-update,
at the cost of a future update possibly adding a depot/key you don't have).

### How an available update is detected

`steamidra_lite` writes `~/.local/share/ACCELA/depots/<appid>.depot` recording
the manifest GID you currently hold. ACCELA's / LumaDeck's update check compares
that against the current public GID from SteamCMD; if they differ it surfaces an
**"Update available"** badge. That's the cue to grab the new Hubcap zip.

That badge is a convenience layer. **Steam itself** decides whether to update by
comparing, **per depot**, the installed manifest GID (in the `.acf`
`InstalledDepots`) against PICS's current GID — **not** by `buildid` alone.
Observed directly: both validation games carried Steam's *current* `buildid` in
the `.acf` (Steam stamps the live buildid at install time even when BuildDep
pinned an older manifest), yet unpinning still triggered an update because the
per-depot manifest GID differed. So an "old manifest under the current buildid"
is enough for Steam to update once the pin is removed.

### Where GMRC stays load-bearing

Even with manifests pre-seeded (§5), the runtime GMRC hook still matters for any
manifest Steam requests that you did **not** pre-seed:

- **Shader pre-cache** — a per-GPU depot (id == app id) that Steam generates;
  its manifest is never in the Hubcap zip, so if Steam runs the shader pre-cache
  it must request the code → GMRC supplies it. (The shader cache is an
  optimisation; the game installs and plays without it, but a denied request can
  surface as a transient "No connection" — RESEARCH §11.5.)
- **Updates** — the new manifest isn't cached, so GMRC fetches its code at
  runtime (until you re-seed via a new zip).

This was observed directly: Brotato fired GMRC for its shader depot (1942280);
Tilt It! Golf, whose content manifests were all pre-seeded and which ran no
shader pre-cache, never fired GMRC and still installed end to end.

---

## 7. The one-line summary

The content always comes from Steam's CDN, and in the native method the **Steam
client** downloads it. The tools' job is to clear the six gates so the client
will: SLSsteam/LumaCore fake **ownership** (1–2); a LoadPackage injection or the
package-0 finder **surface the depots** (3); a BuildDep hook **pins the
manifest** (4); a DepotKey hook **serves the AES key** (5); and the **manifest
request code** (6) — the only server-validated token — is obtained from a
third-party GMRC service, either by a runtime hook (lumalinux, SteamTools) or by
pre-fetching the manifests (SteaMidra).
