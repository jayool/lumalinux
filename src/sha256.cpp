// SPDX-License-Identifier: AGPL-3.0-only
// Originally from https://github.com/AceSLS/SLSsteam (AGPL-3.0)
// Imported from src/utils.cpp; only the SHA256 helper was needed.
#include "sha256.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <openssl/sha.h>

std::string Utils::getFileSHA256(const char *filePath)
{
	std::ifstream fs(filePath, std::ios::binary);
	if (!fs.is_open())
	{
		throw std::runtime_error("Unable to read file!");
	}

	std::vector<unsigned char> bytes(std::istreambuf_iterator<char>(fs), {});
	unsigned char sha256Bytes[SHA256_DIGEST_LENGTH];
	SHA256(bytes.data(), bytes.size(), sha256Bytes);

	std::stringstream sha256;
	for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
	{
		sha256 << std::hex << std::setw(2) << std::setfill('0') << (int)sha256Bytes[i];
	}

	fs.close();
	return sha256.str();
}
