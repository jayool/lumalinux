#pragma once

// Read-only discovery probes for the real CPackageInfoCache::LoadPackage.
//
// v0.10.4 picked the wrong candidate (passed offline behaviour filter, but
// crashed Steam at runtime because it isn't actually LoadPackage). v0.10.6
// rolls the main hook back to v0.10.3's safe-but-inert wrapper and uses these
// probes to identify the right function from a live Steam session WITHOUT
// modifying anything.
//
// OFF by default. Opt in by exporting LUMA_LOADPKG_PROBE=1 in steam.sh.
// When active, installs a passthrough hook on each hardcoded candidate RVA
// and logs each invocation's arg0 fields (read-only). The real LoadPackage is
// the candidate that fires with [arg0+0]==0 during a PICS package load,
// with a sane CUtlVector header at [arg0+0x38].

namespace Hooks::LoadPackageProbe {
bool Install();
void Uninstall();
}
