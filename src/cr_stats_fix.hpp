#pragma once

// CloudRedirect stats-sync cold-start fix (LD_PRELOAD symbol interposition).
//
// THE BUG (CloudRedirect ≤ 2.6.5, Linux, stats_sync_enabled + sync_achievements
// on). When a game asks Steam for its stats (`Player.GetUserStats#1`),
// CloudRedirect answers for every AdditionalApps game from its own store instead
// of letting the request out. Its handler compares the crc the client sent with
// the crc of its store and, when they match, replies "up to date": a 2-byte body
// carrying only the crc. A game CloudRedirect has never seen has an EMPTY store,
// whose crc is 0 — and a client with no local stats also sends crc 0. Equal, so
// "up to date", so the game never receives its achievement schema, so it can
// never unlock anything. And the store only fills from the native blob Steam
// writes AFTER receiving that schema — which CloudRedirect just swallowed. The
// game is stuck at zero forever. Field data (Deck, 2026-09-18/20): five launches
// of one game, all answered `crc-only no-op (2 bytes)`, zero achievements; four
// of five managed games in that state; the one that worked had been played with
// the sync off first. See docs/cloudredirect.md.
//
// CloudRedirect's own code has the right branch: when the handler's body is
// EMPTY, `TryHandleGetUserStats` returns false and the request goes on to
// SLSsteam (schema borrow from a reviewer) and Steam. It just never takes it,
// because the crc-only body is 2 bytes, not 0.
//
// THE FIX. cloud_redirect.so is linked with `-s` but WITHOUT -fvisibility=hidden,
// so every function is still in .dynsym, and its internal calls go through the
// PLT (seen in the 2.5.2 binary; relocation confirmed in 2.6.5 on-device). So
// this .so — which the wrapper puts BEFORE cloud_redirect.so in LD_PRELOAD —
// defines `StatsHandlers::HandleGetUserStats` itself. The dynamic linker binds
// CloudRedirect's call to ours; ours forwards to the real one and, when the
// result is exactly the 2-byte crc-0 body, clears it. CloudRedirect then takes
// its own passthrough branch (and logs `store returned empty -> passthrough`).
// Nothing in CloudRedirect's memory or code is patched. Any other body — the
// "up to date" with a non-zero crc, or the full schema+stats seeded from the
// cloud on a second device — is untouched.
//
// SELF-CHECKS instead of a per-release version list. The fix is armed only if:
//   1. the exact mangled symbol resolves in cloud_redirect.so (its name encodes
//      the parameter types, so a signature change un-arms us);
//   2. the real function's machine code (bounded by its .dynsym st_size)
//      contains a `ret $4` epilogue — i.e. it still returns its result through a
//      hidden pointer the callee pops (i386 sret), which is what the stub
//      forwards;
//   3. our definition is the one the process binds (RTLD_DEFAULT resolves to our
//      stub) — otherwise LD_PRELOAD order was changed and we'd be inert anyway.
// And per call, the result is touched only when the first 12 bytes look like a
// live std::vector<uint8_t> (begin ≤ end ≤ cap, size 2, small capacity, memory
// readable) holding exactly `10 00`. Anything else is forwarded untouched.
// Because the symbol is defined by this .so unconditionally, "disabled"
// (LUMA_NO_CR_STATS_FIX, or any check failing) means "forward without looking",
// never "absent".
namespace CrStatsFix {

enum class Result {
    Applied,    // all checks passed; the stub post-processes results
    Disabled,   // LUMA_NO_CR_STATS_FIX set, or cloud_redirect.so not loaded
    Failed,     // CloudRedirect loaded but a self-check failed (reason logged)
};

// Resolve the real function, run the self-checks, arm the stub. Call once from
// InstallHooks(). Safe if the stub already fired (it resolves lazily on its own).
Result Init();

} // namespace CrStatsFix
