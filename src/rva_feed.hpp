#pragma once

#include <cstdint>

// RVA-first hook resolution from the per-build feed (res/rvas/<sha256>.yaml).
//
// Resolve() returns a live runtime address for a hook by looking up the CI-
// published RVA for THIS steamclient.so build (keyed by its SHA-256) and
// translating it via VaddrXlate — or 0 to fall back to the byte pattern. Because
// the RVA is a precomputed offset, it is robust to a recompile that shifts the
// prologue (which breaks byte patterns). See docs/rva-feed-design.md.
//
// The feed is fetched from the repo (hardcoded URL) with a disk cache, mirroring
// update.cpp. A build with no feed file — or a hook not listed in it — resolves
// to 0 (fall back). There is deliberately NO runtime override of the feed source:
// see the note on obtainYaml() in rva_feed.cpp.
namespace RvaFeed {

// Runtime address for `hookName` via the feed, or 0 to fall back to the byte
// pattern. Lazily loads + parses the feed for the current build on first call.
uintptr_t Resolve(const char* hookName);

} // namespace RvaFeed
