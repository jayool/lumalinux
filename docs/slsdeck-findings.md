# Findings — SLSDeck vs LumaDeck — **SUPERSEDED**

> **This document has been withdrawn. See
> [`slsdeck-analysis.md`](slsdeck-analysis.md).**

The 2026-07-22 investigation recorded here analysed `Kaal31/slsdeck` v0.01: a
~6-commit "stripped" build, explicitly *no hypervisor / no Denuvo*, read over
`raw.githubusercontent` because the repository was outside the session's GitHub
scope.

**That repository no longer exists.** The current history begins on 2026-08-20
with a squashed code drop at v0.9.59, under a new name (`SLSDeckUniversal`), with
the `root` flag, and with the hypervisor/Denuvo build that this document recorded
as announced-but-unpublished now being the published one.

Its conclusions are void rather than stale. In particular, three of them are
directly contradicted by the current code:

- *"Zero traces… independent convergence, not a fork/copy"* — LumaDeck is now
  named in their source and commit history (`slsdeck-analysis.md` §1, §9.2).
- *"gamescope `sessions.d` injection… Steam cannot overwrite it"* — they now
  patch `steam.sh` and re-patch it when Steam reverts it (§3.1).
- *"❌ requires Steam reload"* and the native-achievements gap — both closed
  (§2.4).

Retained only so the withdrawal is on the record.
