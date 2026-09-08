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
// finder.cache_global_disp. Kept apart from g_hooks/g_loaded on purpose: it
// needs neither VaddrXlate nor a non-empty hook map, and load() bails out on
// either of those — so folding it in would have thrown the value away in cases
// where it is perfectly usable.
int32_t                          g_cacheGlobalDisp = 0;

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

// res/rvas/<hash>.yaml text: repo fetch (cached) -> cache fallback (offline).
// Mirrors update.cpp's fetch+cache flow.
//
// The feed source is HARDCODED on purpose. It used to be overridable via the env
// vars LUMA_RVAS_DIR (local dir) and LUMA_RVAS_URL (alternate base URL), which
// was convenient for testing a branch without publishing — but those overrides
// shipped in release builds, and this feed decides WHERE HOOKS GET INSTALLED
// inside Steam's process. Anything able to add an env var to Steam's launch (a
// modified .desktop, a launcher script, another Decky plugin) could therefore
// point the feed at a server it controlled and choose our hook targets. That
// turns "can write a file in $HOME" into "can run code inside Steam", so the
// overrides are gone. To test against a branch, point this URL at it in a local
// build; do NOT reintroduce a runtime override in a release path.
bool obtainYaml(const std::string& hash, std::string& out) {
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
        const YAML::Node root = YAML::Load(yaml);
        // Read the finder's value FIRST, and independently of everything below:
        // the hook path can still bail out (no hooks, or VaddrXlate failing) in
        // ways that say nothing about this number's validity.
        const YAML::Node finder = root["finder"];
        if (finder && finder.IsMap() && finder["cache_global_disp"]) {
            const long v = std::strtol(
                finder["cache_global_disp"].as<std::string>().c_str(), nullptr, 16);
            if (v) {
                g_cacheGlobalDisp = static_cast<int32_t>(v);
                Log::Info("RvaFeed: finder cache_global_disp = 0x%x from the feed",
                          static_cast<unsigned>(g_cacheGlobalDisp));
            }
        }
        YAML::Node hooks = root["hooks"];
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
        g_cacheGlobalDisp = 0;
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

int32_t CacheGlobalDisp() {
    std::call_once(g_once, load);
    return g_cacheGlobalDisp;      // no g_loaded gate: see the note on g_hooks
}

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
