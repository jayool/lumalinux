#pragma once

// Status snapshot written once after InstallHooks() so external UIs (LumaDeck)
// can tell whether lumalinux is actually healthy in this Steam session, not
// just whether the .so file exists on disk. Deliberately minimal — see #5.

namespace Status {

enum Outcome {
    INSTALLED,   // hook installed successfully
    FAILED,      // hook's Install() returned false (pattern miss or LmHook failure)
    DISABLED,    // user opted out via env var (LUMA_NO_*)
};

// Record a hook's outcome. Safe to call from any thread; resolved at Write().
void RecordHook(const char* name, Outcome outcome);

// Write the snapshot to $XDG_RUNTIME_DIR/lumalinux/status.json (or
// $HOME/.cache/lumalinux/status.json as fallback). Best-effort: on failure,
// logs a warning and continues.
void Write();

} // namespace Status
