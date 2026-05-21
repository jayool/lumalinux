#include "key_store.hpp"
#include "log.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <mutex>
#include <unistd.h>
#include <pwd.h>

namespace {

std::map<uint32_t, KeyStore::DepotKey> g_keys;
std::mutex g_mutex;

bool HexCharToNibble(char c, uint8_t& out) {
    if (c >= '0' && c <= '9') { out = c - '0';      return true; }
    if (c >= 'a' && c <= 'f') { out = c - 'a' + 10; return true; }
    if (c >= 'A' && c <= 'F') { out = c - 'A' + 10; return true; }
    return false;
}

bool ParseHex32(const std::string& hex, KeyStore::DepotKey& out) {
    if (hex.size() != 64) return false;
    for (size_t i = 0; i < 32; i++) {
        uint8_t hi, lo;
        if (!HexCharToNibble(hex[i * 2 + 0], hi)) return false;
        if (!HexCharToNibble(hex[i * 2 + 1], lo)) return false;
        out[i] = (hi << 4) | lo;
    }
    return true;
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

} // namespace

namespace KeyStore {

bool LoadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        Log::Warn("KeyStore: cannot open %s — running with empty key store", path.c_str());
        return false;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    g_keys.clear();

    std::string line;
    size_t lineNo = 0;
    size_t loaded = 0;
    while (std::getline(f, line)) {
        lineNo++;
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#') continue;

        auto sep = t.find(';');
        if (sep == std::string::npos) {
            Log::Warn("KeyStore: %s:%zu — missing ';' separator, skipping", path.c_str(), lineNo);
            continue;
        }

        std::string idStr  = Trim(t.substr(0, sep));
        std::string hexStr = Trim(t.substr(sep + 1));

        char* endp = nullptr;
        unsigned long depotId = std::strtoul(idStr.c_str(), &endp, 10);
        if (!endp || *endp != '\0' || depotId == 0 || depotId > 0xFFFFFFFFul) {
            Log::Warn("KeyStore: %s:%zu — bad depot_id '%s'", path.c_str(), lineNo, idStr.c_str());
            continue;
        }

        DepotKey key{};
        if (!ParseHex32(hexStr, key)) {
            Log::Warn("KeyStore: %s:%zu — bad hex key (need 64 hex chars)", path.c_str(), lineNo);
            continue;
        }

        g_keys[static_cast<uint32_t>(depotId)] = key;
        loaded++;
    }

    Log::Info("KeyStore: loaded %zu key(s) from %s", loaded, path.c_str());
    return true;
}

std::optional<DepotKey> Lookup(uint32_t depotId) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_keys.find(depotId);
    if (it == g_keys.end()) return std::nullopt;
    return it->second;
}

size_t Size() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_keys.size();
}

std::string DefaultPath() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return std::string(xdg) + "/lumalinux/keys.txt";

    const char* home = std::getenv("HOME");
    if (!home || !*home) {
        if (passwd* pw = getpwuid(getuid())) home = pw->pw_dir;
    }
    if (home && *home) return std::string(home) + "/.config/lumalinux/keys.txt";

    return "/tmp/lumalinux-keys.txt";
}

} // namespace KeyStore
