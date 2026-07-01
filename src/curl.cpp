// SPDX-License-Identifier: AGPL-3.0-only
// Originally from https://github.com/AceSLS/SLSsteam (AGPL-3.0).
// Reworked for lumalinux: libcurl is loaded with dlopen() AT RUNTIME instead of
// being linked at load time. Rationale — steam.sh LD_PRELOADs liblumalinux.so
// into EVERY child process, including the 32-bit game `reaper`. A hard NEEDED
// dependency on libcurl made the loader resolve curl's versioned symbols in
// those processes too; when the Steam runtime's pinned libcurl dropped the
// CURL_OPENSSL_4 symbol version, liblumalinux.so failed to load into `reaper`
// and games bounced (the bug that forced LUMA_NO_UPDATE=ON in 0.13.6).
// dlopen()ing curl lazily, only here (SafeMode runs solely in the Steam client
// process, and main.cpp aborts before this if steamclient.so isn't present),
// removes the load-time dependency entirely: the .so loads anywhere, and
// dlsym() binds the default symbol version, sidestepping the version mismatch.
// If curl can't be loaded, getString() fails and the Updater falls back to its
// on-disk cache (and ultimately fail-closed).
#include "curl.hpp"

#include "log.hpp"

#include <cstddef>
#include <dlfcn.h>
#include <string>

namespace
{
	// libcurl public ABI constants. These are part of the stable curl ABI and
	// never change, so hardcoding them lets us drop the libcurl-dev build
	// dependency too (no <curl/curl.h> needed).
	//   CURLOPT_TIMEOUT        = CURLOPTTYPE_LONG        + 13  = 13
	//   CURLOPT_FOLLOWLOCATION = CURLOPTTYPE_LONG        + 52  = 52
	//   CURLOPT_CONNECTTIMEOUT = CURLOPTTYPE_LONG        + 78  = 78
	//   CURLOPT_WRITEDATA      = CURLOPTTYPE_OBJECTPOINT + 1   = 10001
	//   CURLOPT_URL            = CURLOPTTYPE_OBJECTPOINT + 2   = 10002
	//   CURLOPT_USERAGENT      = CURLOPTTYPE_OBJECTPOINT + 18  = 10018
	//   CURLOPT_WRITEFUNCTION  = CURLOPTTYPE_FUNCTIONPOINT + 11 = 20011
	constexpr int kCurloptTimeout        = 13;
	constexpr int kCurloptFollowlocation = 52;
	constexpr int kCurloptConnecttimeout = 78;
	constexpr int kCurloptWritedata      = 10001;
	constexpr int kCurloptUrl            = 10002;
	constexpr int kCurloptUseragent      = 10018;
	constexpr int kCurloptWritefunction  = 20011;
	constexpr int kCurleOk               = 0;

	using CurlEasyInit    = void* (*)();
	using CurlEasySetopt  = int   (*)(void*, int, ...);
	using CurlEasyPerform = int   (*)(void*);
	using CurlEasyCleanup = void  (*)(void*);

	size_t writeCallback(const char* content, size_t size, size_t memberSize, std::string* data)
	{
		data->append(content, size * memberSize);
		return size * memberSize;
	}
}

int Curl::getString(const char* url, std::string& out, const char* userAgent)
{
	// RTLD_LOCAL so curl's symbols don't leak into the Steam process namespace.
	// The handle is intentionally left open for the process lifetime: SafeMode
	// fetches once at startup, and dlclose()ing libcurl could tear down global
	// state it lazily initialised in curl_easy_init().
	void* lib = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
	if (!lib)
	{
		Log::Error("Curl: dlopen(libcurl.so.4) failed (%s) — SafeMode will fall back to cache", dlerror());
		return -1;
	}

	auto easyInit    = reinterpret_cast<CurlEasyInit>(dlsym(lib, "curl_easy_init"));
	auto easySetopt  = reinterpret_cast<CurlEasySetopt>(dlsym(lib, "curl_easy_setopt"));
	auto easyPerform = reinterpret_cast<CurlEasyPerform>(dlsym(lib, "curl_easy_perform"));
	auto easyCleanup = reinterpret_cast<CurlEasyCleanup>(dlsym(lib, "curl_easy_cleanup"));
	if (!easyInit || !easySetopt || !easyPerform || !easyCleanup)
	{
		Log::Error("Curl: dlsym of curl_easy_* failed — SafeMode will fall back to cache");
		return -1;
	}

	void* curl = easyInit();
	if (!curl)
	{
		Log::Error("Curl: curl_easy_init() returned null");
		return -1;
	}

	easySetopt(curl, kCurloptUrl, url);
	easySetopt(curl, kCurloptWritefunction, writeCallback);
	easySetopt(curl, kCurloptWritedata, &out);
	// Follow http->https redirects (opensteamtool/steam.run are HTTPS) and cap
	// the request so a wedged endpoint can never hang the caller. These options
	// take a `long`; the value MUST be passed with the right type through the
	// variadic curl_easy_setopt or the ABI read is garbage.
	easySetopt(curl, kCurloptFollowlocation, (long)1);
	easySetopt(curl, kCurloptConnecttimeout, (long)15);
	easySetopt(curl, kCurloptTimeout,        (long)30);
	if (userAgent)
		easySetopt(curl, kCurloptUseragent, userAgent);

	int res = easyPerform(curl);

	easyCleanup(curl);

	return res == kCurleOk ? 0 : res;
}
