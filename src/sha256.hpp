// SPDX-License-Identifier: AGPL-3.0-only
// Originally from https://github.com/AceSLS/SLSsteam (AGPL-3.0)
// Imported from src/utils.hpp; only the SHA256 helper was needed.
#pragma once

#include <string>


namespace Utils
{
	std::string getFileSHA256(const char* filePath);
}
