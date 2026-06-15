#pragma once

namespace Hooks::PackageZeroFinder {

// Walks Steam's internal package cache to locate the PackageInfo* for
// PackageId == 0 and seeds our depots into its AppIdVec via the shared
// Hooks::LoadPackage::InjectDepots(). This is the SOLE injection path — the
// LoadPackage hook no longer injects.
//
// ON BY DEFAULT. Gated by env vars:
//   (unset) / "inject"   → walk + log + inject (default)
//   "diag"               → read-only walk + log, no injection
//   LUMA_NO_PKG0_FINDER  → disable the finder entirely
//
// Rationale: the LoadPackage hook is passive — it only fires when Steam CALLS
// LoadPackage. When Steam keeps PackageId=0 cached from a previous session it
// never re-calls LoadPackage, so the hook never injects and the install breaks.
// This worker doesn't wait for the call: it finds package 0 in the cache
// directly and injects there. It polls forever (a slow login must not break the
// install) and re-injects if Steam rebuilds the package after a re-login.
//
// Build-robust addressing: the cache pointer lives at GOTbase + X. X shifts
// across Steam builds (GOT layout), so the worker derives everything at
// RUNTIME rather than hardcoding it:
//   - GOTbase from the GMRC function prologue (E8 thunk; 05 add eax).
//   - X by scanning the r-x mapping for `lea [GOT+X]; mov; mov [+0xc58]`,
//     anchored on the STABLE tree-root offset 0xc58.
// The tree offsets themselves (root@0xc58, nodes@0xc6c, node 0x18 bytes,
// packageId@0x10, PackageInfo*@0x14) are stable class layout — verified
// identical on the 7c4ac73e and db0d79c2 builds.
//
// Safe even if a future layout change breaks our assumptions: every
// dereference goes through an /proc/self/maps readability check, so a bad
// address yields nullptr (a logged diagnostic) rather than SIGSEGV. Inject
// mode additionally requires InjectDepots()'s AppIdVec sanity checks to pass
// before any write.
void Start();

} // namespace Hooks::PackageZeroFinder
