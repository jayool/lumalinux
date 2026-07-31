# luatools (Windows) — plugin disassembly / source

The **luatools Steam plugin** — the Windows app (a [Millennium](https://steambrew.app)
plugin that runs inside the Steam client). Its shipped source, extracted so it
survives outside an ephemeral scratchpad. Third-party code, kept for **analysis
only** (how luatools resolves/applies fixes, downloads manifests, talks to its
APIs), to compare against LumaDeck's own backend.

This is the actual `luatools` plugin — **not** the Linux ports/forks
(luatools-moon, ltsteam-as-a-project, LuaToolsLinux). Two versions are kept:

| Dir | Version | Notes |
|---|---|---|
| `luatools-8.0.4/` | 8.0.4 | The upstream `luatools` plugin (vendored copy). |
| `luatools-9.0.1/` | 9.0.1 | A newer snapshot of the same plugin. |

## What's inside (each version)

- `public/luatools.js` — the **frontend bundle** (the "disassembly": the shipped,
  minified plugin JS; 323 KB @ 8.0.4, 585 KB @ 9.0.1). This is the main artifact.
- `.millennium/Dist/{index.js,webkit.js}` — the compiled Millennium dist.
- `backend/*.lua` — the Lua backend: `main.lua`, `fixes.lua`, `downloads.lua`,
  `api_manifest.lua`, `http_client.lua`, `config.lua`, `steam_utils.lua`,
  `auto_update.lua`, `settings/`, etc.
- `backend/scripts/downloader.ps1` + `restart_steam.cmd` — the **Windows**-side
  helpers (PowerShell / cmd); `Windows_NT` branches live in `fixes.lua` /
  `downloads.lua` / `auto_update.lua`.
- `backend/locales/*.json` (~40 langs), `public/themes/*.css`, `plugin.json`.

## Key finding — the online-fix / fixes API

From `backend/fixes.lua`: `fixes.check_for_fixes(appid)` GETs the **public** index
`https://index.luatools.work/fixes-index.json` → `{ genericFixes: [...appids],
onlineFixes: [...appids] }`, then builds the download URL
`https://files.luatools.work/OnlineFix1/<appid>.zip` (online) or
`GameBypasses/<appid>.zip` (generic). Apply = download the zip via
`backend/scripts/downloader.ps1`/`.sh` and extract over the install.

LumaDeck's `fixes.py` instead HEADs the (private, 403) bucket directly — it could
use this public index instead. See the Lua backend for the manifest/download and
account/API flows too.

## Provenance

Extracted in a prior session (source dated 2026-07). Kept verbatim; the `.js` is
the shipped bundle (minified) — say the word and I can beautify/deobfuscate it
into readable form.
