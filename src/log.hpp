#pragma once

#include <string>
#include <cstdint>

namespace Log {

void Init();
void Shutdown();

void Info(const char* fmt, ...);
void Warn(const char* fmt, ...);
void Error(const char* fmt, ...);
void Debug(const char* fmt, ...);

} // namespace Log
