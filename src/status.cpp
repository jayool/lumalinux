// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 jayool — https://github.com/jayool/lumalinux
#include "status.hpp"
#include "log.hpp"
#include "version.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

struct HookRecord {
    std::string name;
    Status::Outcome outcome;
};

std::mutex g_mtx;
std::vector<HookRecord> g_hooks;
std::string g_blocked;  // empty when not blocked

const char* OutcomeStr(Status::Outcome o) {
    switch (o) {
        case Status::INSTALLED: return "installed";
        case Status::FAILED:    return "failed";
        case Status::DISABLED:  return "disabled";
    }
    return "unknown";
}

// Same fallback chain as the log: XDG_RUNTIME_DIR (preferred, tmpfs cleared at
// reboot — exactly the right semantics for "live session state") → XDG_CACHE_HOME
// → $HOME/.cache → /tmp. mkdir -p the directory; ignore errors.
std::string ResolvePath() {
    std::string dir;
#ifdef LUMA_X86_64
    // steamrt64 client: the library runs inside the Steam Runtime container,
    // where XDG_RUNTIME_DIR is set but is NOT the host's /run/user/<uid> tree —
    // a file written there is invisible to LumaDeck (docs/steam-rt3-x64.md
    // §1.3). $HOME is bind-mounted, so the cache fallback below is the path
    // both sides see; LumaDeck already lists ~/.cache/lumalinux/status.json
    // among its candidates (backend/paths.py find_lumalinux_status_path).
    const char* xdgRun = nullptr;
#else
    const char* xdgRun = std::getenv("XDG_RUNTIME_DIR");
#endif
    if (xdgRun && *xdgRun) {
        dir = xdgRun;
    } else if (const char* xdgCache = std::getenv("XDG_CACHE_HOME"); xdgCache && *xdgCache) {
        dir = xdgCache;
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        dir = std::string(home) + "/.cache";
    } else {
        dir = "/tmp";
    }
    dir += "/lumalinux";
    std::string cmd = "mkdir -p '" + dir + "' 2>/dev/null";
    int rc = std::system(cmd.c_str());
    (void)rc;
    return dir + "/status.json";
}

std::string SiblingPath(const char* name) {
    std::string p = ResolvePath();
    size_t slash = p.rfind('/');
    return p.substr(0, slash + 1) + name;
}

// Open `<path>.tmp` for writing; CommitAtomic() renames it over `path`.
// LumaDeck reads both files while Steam runs (status.json on every component
// poll, gmrc.json in the pin pass); fopen(path, "w") truncated the file first
// and a read in that window saw an empty JSON, which for gmrc.json meant
// "absent" = providers up and could release pins. A rename is atomic: readers
// see the old file whole or the new one whole.
FILE* OpenAtomic(const std::string& path) {
    return std::fopen((path + ".tmp").c_str(), "w");
}

bool CommitAtomic(FILE* f, const std::string& path) {
    std::fclose(f);
    std::string tmp = path + ".tmp";
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

std::string IsoUtcNow() {
    std::time_t now = std::time(nullptr);
    std::tm tm_utc;
    gmtime_r(&now, &tm_utc);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    return buf;
}

} // namespace

namespace Status {

void RecordHook(const char* name, Outcome outcome) {
    if (!name) return;
    std::lock_guard<std::mutex> lock(g_mtx);
    for (auto& h : g_hooks) {
        if (h.name == name) { h.outcome = outcome; return; }
    }
    g_hooks.push_back({name, outcome});
}

void SetBlocked(const char* reason) {
    std::lock_guard<std::mutex> lock(g_mtx);
    g_blocked = reason ? reason : "";
}

void RecordGmrc(bool providersUp) {
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    std::string path = SiblingPath("gmrc.json");
    FILE* f = OpenAtomic(path);
    if (!f) return;
    std::fprintf(f, "{\"providers\": \"%s\", \"at\": \"%s\"}\n",
                 providersUp ? "up" : "down", IsoUtcNow().c_str());
    CommitAtomic(f, path);
}

void Write() {
    std::vector<HookRecord> snapshot;
    std::string blocked;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        snapshot = g_hooks;
        blocked  = g_blocked;
    }

    std::string path = ResolvePath();
    FILE* f = OpenAtomic(path);
    if (!f) {
        Log::Warn("Status: cannot open %s.tmp for writing", path.c_str());
        return;
    }

    // Hand-rolled JSON — payload is fixed and small (no dep on a JSON lib).
    std::fprintf(f, "{\n");
    std::fprintf(f, "  \"version\": \"%s\",\n", LUMALINUX_VERSION_STRING);
    std::fprintf(f, "  \"pid\": %d,\n", getpid());
    std::fprintf(f, "  \"started_at\": \"%s\",\n", IsoUtcNow().c_str());
    // "blocked": null when we got past the gate; a short reason ("hash_unverified")
    // when InstallHooks() refused to hook (no hook ever ran, so hooks={}).
    if (blocked.empty()) {
        std::fprintf(f, "  \"blocked\": null,\n");
    } else {
        std::fprintf(f, "  \"blocked\": \"%s\",\n", blocked.c_str());
    }
    std::fprintf(f, "  \"hooks\": {");
    for (size_t i = 0; i < snapshot.size(); ++i) {
        std::fprintf(f, "%s\n    \"%s\": \"%s\"",
                     i == 0 ? "" : ",",
                     snapshot[i].name.c_str(),
                     OutcomeStr(snapshot[i].outcome));
    }
    std::fprintf(f, "%s}\n", snapshot.empty() ? "" : "\n  ");
    std::fprintf(f, "}\n");
    if (!CommitAtomic(f, path)) {
        Log::Warn("Status: cannot rename %s.tmp into place", path.c_str());
        return;
    }

    Log::Info("Status: wrote %s (%zu hooks, blocked=%s)",
              path.c_str(), snapshot.size(),
              blocked.empty() ? "no" : blocked.c_str());
}

} // namespace Status
