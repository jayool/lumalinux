// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 jayool — https://github.com/jayool/lumalinux
//
// x86_64 build only. The prebuilt lib/x86_64/liblibmem.a (rdbo/libmem 5.1.5,
// built with a GCC ≥ 11 libstdc++) references std::__throw_bad_array_new_length,
// which the Steam Runtime 3 libstdc++ (GCC 10) does not define. The library is
// linked with -Wl,--no-undefined and must load inside that runtime, so the
// symbol is provided here, hidden, doing exactly what libstdc++'s does.
#include <new>

namespace std {
__attribute__((visibility("hidden"), noreturn))
void __throw_bad_array_new_length() { throw bad_array_new_length(); }
}
