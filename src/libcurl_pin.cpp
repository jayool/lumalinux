// SPDX-License-Identifier: GPL-3.0-only
//
// libcurl pin — make CloudRedirect (and our own SafeMode/GMRC fetches) use the
// SYSTEM libcurl instead of the one bundled in the Steam Runtime.
//
// THE PROBLEM
// -----------
// Neither CloudRedirect nor lumalinux links libcurl: both dlopen() it by bare
// soname at runtime. Inside Steam that name resolves against Steam's own
// LD_LIBRARY_PATH, whose steam-runtime directories come FIRST — so the winner
// is the Runtime's copy: libcurl 7.22 / OpenSSL 1.0.1, built in 2021 against
// crypto from 2013. The system's libcurl 8.x / OpenSSL 3 sits later in the path
// and never gets a look in.
//
// That used to be corrected for us. SLSsteam shipped `library-inject.so`, an
// LD_AUDIT module whose only job was to rewrite libcurl lookups to the system
// directories. AceSLS disabled it on 2026-08-26 (its la_objsearch callback was
// breaking C++ exception unwinding) and their 20260903 release ships the file
// empty, so from that release on every consumer silently drops onto the 2013
// TLS stack. Measured, by swapping only that one file in a SteamOS container:
//
//     library-inject 26568 bytes  ->  Steam client uses libcurl 8.22 / OpenSSL 3
//     library-inject     0 bytes  ->  Steam client uses libcurl 7.22 / OpenSSL 1.0.1
//
// The old stack is not merely old: it resolves trust anchors through the HASHED
// SYMLINK INDEX in /etc/ssl/certs rather than the ca-certificates.crt bundle the
// modern one reads. On a machine where that index is broken it therefore finds
// no issuer for anything and every handshake fails with CURLE_PEER_FAILED_
// VERIFICATION (60) — while the rest of the system, which uses the modern
// libcurl, keeps working and hides the breakage. Reproduced both ways.
//
// WHY THIS SHAPE
// --------------
// We do NOT resurrect an la_objsearch auditor: that is the exact mechanism
// upstream removed, and it breaks exceptions process-wide. We do NOT prepend the
// system libcurl to LD_PRELOAD either: LD_PRELOAD is inherited by every child of
// Steam, so that would also force our libcurl onto games that ship their own —
// something library-inject never did, because it only rewrote LA_SER_ORIG
// lookups and left RPATH/RUNPATH alone.
//
// Instead we interpose dlopen() and rewrite exactly one kind of request. That is
// narrower than library-inject was (it covered load-time NEEDED resolution too),
// which is fine: measured inside a real Steam client, nothing loads libcurl
// before the first dlopen, so the dlopen path is the only one that exists here.
//
// SAFETY
// ------
// dlopen() is on the hot path for everything Steam does — GPU drivers, Vulkan
// layers, audio, plugins. A bug here does not break cloud saves, it breaks
// Steam. So the ordering below is deliberate, cheapest and most-likely-to-exit
// first, and EVERY branch that is not a positive match falls through to the real
// dlopen untouched:
//
//   1. bare soname starting with "libcurl"?   (a strchr + strncmp; ~all calls
//      leave here, and an absolute path is never rewritten — if a caller asked
//      for a specific file it gets that file)
//   2. kill switch not set?
//   3. a process that hosts steamclient.so?
//   4. did the call come from cloud_redirect.so or liblumalinux.so?
//      (dladdr on the return address; fail-CLOSED — an unidentified caller is
//      never redirected)
//   5. does a system copy actually exist?
//
// And the redirect itself is fail-open: if the rewritten dlopen returns null we
// retry the original name and hand that back. The worst case is that we do not
// redirect, which is exactly today's behaviour. This matters more than it looks:
// our own GMRC hook goes through here, and GMRC is what makes installs work, so
// a fail-closed bug would cost downloads rather than cloud saves.
//
// Kill switch: LUMA_NO_LIBCURL_FIX=1
#include "proc_filter.hpp"
#include "log.hpp"

#include <dlfcn.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

using DlopenFn = void* (*)(const char*, int);

// The real dlopen, resolved lazily. It MUST be lazy: this interposer is live
// from the moment the loader maps us, which can be before any constructor of
// ours has run, so there is no initialisation hook we could hang this off.
DlopenFn RealDlopen() {
    static DlopenFn real = reinterpret_cast<DlopenFn>(dlsym(RTLD_NEXT, "dlopen"));
    return real;
}

bool Disabled() {
    static const bool off = std::getenv("LUMA_NO_LIBCURL_FIX") != nullptr;
    return off;
}

// A bare soname, no directory component, in the libcurl family. Requiring the
// name to be bare is what keeps this from touching a caller that asked for one
// specific file by path — including our own rewritten call below, so there is
// no way to recurse.
bool IsBareLibcurlName(const char* name) {
    if (std::strchr(name, '/')) return false;
    return std::strncmp(name, "libcurl", 7) == 0;
}

bool Exists(const char* path) {
    struct stat st;
    return ::stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// Where a 32-bit system libcurl lives, per distro. Same list library-inject
// carried, kept in the same order.
const char* const kSystemDirs[] = {
    "/usr/lib32",               // SteamOS, Arch multilib
    "/usr/local/lib32",         // Bazzite
    "/usr/lib/i386-linux-gnu",  // Debian, Ubuntu, Mint
};

// Fail-closed on purpose: if we cannot positively attribute the call to
// CloudRedirect or to ourselves, we leave it alone.
bool CallerIsPinned(void* retAddr) {
    if (!retAddr) return false;
    Dl_info info{};
    if (!dladdr(retAddr, &info) || !info.dli_fname) return false;

    const char* base = std::strrchr(info.dli_fname, '/');
    base = base ? base + 1 : info.dli_fname;
    return std::strstr(base, "cloud_redirect") != nullptr
        || std::strstr(base, "liblumalinux")   != nullptr;
}

} // namespace

extern "C" __attribute__((visibility("default")))
void* dlopen(const char* file, int mode) {
    // Must be read here, in the interposed function itself.
    void* retAddr = __builtin_return_address(0);

    DlopenFn real = RealDlopen();
    if (!real)                          return nullptr;  // nothing we can do
    if (!file)                          return real(file, mode);  // RTLD handle for the main program
    if (!IsBareLibcurlName(file))       return real(file, mode);
    if (Disabled())                     return real(file, mode);
    if (!ProcFilter::IsSteamHost())     return real(file, mode);
    if (!CallerIsPinned(retAddr))       return real(file, mode);

    for (const char* dir : kSystemDirs) {
        // Fixed buffer, no allocation: dlopen can be entered from anywhere,
        // including contexts where taking the allocator would be unwise. A name
        // that does not fit is simply not one of ours.
        char path[512];
        int n = std::snprintf(path, sizeof path, "%s/%s", dir, file);
        if (n < 0 || static_cast<size_t>(n) >= sizeof path) continue;
        if (!Exists(path)) continue;
        if (void* handle = real(path, mode)) {
            // Log::* is a no-op until Log::Init() has opened the file, and we
            // must not call Log::Init() from here (it shells out to mkdir -p,
            // i.e. it forks, which is not something to do from an arbitrary
            // dlopen). An early redirect therefore goes unrecorded; a later one
            // — CloudRedirect's, which happens well after startup — does not.
            Log::Info("libcurl-pin: redirected '%s' -> %s", file, path);
            return handle;
        }
        break;  // it exists but will not load; fall through to the original
    }

    return real(file, mode);
}
