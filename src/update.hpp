// SPDX-License-Identifier: AGPL-3.0-only
// Originally from https://github.com/AceSLS/SLSsteam (AGPL-3.0)
// Imported unchanged.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_set>


namespace Updater
{
	extern std::map<uint64_t, std::unordered_set<std::string>> clientHashMap;

	std::string getCacheFilePath();
	std::string loadFromCache();
	void saveToCache(std::string yaml);

	// clientHash: the installed steamclient.so's SHA-256 (may be empty). When
	// the cached updates.yaml already lists it, init() uses the cache and
	// skips the fetch; refreshPending() then says so and refreshCache() does
	// the fetch later, off the hook-install path.
	bool init(const std::string& clientHash = "");
	bool refreshPending();
	void refreshCache();
	bool verifySafeModeHash();
}
