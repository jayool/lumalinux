#include "key_store.hpp"
#include "log.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

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

    if (parts.size() == 2) {
        // Legacy: <depot_id>;<64-hex-key>
        out.depot_id      = static_cast<uint32_t>(std::strtoul(parts[0].c_str(), nullptr, 10));
        out.parent_app_id = 0;
        out.manifest_gid  = 0;
        out.manifest_size = 0;
        if (out.depot_id == 0) return false;
        return ParseHexKey(parts[1], out.key);
    }
    if (parts.size() == 5) {
        // Extended: <depot_id>;<parent_app_id>;<manifest_gid>;<manifest_size>;<64-hex-key>
        out.depot_id      = static_cast<uint32_t>(std::strtoul(parts[0].c_str(), nullptr, 10));
        out.parent_app_id = static_cast<uint32_t>(std::strtoul(parts[1].c_str(), nullptr, 10));
        out.manifest_gid  = std::strtoull(parts[2].c_str(), nullptr, 10);
        out.manifest_size = std::strtoull(parts[3].c_str(), nullptr, 10);
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

std::optional<DepotKey> Lookup(uint32_t depot_id) {
    std::lock_guard<std::mutex> lock(Mtx());
    auto it = Keys().find(depot_id);
    if (it == Keys().end()) return std::nullopt;
    return it->second.key;
}

std::optional<DepotInfo> LookupInfo(uint32_t depot_id) {
    std::lock_guard<std::mutex> lock(Mtx());
    auto it = Keys().find(depot_id);
    if (it == Keys().end()) return std::nullopt;
    return it->second;
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

size_t Size() {
    std::lock_guard<std::mutex> lock(Mtx());
    return Keys().size();
}

const void* DebugKeysAddr() {
    return &Keys();
}

} // namespace KeyStore
