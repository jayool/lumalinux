// SPDX-License-Identifier: GPL-3.0-only
#include "proc_filter.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

bool Probe() {
    if (std::getenv("LUMA_PROCESS_ANY")) return true;

    FILE* f = std::fopen("/proc/self/comm", "r");
    if (!f) return true;  // fail-open: if we can't read /proc, allow

    char comm[256] = {0};
    char* got = std::fgets(comm, sizeof(comm), f);
    std::fclose(f);
    if (!got) return true;  // fail-open

    // Trim the trailing newline that /proc/self/comm always includes
    size_t len = std::strlen(comm);
    if (len > 0 && comm[len - 1] == '\n') comm[len - 1] = '\0';

    static const char* const allowed[] = {
        "steam",
        "steamwebhelper",
    };
    for (const char* a : allowed) {
        if (std::strcmp(comm, a) == 0) return true;
    }
    return false;
}

} // namespace

namespace ProcFilter {

bool IsSteamHost() {
    // /proc/self/comm does not change under us for the lifetime of the process
    // (a process that exec's gets a fresh address space and a fresh copy of
    // this library), so one probe is enough. Function-local static => the
    // initialisation is thread-safe and lazy, which matters because the dlopen
    // interposer can call this arbitrarily early.
    static const bool cached = Probe();
    return cached;
}

} // namespace ProcFilter
