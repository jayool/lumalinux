#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <map>

namespace KeyStore {

// Steam depot decryption keys are AES-256 keys (32 bytes raw).
using DepotKey = std::array<uint8_t, 32>;

// Load keys from a text file. Each non-empty, non-comment line is:
//     <depot_id>;<64 hex chars>
// Whitespace and lines starting with '#' are ignored.
// Returns true if at least the file was readable (even if no valid keys).
bool LoadFromFile(const std::string& path);

// Look up a key for a given depot_id. nullopt if not present.
std::optional<DepotKey> Lookup(uint32_t depotId);

// How many keys were loaded
size_t Size();

// Resolve default config path: $XDG_CONFIG_HOME/lumalinux/keys.txt, fallbacks to
// ~/.config/lumalinux/keys.txt
std::string DefaultPath();

} // namespace KeyStore
