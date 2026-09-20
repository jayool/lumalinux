// Test-only glue: minimal Log:: so cr_stats_fix.cpp links without the rest of
// lumalinux, and an exported entry to run CrStatsFix::Init() from the test.
#include "cr_stats_fix.hpp"
#include "log.hpp"
#include <cstdarg>
#include <cstdio>
namespace Log {
static void out(const char* lvl, const char* fmt, va_list ap) { std::fprintf(stderr, "[%s] ", lvl); std::vfprintf(stderr, fmt, ap); std::fputc('\n', stderr); }
void Info(const char* fmt, ...)  { va_list ap; va_start(ap, fmt); out("info", fmt, ap); va_end(ap); }
void Warn(const char* fmt, ...)  { va_list ap; va_start(ap, fmt); out("warn", fmt, ap); va_end(ap); }
void Error(const char* fmt, ...) { va_list ap; va_start(ap, fmt); out("error", fmt, ap); va_end(ap); }
void Debug(const char* fmt, ...) { va_list ap; va_start(ap, fmt); out("debug", fmt, ap); va_end(ap); }
}
extern "C" __attribute__((visibility("default"))) int cr_stats_fix_test_init() {
    return static_cast<int>(CrStatsFix::Init());   // 0 Applied, 1 Disabled, 2 Failed
}
