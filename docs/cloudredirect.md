# Running lumalinux side-by-side with CloudRedirect

CloudRedirect (Selectively11/CloudRedirect) targets the **cloud-save RPC path**
in `steamclient.so` via vtable swaps; lumalinux targets the **content / depot
path** via inline byte hooks. The two hooked sets are disjoint, the mechanisms
don't overlap, and they're designed to run side by side.

## Install CloudRedirect

The h3adcr-b installer used to bundle CloudRedirect, but **its bundled copy is
stale**. Quoting Selectively11 in the v2.0.5 release notes (~Apr 2026):

> *Linux users, read! This release requires a change to your Steam.sh! […]
> Switched Linux injection from LD_AUDIT to LD_PRELOAD for broader distro
> compatibility. All distros/setups should work now. If you manually edit your
> steam.sh, remove cloud_redirect.so from your LD_AUDIT and specify it as an
> LD_PRELOAD.*

The supported path is to install the CloudRedirect **flatpak** from its own
upstream repo (gives you the up-to-date `cloud_redirect.so` plus the
configuration UI for the cloud provider), and add the `LD_PRELOAD` by hand.

## The combined LD_PRELOAD line

`LD_PRELOAD` entries are separated by `:`. Both `cloud_redirect.so` and
`liblumalinux.so` should be on the same line in `/usr/bin/steam`, just before
the final `exec`:

```bash
export LD_PRELOAD="/home/deck/.local/share/CloudRedirect/cloud_redirect.so:/home/deck/.local/share/lumalinux/liblumalinux.so${LD_PRELOAD:+:}${LD_PRELOAD:-}"
```

The `${LD_PRELOAD:+:}${LD_PRELOAD:-}` suffix preserves any existing entries
the runtime might add later.

Order between CloudRedirect and lumalinux doesn't matter for correctness
(they hook different functions), only for symbol-resolution conflicts — none
observed between these two.

## Verifying both loaded

After restarting Steam:

- **lumalinux log**: `~/.cache/lumalinux/lumalinux.log` should show
  `4/4 hooks active`.
- **CloudRedirect log**: `~/.config/CloudRedirect/cloud_redirect.log` should
  end with `CloudRedirect initialized successfully`.

If only one of the two appears, the `LD_PRELOAD` got truncated somewhere
upstream of the Steam process — most often by a re-run of SLSsteam's
`setup.sh` or by a SteamOS update overwriting `/usr/bin/steam`.

## About h3adcr-b's bundled wrapper

If you previously used `h3adcr-b`'s `cr-testbranch`, you may still have a
wrapper at `~/.local/share/Steam/steam.sh` with `INJECT_SLS` / `INJECT_CR`
helpers. **It's not needed with the manual `/usr/bin/steam` flow.** Delete it
(or rename to `.bak`) so it doesn't conflict with the exports above. Steam
will regenerate a vanilla `~/.local/share/Steam/steam.sh` on next launch if
needed.
