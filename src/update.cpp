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

namespace {

constexpr const char* kFeedUrl = "https://raw.githubusercontent.com/jayool/lumalinux/main/res/updates.yaml";
bool g_refreshPending = false;

// Parse the feed into clientHashMap (replacing it). False on a parse error,
// with the map left as it was.
bool parseFeed(const std::string& data)
{
	try
	{
		std::map<uint64_t, std::unordered_set<std::string>> fresh;
		YAML::Node node = YAML::Load(data);
		for (const auto& sub : node["SafeModeHashes"])
		{
			uint64_t version = sub.first.as<uint64_t>();
			fresh[version] = std::unordered_set<std::string>();

			Log::Debug("Parsing version %llu", version);

			for(const auto& hash : sub.second)
			{
				auto str = hash.as<std::string>();
				fresh[version].emplace(str);

				Log::Debug("Added %s to lumalinux version %llu", str.c_str(), version);
			}
		}
		Updater::clientHashMap = std::move(fresh);
		return true;
	}
	catch(...)
	{
		Log::Info("Failed to parse updates!");
		return false;
	}
}

bool listsHash(const std::string& hash)
{
	auto it = Updater::clientHashMap.find(VERSION);
	return it != Updater::clientHashMap.end() && it->second.contains(hash);
}

} // namespace

std::map<uint64_t, std::unordered_set<std::string>> Updater::clientHashMap = std::map<uint64_t, std::unordered_set<std::string>>();

// Does this body actually look like the feed?
//
// Divergence from upstream, and the reason for it: SLSsteam added
// `data.starts_with("SafeModeHashes:\n")` in 20260819224703 after GitHub served
// it a 200 with an unrelated body. That exact check would reject OUR feed —
// res/updates.yaml opens with 26 lines of comment before the key — and a bare
// find() would match the word inside that very header. So: the key, anchored to
// column 0. Accepts both layouts, ours and upstream's.
//
// Measured against yaml-cpp rather than assumed (tools/test_update_feed_guard.cpp):
// an empty body and an API error JSON both parse WITHOUT throwing, leave
// clientHashMap empty, and reach saveToCache() — overwriting a good cache with
// garbage. HTML and plain text throw instead, so they never cached, but they
// still cost the toast. Validating here covers all of them.
static bool looksLikeFeed(const std::string& data)
{
	for(size_t pos = 0; pos != std::string::npos; )
	{
		if(data.compare(pos, 15, "SafeModeHashes:") == 0)
		{
			return true;
		}

		pos = data.find('\n', pos);
		if(pos != std::string::npos)
		{
			++pos;
		}
	}

	return false;
}

bool Updater::init(const std::string& clientHash)
{
	// Cache first. The list of verified clients only ever grows, so a cached
	// copy that already names the installed steamclient.so answers the check
	// without the network — and without loading libcurl inside Steam while it
	// is still starting (measured 2026-10-19: this fetch was 4 of the 6 s
	// between seeing steamclient.so and having the hooks; on the Deck that is
	// time Steam's loader thread sits blocked, and with no wifi yet, a 15 s
	// connect timeout). The copy is refreshed afterwards (refreshCache()), so
	// a new entry still lands by the next start. A client the copy does not
	// know — a fresh Steam update — fetches now, as before.
	if(!clientHash.empty())
	{
		std::string cached = loadFromCache();
		if(looksLikeFeed(cached) && parseFeed(cached) && listsHash(clientHash))
		{
			Log::Info("updates.yaml: the cached copy already lists this steamclient.so — "
			          "no fetch at startup, refresh after install");
			g_refreshPending = true;
			return true;
		}
	}

	std::string data;
	int res = Curl::getString(kFeedUrl, data);
	Log::Info("Curl Res: %u", res);

	// A curl res of 0 means the TRANSFER succeeded, not that the feed arrived: a
	// 200 carrying an error page is a perfectly successful transfer. Treat a body
	// that is not the feed as a fetch failure, so we fall back to the cache we
	// know is good instead of parsing garbage and then saving it over that cache.
	if(res == 0 && !looksLikeFeed(data))
	{
		Log::Warn("updates.yaml fetched but the body is not the feed (%zu bytes) "
		          "— falling back to the cache", data.size());
		res = -1;
	}

	if(res != 0)
	{
		data = loadFromCache();
		if(data.size() < 1)
		{
			return false;
		}

		// The cache is written only from a body that passed looksLikeFeed(), so a
		// pre-existing cache from before that guard is the one thing that can still
		// be garbage. Re-check it rather than trust its provenance.
		if(!looksLikeFeed(data))
		{
			Log::Warn("Cached updates.yaml is not the feed — discarding it");
			return false;
		}

		Log::Info("Using cached updates.yaml");
	}

	Log::Debug("updates.yaml:\n%s", data.c_str());

	if(!parseFeed(data))
	{
		return false;
	}

	saveToCache(data);
	return true;
}

bool Updater::refreshPending()
{
	return g_refreshPending;
}

// Background refresh of the cached updates.yaml, after the hooks are in. Only
// the file on disk changes; the in-memory list stays what init() loaded (the
// check already ran against it).
void Updater::refreshCache()
{
	std::string data;
	int res = Curl::getString(kFeedUrl, data);
	if(res == 0 && looksLikeFeed(data))
	{
		saveToCache(data);
		Log::Info("FeedRefresh: updates.yaml refreshed (%zu bytes)", data.size());
	}
	else
	{
		Log::Info("FeedRefresh: updates.yaml not refreshed (curl %d, %zu bytes) — keeping the copy", res, data.size());
	}
	g_refreshPending = false;
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

	// Write beside and rename: the copy is now read at every start, so a
	// half-written file (Steam exiting mid-refresh) must never be what the
	// next start finds.
	const std::string tmp = path + ".tmp";
	{
		std::ofstream stream = std::ofstream(tmp.c_str());
		stream << yaml;
	}
	std::error_code ec;
	std::filesystem::rename(tmp, path, ec);
	if(ec)
	{
		Log::Warn("updates.yaml: could not replace the cache (%s)", ec.message().c_str());
		std::filesystem::remove(tmp, ec);
		return;
	}

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
