// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 jayool — https://github.com/jayool/lumalinux
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

// Desktop toast via notify-send (same mechanism SLSsteam uses), titled
// "lumalinux". Also written to the log. Disabled if LUMA_NO_NOTIFY is set.
void Notify(const char* fmt, ...);

} // namespace Log
