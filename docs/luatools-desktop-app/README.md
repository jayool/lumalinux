# LuaTools — standalone desktop app (Windows) — RE archive

The **standalone LuaTools Windows application** from
[`madoiscool/LuaTools`](https://github.com/madoiscool/LuaTools) — a **.NET 8 WPF**
desktop app (namespace `LuaToolsGui`), packaged with **Velopack** (the Squirrel
successor). This is the actual `.exe` app, NOT the Millennium plugin (that lives in
`docs/luatools-windows/`) and NOT the Linux ports.

Release **v1.2.6** (2026-07-23). Kept for **analysis only** — how the app talks to
lua.tools, applies fixes, unlocks DLC, injects the plugin, etc.

## Files

| File | What |
|---|---|
| `LuaTools-1.2.6-win-Portable.zip` | The exact shipped portable app (6.6 MB). The source of truth for a full decompile. |
| `LuaTools.dll` | The app's managed assembly (965 KB) — the **decompile target**. |
| `LuaTools.exe`, `LuaTools.deps.json` | Host + dependency manifest. |
| `LuaTools-1.2.6-types-methods.txt` | Every type (473) + methods, from metadata (dnfile). |
| `LuaTools-1.2.6-strings.txt` | All string literals (1458), incl. every URL/endpoint. |

## Full C# decompile

A proper decompile-to-C# needs the .NET runtime, which this environment's egress
policy **blocks** (`builds.dotnet.microsoft.com` → 403, org policy). So it wasn't
done here. Do it on a normal machine against `LuaTools.dll`:

```
# dnSpy (GUI, Windows) — open LuaTools.dll, or:
dotnet tool install -g ilspycmd
ilspycmd -p -o luatools-src LuaTools.dll
```

The type map + strings below already reveal the behaviour without decompiling.

## What it does (from metadata + strings)

- **.NET 8 WPF** (`Wpf.Ui`, `CommunityToolkit.Mvvm`, `Markdig.Wpf`); updater =
  `Velopack`. Views: Home, Fixes, Manage, Mode, Download, Plugin, Settings,
  Onboarding, DropZone. Services: `LuaToolsApiClient`, `AuthService`,
  `GithubProxy`, `HubcapService`, `SteamDepotInfo`, `HttpServerService`,
  `PluginInstallerService`, `PluginAddService`, `ProtocolService`,
  `ProxiedFileDownloader`, `CoverCache`, `SteamAppListCache`.

- **lua.tools API surface** (base host built at runtime; paths from the strings):
  - `/api/denuvo/fixes?appid=` — fixes catalogue for an appid
  - `/api/denuvo/download?fix=` — download a fix
  - `/api/denuvo/listings` — all listings
  - `/api/dlc/info?appid=`, `/api/dlc/generate?appid=` — DLC unlocks
  - `/api/manifest/download?appid=`, `/api/v1/manifest/` — manifest download
  - `/api/v1/user/stats?api_key=`, `/api/me/supporter-status` — user/supporter
  - `/api/v1/status/`, `/api-list`
  - `/auth/v1/authorize?provider=discord` — **Discord OAuth** (Supabase auth); the
    app also accepts a `/login`-in-Discord code paste.

- **Local HTTP server** `http://127.0.0.1:6767` — backs the "Add via LuaTools"
  button injected into Steam store pages (`GetAddViaLuaToolsStatus`, `HandleAdd`).

- **Plugin bridge**: installs/updates the LuaTools **Millennium plugin**, injecting
  `public/luatools.js` (`Loaded luatools.js ({Length} bytes)`,
  `real.callServerMethod=... ltCall`). Can disable Millennium's own copy so
  LuaLoader injects instead.

- **Hubcap** integration (`HubcapService`, API-key gated) for manifest/key stats;
  regex helpers (`AppIdRegex`, `AddAppIdRegex`, `SetManifestRegex`, …) parse the
  installed `.lua` files.

## Provenance

Downloaded from the upstream release `v1.2.6` in this session and archived here so
it isn't lost. Third-party binary + derived metadata; kept as a reference snapshot.
