// SPDX-License-Identifier: AGPL-3.0-only
// Originally from https://github.com/AceSLS/SLSsteam (AGPL-3.0)
// Imported unchanged.
#pragma once

#include <string>


namespace Curl
{
	// GET `url` into `out`. Returns 0 (CURLE_OK) on transport success — note
	// this does NOT check the HTTP status; callers must validate the body.
	//
	// userAgent: when non-null, sets CURLOPT_USERAGENT. The GMRC provider passes
	// a non-'curl' UA because manifest.opensteamtool.com's Cloudflare WAF
	// challenges the default curl/libcurl UA but lets any other UA through
	// (verified: `curl -A OpenSteamTool/1.0` returns the code where a bare curl
	// gets the JS challenge). A total/connect timeout is always applied so a
	// hung endpoint can never block the calling thread (GMRC runs on Steam's
	// manifest-request path).
	int getString(const char* url, std::string& out, const char* userAgent = nullptr);
}
