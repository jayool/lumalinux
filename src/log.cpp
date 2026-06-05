#include "log.hpp"

#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <mutex>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <pwd.h>

namespace {

FILE* g_logFile = nullptr;
std::mutex g_logMutex;

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

void WriteLine(const char* level, const char* fmt, va_list ap) {
    if (!g_logFile) return;

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
    if (g_logFile) return;
    std::string path = ResolveLogPath();
    g_logFile = std::fopen(path.c_str(), "a");
    if (g_logFile) {
        Info("=== lumalinux loaded (pid=%d) ===", getpid());
    }
}

void Shutdown() {
    if (g_logFile) {
        Info("=== lumalinux shutdown ===");
        std::fclose(g_logFile);
        g_logFile = nullptr;
    }
}

void Info(const char* fmt, ...)  { va_list ap; va_start(ap, fmt); WriteLine("INFO",  fmt, ap); va_end(ap); }
void Warn(const char* fmt, ...)  { va_list ap; va_start(ap, fmt); WriteLine("WARN",  fmt, ap); va_end(ap); }
void Error(const char* fmt, ...) { va_list ap; va_start(ap, fmt); WriteLine("ERROR", fmt, ap); va_end(ap); }
void Debug(const char* fmt, ...) { va_list ap; va_start(ap, fmt); WriteLine("DEBUG", fmt, ap); va_end(ap); }

void Notify(const char* fmt, ...) {
    char msg[512];
    va_list ap; va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    // Always log it.
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
