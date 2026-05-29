#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace KeyStore {

using DepotKey = std::array<uint8_t, 32>;

// Full per-depot info as stored in keys.txt.
struct DepotInfo {
    uint32_t depot_id      = 0;
    uint32_t parent_app_id = 0;   // 0 if not set (legacy format / shared depot)
    uint64_t manifest_gid  = 0;   // 0 if not set
    uint64_t manifest_size = 0;   // 0 if not set
    DepotKey key{};
};

// Default keys file path (~/.config/lumalinux/keys.txt).
std::string DefaultPath();

// Load keys from file. Two line formats supported:
//
//   Legacy:    <depot_id>;<64-hex-key>
//                          → key only; no injection (used for depots Steam
//                            already knows about, e.g. shared dependencies).
//
//   Extended:  <depot_id>;<parent_app_id>;<manifest_gid>;<manifest_size>;<64-hex-key>
//                          → full info; lumalinux will INJECT this depot into
//                            pDepotInfo when Steam builds the dependency list
//                            for parent_app_id.
//
// Lines starting with # and blank lines are ignored.
bool LoadFromFile(const std::string& path);

// Lookup just the 32-byte key for a depot. Used by depot_key_hook.
std::optional<DepotKey> Lookup(uint32_t depot_id);

// All depots whose parent_app_id == app_id AND have manifest_gid set.
// Used by depot_dependency_hook to know what gid/size to patch. Empty if none.
std::vector<DepotInfo> GetDepotsForApp(uint32_t app_id);

// Every depot_id present in the store (i.e. every key we have). This is the
// faithful Linux equivalent of LumaCore's LuaLoader::GetAllDepotIds() — it
// returns the DEPOT ids (not parent app ids). The LoadPackage hook injects
// these into PackageId=0's AppIdVec so Steam's per-depot license check in
// BuildDepotDependency finds them "owned" and keeps them in the depot list.
std::vector<uint32_t> GetAllDepotIds();

// True if `manifest_gid` is the manifest of one of our forced depots. The GMRC
// hook uses this to only inject a manifest request code for manifests we
// actually manage (leaving genuinely-owned content to Steam's normal path).
bool HasManifestGid(uint64_t manifest_gid);

// Total count of keys loaded.
size_t Size();

// Debug accessor — pointer to internal map for cross-thread sanity checks.
const void* DebugKeysAddr();

} // namespace KeyStore
