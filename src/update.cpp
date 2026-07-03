// SPDX-License-Identifier: AGPL-3.0-only
// Originally from https://github.com/AceSLS/SLSsteam (AGPL-3.0) src/update.cpp.
// Adaptations vs upstream (kept minimal):
//   1. URL of the fetch is lumalinux's, not SLSsteam's.
//   2. Cache path is hardcoded to ~/.cache/lumalinux/.updates.yaml (lumalinux
//      has no CConfig::getDir() equivalent and already uses ~/.cache/lumalinux/
//      for its log).
//   3. Logger calls go through Log:: (lumalinux's logger), not g_pLog->.
// Everything else (the YAML schema, the comparator, the cache-on-success flow,
// the fail-closed semantics) is unchanged from SLSsteam.
#include "update.hpp"

#include "curl.hpp"
#include "globals.hpp"
#include "log.hpp"
#include "patterns.hpp"
#include "sha256.hpp"
#include "version.hpp"

#include "yaml-cpp/yaml.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

// SLSsteam upstream uses a top-level `VERSION` constant from its version.hpp.
// lumalinux's version.hpp is CMake-generated and namespaces this as
// LUMALINUX_SAFEMODE_VERSION to coexist with LUMALINUX_VERSION_STRING; alias
// it locally so the rest of this file stays byte-identical to SLSsteam.
namespace { constexpr uint64_t VERSION = LUMALINUX_SAFEMODE_VERSION; }

std::map<uint64_t, std::unordered_set<std::string>> Updater::clientHashMap = std::map<uint64_t, std::unordered_set<std::string>>();

// v0.16 runtime pattern override state (parsed from the additive Builds /
// PatternGroups sections of res/updates.yaml).
std::map<std::string, std::string> Updater::buildToGroup;
std::map<std::string, std::map<std::string, std::string>> Updater::patternGroups;
std::map<std::string, int> Updater::loadPackageIndex;

bool Updater::init()
{
	std::string data;
	int res = Curl::getString("https://raw.githubusercontent.com/jayool/lumalinux/main/res/updates.yaml", data);
	Log::Info("Curl Res: %u", res);

	if(res != 0)
	{
		data = loadFromCache();
		if(data.size() < 1)
		{
			return false;
		}

		Log::Info("Using cached updates.yaml");
	}

	Log::Debug("updates.yaml:\n%s", data.c_str());

	try
	{
		YAML::Node node = YAML::Load(data);
		for (const auto& sub : node["SafeModeHashes"])
		{
			uint64_t version = sub.first.as<uint64_t>();
			clientHashMap[version] = std::unordered_set<std::string>();

			Log::Debug("Parsing version %llu", version);

			for(const auto& hash : sub.second)
			{
				auto str = hash.as<std::string>();
				clientHashMap[version].emplace(str);

				Log::Debug("Added %s to lumalinux version %llu", str.c_str(), version);
			}
		}

			// ── v0.16: additive Builds / PatternGroups (optional) ──
			// Absent in pre-v0.16 files — parse defensively; leave the maps
			// empty if missing so an old fetched file still boots on SafeMode
			// alone (compiled-in patterns).
			buildToGroup.clear();
			patternGroups.clear();
			loadPackageIndex.clear();

			if (node["Builds"] && node["Builds"].IsMap())
			{
				for (const auto& b : node["Builds"])
				{
					std::string sha = b.first.as<std::string>();
					const auto& meta = b.second;
					if (meta.IsMap() && meta["pattern_group"])
						buildToGroup[sha] = meta["pattern_group"].as<std::string>();
				}
				Log::Debug("Parsed %zu Builds entries", buildToGroup.size());
			}

			if (node["PatternGroups"] && node["PatternGroups"].IsMap())
			{
				for (const auto& g : node["PatternGroups"])
				{
					std::string group = g.first.as<std::string>();
					const auto& set = g.second;
					if (!set.IsMap()) continue;
					for (const auto& e : set)
					{
						std::string name = e.first.as<std::string>();
						if (name == "schema") continue;   // metadata, not a pattern
						const auto& val = e.second;
						// scalar string, or map { pattern: "...", match_index: N }
						if (val.IsScalar())
							patternGroups[group][name] = val.as<std::string>();
						else if (val.IsMap() && val["pattern"])
						{
							patternGroups[group][name] = val["pattern"].as<std::string>();
							if (name == "LoadPackage" && val["match_index"])
								loadPackageIndex[group] = val["match_index"].as<int>();
						}
					}
				}
				Log::Debug("Parsed %zu PatternGroups", patternGroups.size());
			}
	}
	catch(...)
	{
		Log::Info("Failed to parse updates!");
		return false;
	}

	saveToCache(data);
	return true;
}

std::string Updater::getCacheFilePath()
{
	const char* home = std::getenv("HOME");
	std::filesystem::path dir = std::filesystem::path(home ? home : "/tmp") / ".cache" / "lumalinux";
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	return (dir / ".updates.yaml").string();
}

void Updater::saveToCache(std::string yaml)
{
	auto path = Updater::getCacheFilePath();

	std::ofstream stream = std::ofstream(path.c_str());
	stream << yaml;
	stream.close();

	Log::Debug("Cached res/updates.yaml!");
}

std::string Updater::loadFromCache()
{
	auto path = Updater::getCacheFilePath();
	if (!std::filesystem::exists(path))
	{
		return std::string();
	}

	Log::Debug("Loading updates.yaml from disk!");

	std::ifstream fstream = std::ifstream(path.c_str());
	std::stringstream buf;
	buf << fstream.rdbuf();

	fstream.close();
	return buf.str();
}

bool Updater::verifySafeModeHash()
{
	auto path = std::filesystem::path(g_modSteamClient.path);

	try
	{
		std::string sha256 = Utils::getFileSHA256(path.c_str());
		Log::Info("steamclient.so hash is %s", sha256.c_str());

		// Original path: hash whitelisted under THIS binary's compiled-in version
		// group. Backward-compatible with pre-v0.16 files.
		if (clientHashMap.contains(VERSION) && clientHashMap[VERSION].contains(sha256))
		{
			return true;
		}

		// v0.16: accept a hash published in Builds with a pattern group we can
		// resolve — so a build in a NEWER pattern group than the one compiled into
		// this binary still passes (applyPatternOverrides has by now installed that
		// group's patterns). Without this the runtime override is inert on the case
		// that matters (a Steam update that CHANGED patterns). Still fail-closed: a
		// hash not published anywhere is rejected.
		if (buildToGroup.contains(sha256) && patternGroups.contains(buildToGroup[sha256]))
		{
			Log::Info("SafeMode: hash accepted via Builds/PatternGroups (group %s)",
			          buildToGroup[sha256].c_str());
			return true;
		}

		return false;
	}
	catch(std::runtime_error& err)
	{
		Log::Debug("Unable to read steamclient.so hash!");
		return false;
	}

	return true;
}

void Updater::applyPatternOverrides()
{
	if (buildToGroup.empty() && patternGroups.empty())
	{
		Log::Info("applyPatternOverrides: updates.yaml has no Builds/PatternGroups "
		          "(pre-v0.16 file) — using compiled-in patterns");
		return;
	}

	std::string sha256;
	try
	{
		sha256 = Utils::getFileSHA256(std::filesystem::path(g_modSteamClient.path).c_str());
	}
	catch(...)
	{
		Log::Debug("applyPatternOverrides: cannot hash steamclient.so — compiled-in patterns");
		return;
	}

	auto it = buildToGroup.find(sha256);
	if (it == buildToGroup.end())
	{
		Log::Info("applyPatternOverrides: no Builds entry for %s — using compiled-in patterns", sha256.c_str());
		return;
	}
	const std::string& group = it->second;
	auto git = patternGroups.find(group);
	if (git == patternGroups.end())
	{
		Log::Debug("applyPatternOverrides: no PatternGroups[%s] — compiled-in patterns", group.c_str());
		return;
	}

	static const std::map<std::string, Patterns::PatternId> kNameToId = {
		{"DepotKey",    Patterns::PatternId::DepotKey},
		{"BuildDep",    Patterns::PatternId::BuildDep},
		{"LoadPackage", Patterns::PatternId::LoadPackage},
		{"Gmrc",        Patterns::PatternId::Gmrc},
		{"ShaderDepot", Patterns::PatternId::ShaderDepot},
	};

	int applied = 0;
	for (const auto& kv : git->second)
	{
		auto nid = kNameToId.find(kv.first);
		if (nid == kNameToId.end()) continue;
		Patterns::SetPatternOverride(nid->second, kv.second);
		++applied;
	}
	auto lpi = loadPackageIndex.find(group);
	if (lpi != loadPackageIndex.end())
		Patterns::SetMatchIndexOverride(Patterns::PatternId::LoadPackage, lpi->second);

	Log::Info("applyPatternOverrides: applied %d override(s) from group %s for steamclient %s",
	          applied, group.c_str(), sha256.c_str());
}
