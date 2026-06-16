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
    if (const char* xdgRun = std::getenv("XDG_RUNTIME_DIR"); xdgRun && *xdgRun) {
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

void Write() {
    std::vector<HookRecord> snapshot;
    std::string blocked;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        snapshot = g_hooks;
        blocked  = g_blocked;
    }

    std::string path = ResolvePath();
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        Log::Warn("Status: cannot open %s for writing", path.c_str());
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
    std::fclose(f);

    Log::Info("Status: wrote %s (%zu hooks, blocked=%s)",
              path.c_str(), snapshot.size(),
              blocked.empty() ? "no" : blocked.c_str());
}

} // namespace Status
