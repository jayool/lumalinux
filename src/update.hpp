// SPDX-License-Identifier: AGPL-3.0-only
// Originally from https://github.com/AceSLS/SLSsteam (AGPL-3.0)
// Imported unchanged.
#pragma once

#include <curl/curl.h>

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

	bool init();
	bool verifySafeModeHash();
}
