// smoke_dlopen.c — minimal 32-bit host for the runtime smoke test.
//
// It dlopen()s the CURRENT steamclient.so and then just waits. That is enough,
// because lumalinux's LD_PRELOAD path does not need Steam: its constructor
// starts a thread that polls /proc/self/maps for steamclient.so and installs the
// hooks as soon as it appears (src/main.cpp, LumalinuxCtor). The package-0
// finder then derives its addresses off the same mapping.
//
// This deliberately does NOT try to be Steam. The old harness ran the real
// client out of a 329 MB tarball pinned in June and never got as far as mapping
// steamclient.so at all, so it asserted nothing while looking like it did. What
// the resolvers actually need is the library mapped in a 32-bit process — so
// that is exactly what this provides, deterministically and in seconds.
//
// What it therefore does NOT cover, and must not be claimed to: SLSsteam and
// CloudRedirect loaded alongside, Steam's own threads mutating the package
// cache, and anything that needs a logged-in session (package 0 included).
//
// Run it with LD_PRELOAD=liblumalinux.so and LUMA_PROCESS_ANY=1 (lumalinux
// otherwise limits itself to processes named steam/steamwebhelper).
//
//   gcc -m32 -o smoke_dlopen tools/smoke_dlopen.c -ldl
//   LUMA_PROCESS_ANY=1 LD_PRELOAD=... ./smoke_dlopen <tree>/steamclient.so 15
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <path/to/steamclient.so> [seconds]\n", argv[0]);
        return 2;
    }
    int secs = (argc > 2) ? atoi(argv[2]) : 15;
    if (secs < 1) secs = 1;

    // RTLD_LAZY: steamclient.so has undefined symbols it resolves against the
    // rest of the client at run time. We are not calling into it — we only need
    // it MAPPED — so binding lazily avoids failing on symbols nobody will use.
    void *h = dlopen(argv[1], RTLD_LAZY | RTLD_LOCAL);
    if (!h) {
        fprintf(stderr, "smoke_dlopen: dlopen failed: %s\n", dlerror());
        return 1;
    }
    fprintf(stderr, "smoke_dlopen: mapped %s; holding %d s for the hook installer\n",
            argv[1], secs);
    // lumalinux's poller ticks once a second and sleeps 200 ms before installing,
    // so a few seconds is plenty; the finder resolves on its first pass.
    sleep(secs);
    fprintf(stderr, "smoke_dlopen: done\n");
    return 0;
}
