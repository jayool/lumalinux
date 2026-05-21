# lumalinux

Function-level depot key resolution hook for the Steam Linux client. Enables Steam's native "Install" button to work for depots whose decryption keys you have locally.

**Status:** v0.1.0 — alpha / POC. Designed to coexist with SLSsteam. Tested empirically: not yet.

## What this does

The standard Linux Steam piracy stack (SteaMidra Linux + SLSsteam) requires an external tool (DepotDownloaderMod) to actually download games, because SLSsteam only spoofs ownership at the API level — it doesn't intercept depot decryption keys. So Steam asks Valve for keys, Valve refuses for unowned content, and downloads fail.

`lumalinux` closes that gap by intercepting the depot key resolution function inside `steamclient.so` (the equivalent of what LumaCore does on Windows). When Steam asks for a key during install:

1. Steam internally calls `LoadDepotDecryptionKey(this, app_id, depot_id, out_buffer)`
2. `lumalinux` intercepts the call
3. If `depot_id` is in our local key store, write the key to `out_buffer` and return `EResult::OK`
4. Otherwise, fall through to Steam's original implementation (which will do the normal RPC to Valve)

Result: clicking "Install" in Steam UI on the Steam Deck works natively for depots you have keys for, without DepotDownloaderMod or any external tool.

## How it differs from existing tools

| | LumaCore (Windows) | SLSsteam (Linux) | lumalinux (Linux) |
|---|---|---|---|
| Ownership spoof | ✓ | ✓ | ✗ (relies on SLSsteam) |
| Depot key hook | ✓ | ✗ | ✓ |
| Steam UI Install button works | ✓ | ✗ | ✓ |
| Family Share bypass | ✗ | ✓ | ✗ (relies on SLSsteam) |

`lumalinux` is designed to coexist with SLSsteam, not replace it. SLSsteam handles ownership/licensing; lumalinux handles depot decryption.

## Requirements

- Linux x86_64 host with multilib support (or SteamOS)
- SLSsteam installed and configured (lumalinux assumes Steam thinks you "own" the apps you want to install)
- Local depot keys for the apps you want to install (extracted from SteamTools/LumaCore `.lua` files)
- Steam client must be the 32-bit Linux version (the standard one — there is no 64-bit Steam Linux client at time of writing)

## Build from source

```bash
# Install 32-bit toolchain
sudo apt install gcc-multilib g++-multilib cmake ninja-build
# (or equivalent on your distro)

# Configure and build
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja
```

The output is `build/liblumalinux.so`. If CMake fails to fetch libmem, see the troubleshooting section below.

## Install

```bash
# Close Steam first
./install.sh
```

The script:
1. Detects your Steam installation (native or Flatpak)
2. Copies `liblumalinux.so` to `~/.local/share/lumalinux/`
3. Patches `steam.sh` to add lumalinux to `LD_PRELOAD`
4. Creates `~/.config/lumalinux/keys.txt` (empty) if it doesn't exist
5. Backs up the original `steam.sh` to `steam.sh.bak`

To uninstall:
```bash
./install.sh --uninstall
```

## Populate keys

Edit `~/.config/lumalinux/keys.txt`. Format:

```
<depot_id>;<64 hex chars>
```

One key per line. Lines starting with `#` are comments.

Where to get keys: SteamTools/LumaCore `.lua` files contain them. Each `addappid(N, 1, "hex_key")` line gives you depot `N` with key `hex_key`. Convert to lumalinux format:

```
N;hex_key
```

You can use SteaMidra's `saved_lua/` directory as your source — same `.lua` files you use on Windows.

## Verify it's working

After running `install.sh` and starting Steam:

```bash
tail -f ~/.cache/lumalinux/lumalinux.log
```

You should see lines like:

```
[2026-05-21 14:32:01] [INFO] lumalinux v0.1.0 loading...
[2026-05-21 14:32:01] [INFO] KeyStore: loaded 5 key(s) from /home/deck/.config/lumalinux/keys.txt
[2026-05-21 14:32:02] [INFO] Patterns: steamclient.so loaded at 0xf6c00000
[2026-05-21 14:32:02] [INFO] Patterns: depot key function found at 0xf6f9ec00 (RVA 0x39ec00)
[2026-05-21 14:32:02] [INFO] DepotKey hook: INSTALLED (target=0xf6f9ec00, trampoline=0xf7a14000, 5 keys loaded)
```

Then try clicking "Install" on an unowned game (with key present in keys.txt). When the hook fires:

```
[2026-05-21 14:32:45] [INFO] LoadDepotKey: SERVED local key for depot 246621 (app 246620)
```

If you don't see "depot key function found", the byte pattern in `src/patterns.hpp` doesn't match your build of `steamclient.so`. Steam updates can change this. See troubleshooting below.

## Troubleshooting

### Pattern not found

Steam updates can shift internal functions. To re-extract the pattern:

1. Open `~/.steam/steam/linux64/steamclient.so` in Ghidra
2. Search → Defined Strings → "Failed to get decryption key for depot"
3. Click the result, then References → References to this address
4. Navigate to the function that uses this string. That's `LoadDepotDecryptionKey`.
5. Copy the first ~24 bytes of the function as a byte pattern, replacing GOT-relative offsets with `??`
6. Update `kDepotKeyFnPattern` in `src/patterns.hpp` and rebuild

### libmem fetch fails

If `cmake` fails during the libmem `FetchContent`, you can vendor libmem manually:

1. Get `liblibmem.a` from a working SLSsteam build (it's in `lib/` of their source repo, GPL-3.0 compatible with our license)
2. Get `libmem.h` and `libmem.hpp` from the same source's `include/libmem/`
3. Modify `CMakeLists.txt` to use these local files instead of FetchContent

### Hook installs but doesn't fire

Means the pattern matched a wrong function. Verify:
- Pattern matches exactly ONE address in the binary (use Ghidra search)
- The function at that address contains the EMsg 5438 reference (look for `mov [eax], 0x8000153e` or similar)
- The arg layout matches: `(this*, app_id, depot_id, out_buffer)`

If layout differs, adjust the hook function signature in `src/hooks/depot_key_hook.cpp`.

### Steam crashes on startup

If lumalinux causes Steam to crash:

1. Run `./install.sh --uninstall`
2. Steam launches normally (using the `steam.sh.bak` backup)
3. Check `~/.cache/lumalinux/lumalinux.log` for the last entries before crash
4. File an issue with the log

## Roadmap

This is v0.1.0. Only depot keys are hooked. Future versions may add:

- `LoadPackage` hook for package-level license injection
- `KeyValues_ReadAsBinary` hook for manifest pinning enforcement
- `MarkLicenseAsChanged` for license refresh without Steam restart
- `SpawnProcess` for online-fix.me game launch handling

If you want any of these specifically, open an issue describing the use case.

## License

GPL-3.0. See [LICENSE](LICENSE).

This project uses libmem (rdbo/libmem), also GPL-3.0.

## Acknowledgements

- LumaCore (Midrags/SFF/LumaCore) — Windows reference implementation that informed the function-level hook approach
- SLSsteam (AceSLS/SLSsteam) — Linux infrastructure precedent; lumalinux complements rather than replaces
- DepotDownloader (SteamRE/DepotDownloader) — flow reference for understanding Steam's depot key RPC
- SteamKit (SteamRE/SteamKit) — protocol definitions
- libmem (rdbo/libmem) — function detour library
