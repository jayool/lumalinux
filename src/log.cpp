#include "log.hpp"

#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <mutex>
#include <atomic>
#include <string>
#include <cstdlib>
#include <strings.h>   // strcasecmp
#include <unistd.h>
#include <pwd.h>

namespace {

FILE* g_logFile = nullptr;
std::mutex g_logMutex;

// Severity ordering: lower = more important. A line is written only if its
// severity <= the active threshold (g_logLevel). Default is INFO, so the noisy
// per-call DEBUG traces (e.g. BuildDepotDependency on every depot query) are
// suppressed unless the user opts in with LUMA_LOG_LEVEL=debug.
enum Severity { SEV_ERROR = 0, SEV_WARN = 1, SEV_INFO = 2, SEV_DEBUG = 3 };
std::atomic<int> g_logLevel{SEV_INFO};

const char* LevelName(int sev) {
    switch (sev) {
        case SEV_ERROR: return "error";
        case SEV_WARN:  return "warn";
        case SEV_DEBUG: return "debug";
        default:        return "info";
    }
}

// LUMA_LOG_LEVEL: error|warn|info|debug (case-insensitive) or 0..3.
// Unset / unrecognised → INFO.
int ParseLogLevel() {
    const char* e = std::getenv("LUMA_LOG_LEVEL");
    if (!e || !*e) return SEV_INFO;
    if (e[0] >= '0' && e[0] <= '9') {
        int n = std::atoi(e);
        if (n < SEV_ERROR) n = SEV_ERROR;
        if (n > SEV_DEBUG) n = SEV_DEBUG;
        return n;
    }
    if (strcasecmp(e, "error") == 0)                              return SEV_ERROR;
    if (strcasecmp(e, "warn") == 0 || strcasecmp(e, "warning") == 0) return SEV_WARN;
    if (strcasecmp(e, "info") == 0)                              return SEV_INFO;
    if (strcasecmp(e, "debug") == 0)                             return SEV_DEBUG;
    return SEV_INFO;
}

std::string ResolveLogPath() {
    // Prefer XDG_CACHE_HOME, fall back to $HOME/.cache, then /tmp
    const char* xdgCache = std::getenv("XDG_CACHE_HOME");
    std::string dir;
    if (xdgCache && *xdgCache) {
        dir = xdgCache;
    } else {
        const char* home = std::getenv("HOME");
        if (!home || !*home) {
            if (passwd* pw = getpwuid(getuid())) {
                home = pw->pw_dir;
            }
        }
        if (home && *home) {
            dir = std::string(home) + "/.cache";
        } else {
            dir = "/tmp";
        }
    }

    dir += "/lumalinux";
    // mkdir -p (best-effort, ignore errors)
    std::string cmd = "mkdir -p '" + dir + "' 2>/dev/null";
    int rc = std::system(cmd.c_str());
    (void)rc;

    return dir + "/lumalinux.log";
}

void WriteLine(int severity, const char* level, const char* fmt, va_list ap) {
    if (!g_logFile) return;
    // Level filter — cheap, lock-free, evaluated before taking the mutex.
    if (severity > g_logLevel.load(std::memory_order_relaxed)) return;

    std::lock_guard<std::mutex> lock(g_logMutex);

    std::time_t now = std::time(nullptr);
    std::tm tm_local;
    localtime_r(&now, &tm_local);

    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_local);

    std::fprintf(g_logFile, "[%s] [%s] ", ts, level);
    std::vfprintf(g_logFile, fmt, ap);
    std::fputc('\n', g_logFile);
    std::fflush(g_logFile);
}

} // namespace

namespace Log {

void Init() {
    // Steam calls into our hooks from multiple threads; two of them can race
    // into Init() before g_logFile is set. Serialize the check-and-open under
    // g_logMutex so we open exactly once (a double-open would leak the first
    // FILE* and could interleave the banner). The banner itself is emitted
    // OUTSIDE the lock — Info()->WriteLine() re-takes g_logMutex, and our mutex
    // is non-recursive, so logging while holding it would self-deadlock.
    bool opened = false;
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (g_logFile) return;
        g_logLevel.store(ParseLogLevel(), std::memory_order_relaxed);
        std::string path = ResolveLogPath();
        g_logFile = std::fopen(path.c_str(), "a");
        opened = (g_logFile != nullptr);
    }
    if (opened) {
        Info("=== lumalinux loaded (pid=%d, log level=%s) ===",
             getpid(), LevelName(g_logLevel.load(std::memory_order_relaxed)));
    }
}

void Shutdown() {
    if (g_logFile) {
        Info("=== lumalinux shutdown ===");
        std::fclose(g_logFile);
        g_logFile = nullptr;
    }
}

void Info(const char* fmt, ...)  { va_list ap; va_start(ap, fmt); WriteLine(SEV_INFO,  "INFO",  fmt, ap); va_end(ap); }
void Warn(const char* fmt, ...)  { va_list ap; va_start(ap, fmt); WriteLine(SEV_WARN,  "WARN",  fmt, ap); va_end(ap); }
void Error(const char* fmt, ...) { va_list ap; va_start(ap, fmt); WriteLine(SEV_ERROR, "ERROR", fmt, ap); va_end(ap); }
void Debug(const char* fmt, ...) { va_list ap; va_start(ap, fmt); WriteLine(SEV_DEBUG, "DEBUG", fmt, ap); va_end(ap); }

void Notify(const char* fmt, ...) {
    char msg[512];
    va_list ap; va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    // Always log it (at INFO so it survives the default level).
    Info("NOTIFY: %s", msg);

    if (std::getenv("LUMA_NO_NOTIFY")) return;

    // Neutralize shell metacharacters (messages are internal/fixed, but be safe).
    for (char* p = msg; *p; ++p)
        if (*p == '"' || *p == '`' || *p == '$' || *p == '\\') *p = '\'';

    char cmd[700];
    std::snprintf(cmd, sizeof(cmd),
                  "notify-send -t 8000 -u normal 'lumalinux' \"%s\" >/dev/null 2>&1 &",
                  msg);
    int rc = std::system(cmd);
    (void)rc;
}

} // namespace Log
