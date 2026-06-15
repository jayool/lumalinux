#pragma once

namespace Hooks::PackageZeroFinder {

// Walks Steam's internal package cache to locate the PackageInfo* for
// PackageId == 0, then either logs what it found ("diag" mode) or invokes
// the shared Hooks::LoadPackage::InjectDepots() to seed our depots into
// AppIdVec ("inject" mode).
//
// Gated by env var LUMA_PKG0_FINDER:
//   unset / empty   → does nothing (default; finder is fully opt-in)
//   "diag"          → read-only walk + log the PackageInfo it finds
//   "inject"        → walk + log + call InjectDepots()
//
// Rationale: lumalinux's LoadPackage hook installs ~1.2 s after
// steamclient.so maps in. In recent steamclient builds, ReadFromDisk →
// LoadPackage(PackageId=0) runs DURING that gap, so the hook never sees the
// load. This worker complements the hook by finding package 0 in the cache
// that ReadFromDisk already populated and injecting there directly — no hook
// timing required.
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
