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

		if (!clientHashMap.contains(VERSION))
		{
			return false;
		}

		const auto& safeHashes = clientHashMap[VERSION];
		if (safeHashes.contains(sha256))
		{
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
