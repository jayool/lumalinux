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
| 5 | **Depot key** | The AES-128 key to decrypt the depot's chunks | Valve won't hand it over for an unowned depot |
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

Once a game is installed, its life is governed by the **manifest GID pin** in the
BuildDep hook. `steamidra_lite` writes each depot's pinned GID into `keys.txt`
(`depot;parent;gid;size;key`), and BuildDep patches Steam's download plan to that
GID. The effect: Steam stays on the exact version you have keys + a manifest for,
and **does not auto-update** to a newer one.

### Why pin — and what would happen without it

The pin is **not** what enables the download; it's what **prevents an
auto-update**. If BuildDep did *not* pin, a typical update would download on its
own:

1. Steam queries PICS → gets the **new** manifest GID.
2. The **GMRC** hook fetches the request code for that new GID (the GMRC service
   has it — it derives codes from accounts that own the game, which are on the
   latest version).
3. Steam downloads the new manifest + the changed chunks from the CDN.
4. The depot AES key is **per-depot and stable across versions**, so the existing
   key decrypts the new chunks.

So an unpinned game would, in the common case, just update itself. The pin is a
**deliberate safety choice**, because an update can break the install in two ways
the pin guards against:

1. The update **adds a new depot** whose key you don't have → `Missing decryption
   key` on that depot (cancels the whole app — cf. the Balatro case, §13.7 in
   RESEARCH).
2. Valve **rotates the depot key** (rare) → your old key stops decrypting.

The pin trades "auto-updates" for "always decryptable": you stay on a version
whose keyset you fully hold. It's **per depot** — a depot written with `gid=0` in
`keys.txt` is not pinned and would follow PICS, so pinning is effectively opt-in
per depot via whether `steamidra_lite` has a GID for it.

### Updating to a new version

To actually move to a newer version you need the **new Hubcap zip** for it:

- Re-run `steamidra_lite` with the new zip (LumaDeck's **"Re-download Manifest"**
  button does exactly this). It rewrites `keys.txt` with the **new** GID (plus
  any new depots/keys the update added), pre-seeds the new manifest into
  `depotcache`, and re-stubs the `.acf`.
- BuildDep then pins the **new** version → Steam downloads the delta.

Without the new zip, lumalinux keeps you pinned on the version you have — which
is the **safe default, not a failure**. "You need the new zip" is the safe answer,
not a hard technical limit: a *simple* update (same depots, same key) would
download even unpinned; the new zip guarantees you have everything a *complex*
update might add (new depots/keys).

### How an available update is detected

`steamidra_lite` writes `~/.local/share/ACCELA/depots/<appid>.depot` recording
the manifest GID you currently hold. ACCELA's / LumaDeck's update check compares
that against the current public GID from SteamCMD; if they differ it surfaces an
**"Update available"** badge. That's the cue to grab the new Hubcap zip.

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
