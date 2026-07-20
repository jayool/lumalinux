#include "key_store.hpp"
#include "license_reconcile.hpp"
#include "log.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

// Lock ordering (outer → inner): KeyStore::Mtx > Log::g_logMutex.
// Several functions below call Log::Info / Log::Warn while holding
// KeyStore::Mtx (specifically inside LoadFromFile). That is acceptable
// because Log::* never calls back into KeyStore — keep it that way.
// If you ever need to log from inside KeyStore while a different module
// also needs to log from inside its own lock, document the ordering there
// too so the global lock graph stays acyclic.

namespace {

// Function-local statics avoid the static-initialization-order fiasco that
// bit us in v0.1 (main thread saw a different g_keys than worker thread).
auto& Keys() {
    static std::map<uint32_t, KeyStore::DepotInfo> instance;
    return instance;
}

auto& Mtx() {
    static std::mutex instance;
    return instance;
}

bool ParseHexKey(const std::string& hex, KeyStore::DepotKey& out) {
    if (hex.size() != 64) return false;
    for (size_t i = 0; i < 32; ++i) {
        char buf[3] = { hex[2*i], hex[2*i+1], 0 };
        char* end = nullptr;
        unsigned long b = std::strtoul(buf, &end, 16);
        if (end != buf + 2) return false;
        out[i] = static_cast<uint8_t>(b);
    }
    return true;
}

std::vector<std::string> SplitTrim(const std::string& line, char sep) {
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, sep)) {
        size_t a = item.find_first_not_of(" \t\r\n");
        size_t b = item.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) { parts.push_back(""); continue; }
        parts.push_back(item.substr(a, b - a + 1));
    }
    return parts;
}

bool ParseLine(const std::string& line, KeyStore::DepotInfo& out) {
    auto parts = SplitTrim(line, ';');

    // No-key (presence-only) format: `<depot_id>;` — written by
    // steamidra_lite.py for the main AppID when the .lua has addappid(N) with
    // no key argument. std::getline used by SplitTrim drops the trailing empty
    // token after the separator, so we detect this by checking that the
    // original line ended in ';' and only one token was parsed.
    bool legacy_no_key = (parts.size() == 2 && parts[1].empty()) ||
                        (parts.size() == 1 && !line.empty() && line.back() == ';');
    if (legacy_no_key) {
        out.depot_id      = static_cast<uint32_t>(std::strtoul(parts[0].c_str(), nullptr, 10));
        out.parent_app_id = 0;
        out.manifest_gid  = 0;
        out.manifest_size = 0;
        out.has_key       = false;
        out.key           = {};
        return out.depot_id != 0;
    }

    if (parts.size() == 2) {
        // Legacy: <depot_id>;<64-hex-key>
        out.depot_id      = static_cast<uint32_t>(std::strtoul(parts[0].c_str(), nullptr, 10));
        out.parent_app_id = 0;
        out.manifest_gid  = 0;
        out.manifest_size = 0;
        out.has_key       = true;
        if (out.depot_id == 0) return false;
        return ParseHexKey(parts[1], out.key);
    }
    if (parts.size() == 5) {
        // Extended: <depot_id>;<parent_app_id>;<manifest_gid>;<manifest_size>;<64-hex-key>
        out.depot_id      = static_cast<uint32_t>(std::strtoul(parts[0].c_str(), nullptr, 10));
        out.parent_app_id = static_cast<uint32_t>(std::strtoul(parts[1].c_str(), nullptr, 10));
        out.manifest_gid  = std::strtoull(parts[2].c_str(), nullptr, 10);
        out.manifest_size = std::strtoull(parts[3].c_str(), nullptr, 10);
        out.has_key       = true;
        if (out.depot_id == 0) return false;
        return ParseHexKey(parts[4], out.key);
    }
    return false;
}

} // namespace

namespace KeyStore {

std::string DefaultPath() {
    const char* home = std::getenv("HOME");
    if (!home) return "/tmp/lumalinux-keys.txt";
    return std::string(home) + "/.config/lumalinux/keys.txt";
}

bool LoadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        Log::Warn("KeyStore: cannot open %s", path.c_str());
        return false;
    }

    std::lock_guard<std::mutex> lock(Mtx());
    auto& keys = Keys();
    keys.clear();

    size_t lineNo = 0;
    size_t loaded = 0;
    size_t injectable = 0;
    std::string line;
    while (std::getline(f, line)) {
        lineNo++;
        if (line.empty()) continue;
        size_t firstNonWs = line.find_first_not_of(" \t");
        if (firstNonWs == std::string::npos) continue;
        if (line[firstNonWs] == '#') continue;

        DepotInfo info;
        if (!ParseLine(line, info)) {
            Log::Warn("KeyStore: skipping malformed line %zu", lineNo);
            continue;
        }
        keys[info.depot_id] = info;
        loaded++;
        if (info.parent_app_id != 0 && info.manifest_gid != 0) injectable++;
    }

    Log::Info("KeyStore: loaded %zu key(s) from %s (%zu injectable)",
              loaded, path.c_str(), injectable);
    return true;
}

void StartWatcher(const std::string& path) {
    static std::atomic<bool> started{false};
    if (started.exchange(true)) return;

    std::thread([path]() {
        // Watch the DIRECTORY, not the file, so a keys.txt created/replaced
        // after launch is still caught; filter events by basename.
        std::string dir, base;
        auto slash = path.find_last_of('/');
        if (slash == std::string::npos) { dir = "."; base = path; }
        else { dir = path.substr(0, slash); base = path.substr(slash + 1); }
        mkdir(dir.c_str(), 0755);  // best-effort; ~/.config already exists

        int fd = inotify_init1(IN_CLOEXEC);
        if (fd < 0) {
            Log::Warn("KeyStore watcher: inotify_init1 failed (%s) — no-restart disabled",
                      std::strerror(errno));
            return;
        }
        // IN_CLOSE_WRITE: steamidra_lite writes keys.txt with a plain
        // open/write/close, so we act on a COMPLETE file. IN_MOVED_TO/IN_CREATE
        // cover a rename-into-place or the first-ever keys.txt.
        int wd = inotify_add_watch(fd, dir.c_str(),
                                   IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
        if (wd < 0) {
            Log::Warn("KeyStore watcher: cannot watch %s (%s) — no-restart disabled",
                      dir.c_str(), std::strerror(errno));
            close(fd);
            return;
        }
        Log::Info("KeyStore watcher: watching %s for changes to %s",
                  dir.c_str(), base.c_str());

        alignas(struct inotify_event) char buf[4096];
        for (;;) {
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                Log::Warn("KeyStore watcher: read ended (%s) — stopping",
                          n < 0 ? std::strerror(errno) : "eof");
                break;
            }
            bool hit = false;
            for (char* p = buf; p < buf + n; ) {
                auto* ev = reinterpret_cast<const struct inotify_event*>(p);
                if (ev->len > 0 && base == ev->name) hit = true;
                p += sizeof(struct inotify_event) + ev->len;
            }
            if (hit) {
                LoadFromFile(path);
                // Signal the finder thread to fire the reconcile after it
                // re-injects the freshly-loaded depots. This thread NEVER calls
                // into Steam itself.
                LicenseReconcile::NotifyKeysChanged();
                Log::Info("KeyStore watcher: %s changed — reloaded (Size=%zu), "
                          "reconcile armed", base.c_str(), Size());
            }
        }
        close(fd);
    }).detach();
}

std::optional<DepotKey> Lookup(uint32_t depot_id) {
    std::lock_guard<std::mutex> lock(Mtx());
    auto it = Keys().find(depot_id);
    if (it == Keys().end()) return std::nullopt;
    // Presence-only entries (no key in the .lua): tell the caller "not found"
    // so the DepotKey hook falls through to Steam's original implementation
    // instead of serving zeros. Mirrors LumaCore (DepotKeys.cpp:14): if the
    // DepotKeySet value is an empty string, GetDecryptionKey returns an empty
    // vector and the hook does not respond.
    if (!it->second.has_key) return std::nullopt;
    return it->second.key;
}

std::vector<DepotInfo> GetDepotsForApp(uint32_t app_id) {
    std::lock_guard<std::mutex> lock(Mtx());
    std::vector<DepotInfo> result;
    if (app_id == 0) return result;
    for (const auto& [_, info] : Keys()) {
        if (info.parent_app_id == app_id && info.manifest_gid != 0) {
            result.push_back(info);
        }
    }
    return result;
}

std::vector<uint32_t> GetAllDepotIds() {
    std::lock_guard<std::mutex> lock(Mtx());
    std::vector<uint32_t> result;
    result.reserve(Keys().size());
    for (const auto& [depot_id, _] : Keys()) {
        result.push_back(depot_id);
    }
    return result;
}

bool HasManifestGid(uint64_t manifest_gid) {
    if (manifest_gid == 0) return false;
    std::lock_guard<std::mutex> lock(Mtx());
    for (const auto& [_, info] : Keys()) {
        if (info.manifest_gid == manifest_gid) return true;
    }
    return false;
}

bool HasDepot(uint32_t depot_id) {
    if (depot_id == 0) return false;
    std::lock_guard<std::mutex> lock(Mtx());
    return Keys().find(depot_id) != Keys().end();
}

bool IsPresenceOnly(uint32_t depot_id) {
    if (depot_id == 0) return false;
    std::lock_guard<std::mutex> lock(Mtx());
    auto it = Keys().find(depot_id);
    if (it == Keys().end()) return false;   // not ours at all
    return !it->second.has_key;             // ours, but no key (keyless shader depot)
}

size_t Size() {
    std::lock_guard<std::mutex> lock(Mtx());
    return Keys().size();
}

const void* DebugKeysAddr() {
    return &Keys();
}

} // namespace KeyStore
