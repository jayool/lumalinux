# Maintenance: updating after a Steam or SLSsteam update

Two distinct breakage cases. The symptoms tell them apart.

## A) Steam client update → a hook's byte pattern stops matching

**Symptom**: the startup toast says `N/4 hooks — <HOOK> FAILED`, and the
log (`~/.cache/lumalinux/lumalinux.log`) contains `pattern NOT FOUND`
for the failing hook.

**Cause**: Valve updated `steamclient.so` and the bytes lumalinux expects
at the start of the affected function changed. The hook can't install.

**Fix**:

1. **Grab the new `steamclient.so`** from the Deck:
   ```sh
   cp ~/.local/share/Steam/linux32/steamclient.so /tmp/
   ```

2. **Re-derive the patterns** with the maintainer script (requires Ghidra
   installed locally):
   ```sh
   # one-time import (analysis takes ~5-15 min):
   analyzeHeadless <proj> sc -import /tmp/steamclient.so

   # re-derive against the imported binary:
   analyzeHeadless <proj> sc -process steamclient.so \
       -noanalysis -scriptPath tools -postScript derive_patterns.py
   ```
   String-anchored hooks (BuildDep, GMRC) auto-derive from their anchor
   strings. Anchorless hooks (DepotKey, LoadPackage) auto-validate the
   current pattern; if they no longer match, they get flagged for manual
   re-derive — see [`RESEARCH.md`](RESEARCH.md) §8.1.

3. **Paste** the printed patterns into `src/patterns.hpp`, rebuild
   (`./tools/fetch_libmem.sh && cmake + make` if needed), and redeploy
   with `./install.sh`.

Most updates only move the anchored hooks, which the script fixes
automatically — usually copy-paste-rebuild, not a full RE session.

## B) SLSsteam update → launcher script regenerated

**Symptom**: Steam starts with no toast at all. lumalinux is not loaded
— no banner in `~/.cache/lumalinux/lumalinux.log` from that session.

**Cause**: the Headcrab Updater (the SLSsteam updater shipped with
enter-the-wired) rewrites `~/.local/share/Steam/steam.sh` whenever it
runs, replacing it with a fresh copy from upstream `h3adcr-b-modul3s`.
The block lumalinux's installer added goes with it.

**Fix**: re-run the lumalinux installer.

```bash
curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/install.sh | bash
```

The deployed `.so` and `keys.txt` survive — only the launcher block
needs re-adding.

> This happens on every Headcrab Updater run. Until upstream Headcrab
> grows a hook point for third-party `LD_PRELOAD` additions (or until
> LumaDeck's "Install / Reinstall Dependencies" action wraps both
> Headcrab and lumalinux's installer in sequence), the re-run is manual.
