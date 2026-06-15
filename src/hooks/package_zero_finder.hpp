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
// steamclient.so maps in (worker polls /proc/self/maps once per second
// then sleeps 200 ms before installing). In the 7c4ac73e steamclient
// build, ReadFromDisk → LoadPackage(PackageId=0) runs DURING that gap, so
// the hook never sees the load. This worker complements the hook by
// finding package 0 in the cache that ReadFromDisk already populated and
// injecting there directly — no hook timing required.
//
// The cache offsets it uses (cacheGlobal=+0x3a1bc, rootIdx=+0xc58,
// nodes=+0xc6c, node {0x18 bytes, packageId@0x10, PackageInfo*@0x14})
// were verified by decompiling the accessor at va 0xf7d4e0 and confirmed
// by counting 2405 distinct uses of `lea reg, [GOTbase + 0x3a1bc]` across
// the binary.
//
// Safe even if the cache layout we deduced is wrong: every dereference
// goes through an /proc/self/maps-based readability check, so an offset
// pointing into garbage just makes the walk return nullptr (logged warn)
// rather than SIGSEGV. Inject mode further requires the AppIdVec sanity
// checks in InjectDepots() to pass before writing anything.
void Start();

} // namespace Hooks::PackageZeroFinder
