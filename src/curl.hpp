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
	// userAgent: when non-null, sets CURLOPT_USERAGENT. The GMRC providers pass
	// their own (`lumalinux/<version>`, `ManifestDeX/1.0`; gmrc_store.hpp). It
	// started with manifest.opensteamtool.com, dropped from the cascade in
	// v0.21.0, whose Cloudflare WAF challenged the default curl/libcurl UA but
	// let any other through. A total/connect timeout is always applied so a
	// hung endpoint can never block the calling thread (GMRC runs on Steam's
	// manifest-request path).
	//
	// connectTimeoutSec / totalTimeoutSec bound the request. The defaults match
	// the long-standing SafeMode/GMRC values; the ShaderDepot liveness probe
	// (Gmrc::ProvidersReachable) passes short values so a wedged provider can't
	// stall the shader pre-cache job for the full 30s.
	//
	// httpStatus: when non-null, receives the HTTP response code of the final
	// response (0 if curl could not report one). The GMRC cascade uses it to
	// tell a Cloudflare 429 (rate-limited: wait and retry) from a 401/403 (the
	// provider has no licence for that depot: give up on it) — both come back
	// as rc=0 with a short text body, indistinguishable from the body alone.
	//
	// byteRange: when non-null, sets CURLOPT_RANGE (e.g. "0-0") so a probe of a
	// large object costs one byte instead of the whole body — the GMRC cascade
	// checks a request code against Valve's CDN this way before handing it to
	// Steam (the manifest itself is hundreds of KB).
	int getString(const char* url, std::string& out, const char* userAgent = nullptr,
	              long connectTimeoutSec = 15, long totalTimeoutSec = 30,
	              long* httpStatus = nullptr, const char* byteRange = nullptr);
}
