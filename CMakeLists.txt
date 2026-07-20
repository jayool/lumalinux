cmake_minimum_required(VERSION 3.20)
project(lumalinux LANGUAGES C CXX VERSION 0.16.15)

# SafeMode VERSION (SLSsteam-style): the active group key in res/updates.yaml.
# Bumped only when src/patterns.hpp changes (= a Steam update broke the prior
# hooks and we shipped new patterns). Within the same VERSION, new compatible
# steamclient.so hashes can be appended to res/updates.yaml without recompiling.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/res/version.txt lumalinux_SAFEMODE_VERSION)
string(STRIP "${lumalinux_SAFEMODE_VERSION}" lumalinux_SAFEMODE_VERSION)

# Generate version.hpp from version.hpp.in so source code references a single
# version macro (LUMALINUX_VERSION_STRING) and bumps happen in one place
# (the project() call above). Also embeds LUMALINUX_SAFEMODE_VERSION read above.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/version.hpp
    @ONLY
)

# Steam Linux client is 32-bit. We MUST build as 32-bit to be loadable via LD_PRELOAD.
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -m32")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32 -D_GLIBCXX_USE_CXX11_ABI=0")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -m32")
set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} -m32")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter -fvisibility=hidden)
endif()

# -fno-reorder-blocks-and-partition (GCC only): keep exception landing pads out
# of the .cold section. At -O2+ block partitioning can strand a throw's landing
# pad in .cold so the call-site table fails to reach it, and even catch(...) is
# bypassed -> the process aborts. src/update.cpp parses updates.yaml with
# YAML::Load inside try/catch, so a corrupt/truncated updates.yaml could hit
# exactly that. Layout-only flag: zero behaviour change, negligible perf cost.
# Mirrors slsteam-moon's fix (see docs/slsteam-moon-findings.md, Delta 2026-07-10).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fno-reorder-blocks-and-partition)
endif()

set(LUMA_SOURCES
    src/main.cpp
    src/log.cpp
    src/key_store.cpp
    src/license_reconcile.cpp
    src/patterns.cpp
    src/rtti.cpp
    src/sls_update_unblock.cpp
    src/sls_achievement_unblock.cpp
    src/lmhook.cpp
    src/hooks/depot_key_hook.cpp
    src/hooks/depot_dependency_hook.cpp
    src/hooks/load_package_hook.cpp
    src/hooks/package_zero_finder.cpp
    src/hooks/gmrc_hook.cpp
    src/hooks/shader_depot_hook.cpp
    src/globals.cpp
    src/status.cpp
    src/curl.cpp
)

# SafeMode update-check (fetches res/updates.yaml, hashes steamclient.so).
#
# History: SafeMode used to LINK libcurl + libcrypto. steam.sh LD_PRELOADs
# liblumalinux.so into EVERY child process, including the 32-bit game `reaper`,
# so the loader resolved curl/openssl's versioned symbols there too; when the
# Steam Linux Runtime's pinned libcurl dropped the CURL_OPENSSL_4 symbol version
# (SteamOS 3.8's Arch base bump), the reaper failed to load the .so → games
# bounced. The emergency fix (0.13.6) was to compile SafeMode out by default
# (LUMA_NO_UPDATE=ON), which shipped releases WITHOUT the hash gate.
#
# 0.15.0 removes the toxic link entirely instead: src/sha256.cpp is now a
# self-contained SHA-256 (no libcrypto) and src/curl.cpp dlopen()s libcurl
# lazily at runtime (no NEEDED entry, dlsym binds the default symbol version).
# A SafeMode build therefore has ZERO curl/openssl link dependency and loads
# anywhere — the reaper brick can't happen — so releases can ship SafeMode ON
# again (the release workflow passes -DLUMA_NO_UPDATE=OFF).
#
# The option still defaults ON purely as a build convenience for gate-less
# validation builds (verify-fix.yml, codespace harnesses) that run against an
# unwhitelisted steamclient.so. Pass -DLUMA_NO_UPDATE=OFF to compile the gate in.
# NOTE: src/curl.cpp is compiled UNCONDITIONALLY (in LUMA_SOURCES above), not
# only for SafeMode. Besides the update-check, the GMRC provider cascade
# (src/gmrc_store.hpp) now uses Curl::getString for its HTTPS endpoints
# (manifest.opensteamtool.com, manifest.steam.run) — so it must be present even
# in LUMA_NO_UPDATE builds. curl.cpp is dlopen-lazy with ZERO link cost (no
# NEEDED entry), so compiling it always is free and keeps the reaper-safe
# property intact. Only sha256.cpp/update.cpp (the actual SafeMode gate) stay
# gated below.
option(LUMA_NO_UPDATE "Build without the SafeMode update-check (no hash gate)" ON)
if(NOT LUMA_NO_UPDATE)
    list(APPEND LUMA_SOURCES src/sha256.cpp src/update.cpp)
endif()

add_library(lumalinux SHARED ${LUMA_SOURCES})

if(LUMA_NO_UPDATE)
    target_compile_definitions(lumalinux PRIVATE LUMA_NO_UPDATE)
endif()

# SafeMode no longer links libcurl or libcrypto: sha256.cpp is self-contained
# and curl.cpp dlopen()s libcurl at runtime (see those files). The only extra
# system dependency is libdl (CMAKE_DL_LIBS) for dlopen/dlsym, already linked
# below. No pkg-config / -dev packages are needed even for a SafeMode build.

target_include_directories(lumalinux PRIVATE
    src/
    include/
    ${CMAKE_CURRENT_BINARY_DIR}/generated/
)
target_link_libraries(lumalinux PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/liblibmem.a
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/libyaml-cpp.a
    ${CMAKE_DL_LIBS}
)

set_target_properties(lumalinux PROPERTIES
    OUTPUT_NAME "lumalinux"
    PREFIX "lib"
    SUFFIX ".so"
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(lumalinux PROPERTIES LINK_FLAGS_RELEASE "-s")
endif()


