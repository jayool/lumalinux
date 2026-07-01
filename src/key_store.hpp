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
    bool     has_key       = true; // false for "presence-only" entries (the
                                   // .lua had addappid(N) without a key — used
                                   // for the main AppID in older Hubcap .luas
                                   // like Balatro's). Mirrors LumaCore's
                                   // empty-string semantics in DepotKeySet.
    DepotKey key{};
};

// Default keys file path (~/.config/lumalinux/keys.txt).
std::string DefaultPath();

// Load keys from file. Three line formats supported:
//
//   Legacy:    <depot_id>;<64-hex-key>
//                          → key only; no injection (used for depots Steam
//                            already knows about, e.g. shared dependencies).
//
//   No-key:    <depot_id>;
//                          → presence-only entry (trailing ';' with empty key
//                            field). The depot id IS returned by
//                            GetAllDepotIds() so LoadPackage injects it into
//                            AppIdVec, but Lookup() returns nullopt so the
//                            DepotKey hook falls through to Steam's original
//                            instead of serving a fake key. Matches LumaCore's
//                            DepotKeySet[id] = "" semantics for addappid(N)
//                            without a key argument.
//
//   Extended:  <depot_id>;<parent_app_id>;<manifest_gid>;<manifest_size>;<64-hex-key>
//                          → full info; lumalinux will INJECT this depot into
//                            pDepotInfo when Steam builds the dependency list
//                            for parent_app_id.
//
// Lines starting with # and blank lines are ignored.
bool LoadFromFile(const std::string& path);

// Lookup just the 32-byte key for a depot. Used by depot_key_hook.
// Returns nullopt if the depot is not in the store OR if its entry is
// presence-only (has_key=false) — in the latter case the hook should
// passthrough to the original LoadDepotDecryptionKey.
std::optional<DepotKey> Lookup(uint32_t depot_id);

// All depots whose parent_app_id == app_id AND have manifest_gid set.
// Used by depot_dependency_hook to know what gid/size to patch. Empty if none.
std::vector<DepotInfo> GetDepotsForApp(uint32_t app_id);

// True if `depot_id` is stored as a CONTENT depot (parent_app_id != 0),
// regardless of whether a manifest_gid is pinned. The base app depot is stored
// legacy (parent_app_id == 0), so this is false for it — which is exactly how
// the BuildDep hook tells a real content depot from the contentless base
// placeholder even in the default no-pin mode (where content depots carry
// gid=0 and GetDepotsForApp is empty).
bool IsContentDepot(uint32_t depot_id);

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

// True if `depot_id` is present in the KeyStore (regardless of which specific
// manifest_gid the caller is asking about). Used by the GMRC hook to cover
// the case where Steam asks for a manifest of one of our depots whose gid was
// not pre-seeded by steamidra_lite — typically the app/shader depot during
// shader pre-cache, where Steam supplies a manifest_gid taken from PICS
// appinfo rather than from the Hubcap .lua.
bool HasDepot(uint32_t depot_id);

// True if `depot_id` is one of ours AND was registered presence-only (no key
// in the .lua — has_key=false). This is exactly the keyless shader pre-cache
// depot of a game added via lumalinux (its id == the app id). The shader-depot
// hook (Hooks::ShaderDepot) uses this to zero out the shader depot id for such
// games so Steam takes its native "skip shader pre-cache" path. Returns false
// for depots that ship a real key (their shaders decrypt and should run) and
// for depots we don't manage.
bool IsPresenceOnly(uint32_t depot_id);

// Total count of keys loaded.
size_t Size();

// Debug accessor — pointer to internal map for cross-thread sanity checks.
const void* DebugKeysAddr();

} // namespace KeyStore
