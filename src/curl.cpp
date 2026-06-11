// SPDX-License-Identifier: AGPL-3.0-only
// Originally from https://github.com/AceSLS/SLSsteam (AGPL-3.0)
// Imported unchanged (only adapted to lumalinux's logger).
#include "curl.hpp"

#include "log.hpp"

#include <curl/curl.h>
#include <curl/easy.h>

static CURL* curl = nullptr;

static size_t writeCallback(const char* content, size_t size, size_t memberSize, std::string* data)
{
	data->append(content, size * memberSize);
	return size * memberSize;
}

int Curl::getString(const char* url, std::string& out)
{
	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);

	auto res = curl_easy_perform(curl);

	curl_easy_cleanup(curl);

	return res;
}
