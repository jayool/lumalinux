# Running lumalinux side-by-side with CloudRedirect

CloudRedirect (Selectively11/CloudRedirect) targets the **cloud-save RPC
path** in `steamclient.so` via vtable swaps; lumalinux targets the
**content / depot path** via inline byte hooks. The two hooked sets are
disjoint, the mechanisms don't overlap, and they're designed to run side
by side.

## Install order

CloudRedirect goes in **before** lumalinux. lumalinux's installer detects
CloudRedirect's `.so` if present and arranges for both to load.

1. Install ACCELA + SLSsteam (`curl … enter-the-wired | bash`). This
   creates `~/.config/SLSsteam/config.yaml` with `DisableCloud: yes` by
   default.

2. **Enable CloudRedirect in SLSsteam's config** — edit
   `~/.config/SLSsteam/config.yaml` and change the `DisableCloud` line
   from `yes` to `no`.

3. **Install CloudRedirect**:

   ```bash
   curl -fsSL https://headcrab.pages.dev | bash
   ```

   This downloads `cloud_redirect.so` to
   `~/.local/share/CloudRedirect/cloud_redirect.so`, installs the
   configuration Flatpak (`org.cloudredirect.CloudRedirect`), and
   regenerates `~/.local/share/Steam/steam.sh` to the CR-aware variant
   (with `INJECT_CR=LD_PRELOAD=…cloud_redirect.so`).

4. **Open the CloudRedirect Flatpak app once** to sign into your cloud
   provider.

5. **Install lumalinux**:

   ```bash
   curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/install.sh | bash
   ```

## How they coexist in `steam.sh`

The CR-aware `steam.sh` from Headcrab has (paraphrased):

```bash
GameLauncher(){
    CheckClientInfo
    export $INJECT_SLS                         # LD_AUDIT=… SLSsteam
    export $INJECT_CR                          # LD_PRELOAD=…cloud_redirect.so
    source $STEAM_CLIENT "$@"
}
```

`export $INJECT_CR` sets `LD_PRELOAD` to `cloud_redirect.so` only — it
does **not** preserve whatever was previously in `LD_PRELOAD` (this is an
upstream-Headcrab behaviour we work around).

lumalinux's installer inserts a block right before `source $STEAM_CLIENT`:

```bash
# >>> lumalinux launcher patch >>> (managed by install.sh - do not edit)
export LD_PRELOAD="$HOME/.local/share/lumalinux/liblumalinux.so${LD_PRELOAD:+:}${LD_PRELOAD:-}"
# <<< lumalinux launcher patch <<<
```

The `${LD_PRELOAD:+:}${LD_PRELOAD:-}` suffix preserves whatever
`LD_PRELOAD` already has — in the CR case, that's
`cloud_redirect.so`. Final result inside the Steam process:

```
LD_PRELOAD=/home/<user>/.local/share/lumalinux/liblumalinux.so:/home/<user>/.local/share/CloudRedirect/cloud_redirect.so
```

Both `.so`s load. lumalinux comes first in the chain so its symbols
shadow anything CR also provides, but the two hook disjoint functions in
`steamclient.so` — no symbol-resolution conflicts observed.

## Verifying both loaded

After restarting Steam:

- **lumalinux**: `~/.cache/lumalinux/lumalinux.log` ends with
  `4/4 hooks active`.
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

The Headcrab Updater regenerated `steam.sh` and the lumalinux block went
with it. Re-run the lumalinux installer; the CR install survives
untouched.

```bash
curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/install.sh | bash
```
