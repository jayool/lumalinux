#include "rva_feed.hpp"

#include "curl.hpp"
#include "globals.hpp"
#include "log.hpp"
#include "sha256.hpp"
#include "vaddr_xlate.hpp"

#include "yaml-cpp/yaml.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>

namespace RvaFeed {
namespace {

std::once_flag                   g_once;
bool                             g_loaded = false;
std::map<std::string, uintptr_t> g_hooks;    // hook name -> file vaddr (RVA)

std::string cacheDir() {
    const char* home = std::getenv("HOME");
    return (std::filesystem::path(home ? home : "/tmp") / ".cache" / "lumalinux" / "rvas").string();
}

bool readFile(const std::filesystem::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return !out.empty();
}

// res/rvas/<hash>.yaml text: LUMA_RVAS_DIR override (test/offline) -> repo fetch
// (cached) -> cache fallback (offline). Mirrors update.cpp's fetch+cache flow.
bool obtainYaml(const std::string& hash, std::string& out) {
    if (const char* dir = std::getenv("LUMA_RVAS_DIR")) {
        if (readFile(std::filesystem::path(dir) / (hash + ".yaml"), out)) return true;
    }
    const std::string url =
        "https://raw.githubusercontent.com/jayool/lumalinux/main/res/rvas/" + hash + ".yaml";
    if (Curl::getString(url.c_str(), out) == 0 && out.find("hooks:") != std::string::npos) {
        std::error_code ec;
        std::filesystem::create_directories(cacheDir(), ec);
        std::ofstream(std::filesystem::path(cacheDir()) / (hash + ".yaml"), std::ios::binary) << out;
        return true;
    }
    return readFile(std::filesystem::path(cacheDir()) / (hash + ".yaml"), out);
}

void load() {
    const char* path = g_modSteamClient.path;
    if (!path || !*path) { Log::Debug("RvaFeed: steamclient.so path unknown"); return; }

    const std::string hash = Utils::getFileSHA256(path);
    if (hash.empty()) { Log::Debug("RvaFeed: could not hash steamclient.so"); return; }

    std::string yaml;
    if (!obtainYaml(hash, yaml)) {
        Log::Info("RvaFeed: no feed for build %s — hooks fall back to byte patterns",
                  hash.c_str());
        return;
    }
    try {
        YAML::Node hooks = YAML::Load(yaml)["hooks"];
        if (hooks && hooks.IsMap()) {
            for (auto it = hooks.begin(); it != hooks.end(); ++it) {
                const std::string name = it->first.as<std::string>();
                const uintptr_t rva = static_cast<uintptr_t>(
                    std::strtoul(it->second.as<std::string>().c_str(), nullptr, 16));
                if (rva) g_hooks[name] = rva;
            }
        }
    } catch (const std::exception& e) {
        Log::Warn("RvaFeed: parse error (%s) — falling back to byte patterns", e.what());
        g_hooks.clear();
        return;
    }
    if (g_hooks.empty()) { Log::Info("RvaFeed: feed has no hooks for %s", hash.c_str()); return; }
    if (!VaddrXlate::Init(path)) {
        Log::Warn("RvaFeed: vaddr translation init failed — falling back to byte patterns");
        g_hooks.clear();
        return;
    }
    g_loaded = true;
    Log::Info("RvaFeed: loaded %zu hook RVA(s) for build %s", g_hooks.size(), hash.c_str());
}

// Light, prologue-independent sanity: is `addr` inside an executable
// steamclient.so mapping? Guards a feed RVA that translates into non-code.
bool inSteamclientExec(uintptr_t addr) {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find("steamclient.so") == std::string::npos) continue;
        if (line.find("r-x") == std::string::npos) continue;
        unsigned long s = 0, e = 0;
        if (std::sscanf(line.c_str(), "%lx-%lx", &s, &e) < 2) continue;
        if (addr >= s && addr < e) return true;
    }
    return false;
}

} // namespace

uintptr_t Resolve(const char* hookName) {
    std::call_once(g_once, load);
    if (!g_loaded) return 0;

    auto it = g_hooks.find(hookName);
    if (it == g_hooks.end()) return 0;

    const uintptr_t rt = VaddrXlate::ToRuntime(it->second);
    if (!rt) {
        Log::Warn("RvaFeed: %s rva 0x%lx did not translate — fallback",
                  hookName, static_cast<unsigned long>(it->second));
        return 0;
    }
    if (!inSteamclientExec(rt)) {
        Log::Warn("RvaFeed: %s -> 0x%lx not in steamclient .text — fallback",
                  hookName, static_cast<unsigned long>(rt));
        return 0;
    }
    Log::Info("RvaFeed: %s via feed: rva 0x%lx -> 0x%lx",
              hookName, static_cast<unsigned long>(it->second), static_cast<unsigned long>(rt));
    return rt;
}

} // namespace RvaFeed
