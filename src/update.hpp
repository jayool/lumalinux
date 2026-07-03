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

	// v0.16 runtime pattern override (see res/updates.yaml schema + patterns.hpp).
	// Populated by init() from the (additive, optional) Builds / PatternGroups
	// sections; consumed by applyPatternOverrides(). Empty when the fetched file
	// predates the schema — the compiled-in patterns are then used unchanged.
	extern std::map<std::string, std::string> buildToGroup;                          // sha256 -> pattern group id
	extern std::map<std::string, std::map<std::string, std::string>> patternGroups;  // group -> {name -> pattern}
	extern std::map<std::string, int> loadPackageIndex;                              // group -> LoadPackage match_index (-1 unset)

	std::string getCacheFilePath();
	std::string loadFromCache();
	void saveToCache(std::string yaml);

	bool init();
	bool verifySafeModeHash();

	// Resolve the running steamclient.so hash to its published pattern group and
	// install those patterns as runtime overrides (Patterns::SetPatternOverride).
	// Safe no-op when nothing is published for this hash / the sections are absent.
	void applyPatternOverrides();
}
