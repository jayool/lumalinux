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
	//   CURLOPT_NOSIGNAL       = CURLOPTTYPE_LONG        + 99  = 99
	//   CURLOPT_RANGE          = CURLOPTTYPE_OBJECTPOINT + 7   = 10007
	//   CURLOPT_WRITEDATA      = CURLOPTTYPE_OBJECTPOINT + 1   = 10001
	//   CURLOPT_URL            = CURLOPTTYPE_OBJECTPOINT + 2   = 10002
	//   CURLOPT_USERAGENT      = CURLOPTTYPE_OBJECTPOINT + 18  = 10018
	//   CURLOPT_WRITEFUNCTION  = CURLOPTTYPE_FUNCTIONPOINT + 11 = 20011
	constexpr int kCurloptTimeout        = 13;
	constexpr int kCurloptFollowlocation = 52;
	constexpr int kCurloptConnecttimeout = 78;
	constexpr int kCurloptNosignal       = 99;
	constexpr int kCurloptRange          = 10007;
	constexpr int kCurloptWritedata      = 10001;
	constexpr int kCurloptUrl            = 10002;
	constexpr int kCurloptUseragent      = 10018;
	constexpr int kCurloptWritefunction  = 20011;
	constexpr int kCurleOk               = 0;
	//   CURLINFO_RESPONSE_CODE = CURLINFO_LONG + 2 = 0x200002
	constexpr int kCurlinfoResponseCode  = 0x200002;

	using CurlEasyInit    = void* (*)();
	using CurlEasySetopt  = int   (*)(void*, int, ...);
	using CurlEasyPerform = int   (*)(void*);
	using CurlEasyCleanup = void  (*)(void*);
	using CurlEasyGetinfo = int   (*)(void*, int, ...);

	size_t writeCallback(const char* content, size_t size, size_t memberSize, std::string* data)
	{
		data->append(content, size * memberSize);
		return size * memberSize;
	}
}

int Curl::getString(const char* url, std::string& out, const char* userAgent,
                    long connectTimeoutSec, long totalTimeoutSec, long* httpStatus,
                    const char* byteRange)
{
	if (httpStatus) *httpStatus = 0;
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
	// Optional: only needed to report the HTTP status; its absence is not fatal.
	auto easyGetinfo = reinterpret_cast<CurlEasyGetinfo>(dlsym(lib, "curl_easy_getinfo"));
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
	easySetopt(curl, kCurloptConnecttimeout, (long)connectTimeoutSec);
	easySetopt(curl, kCurloptTimeout,        (long)totalTimeoutSec);
	// No signals. A libcurl built with the synchronous resolver (the Steam
	// Runtime's 7.22 is one) enforces CURLOPT_TIMEOUT with alarm() and a
	// siglongjmp from the SIGALRM handler. Inside Steam, a process with dozens
	// of threads, the signal lands on whichever thread is running, not on the
	// one that armed it; glibc's __longjmp_chk then sees a frame from another
	// stack and aborts the whole client (seen on 2026-10-02: three cores in one
	// hour, main thread in steamui, handler in libcurl.so.4). The system libcurl
	// we pin to has the threaded resolver and never takes that path, so this is
	// a no-op there and a crash-to-slow-DNS trade on the Runtime copy, where the
	// name lookup is then bounded by the resolver's own timeout instead of ours.
	// libcurl's documented requirement for any multi-threaded program.
	easySetopt(curl, kCurloptNosignal,       (long)1);
	if (userAgent)
		easySetopt(curl, kCurloptUseragent, userAgent);
	if (byteRange)
		easySetopt(curl, kCurloptRange, byteRange);

	int res = easyPerform(curl);

	if (httpStatus && easyGetinfo && res == kCurleOk)
	{
		long code = 0;
		if (easyGetinfo(curl, kCurlinfoResponseCode, &code) == kCurleOk)
			*httpStatus = code;
	}

	easyCleanup(curl);

	return res == kCurleOk ? 0 : res;
}
