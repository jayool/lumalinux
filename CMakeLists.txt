cmake_minimum_required(VERSION 3.20)
project(lumalinux LANGUAGES C CXX VERSION 0.14.0)

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

set(LUMA_SOURCES
    src/main.cpp
    src/log.cpp
    src/key_store.cpp
    src/patterns.cpp
    src/lmhook.cpp
    src/hooks/depot_key_hook.cpp
    src/hooks/depot_dependency_hook.cpp
    src/hooks/load_package_hook.cpp
    src/hooks/package_zero_finder.cpp
    src/hooks/gmrc_hook.cpp
    src/hooks/shader_depot_hook.cpp
    src/globals.cpp
    src/status.cpp
)

# SafeMode update-check (fetches res/updates.yaml, hashes steamclient.so). This
# is the ONLY part of lumalinux that pulls in libcurl + libcrypto. On a Steam
# runtime whose pinned libcurl/libcrypto predate the build host's (e.g. a
# generic Arch container whose Steam runtime ships curl < CURL_OPENSSL_4),
# linking those system libs makes the whole .so fail to preload into the Steam
# process (`version 'CURL_OPENSSL_4' not found`), so no hook ever installs.
# LUMA_NO_UPDATE compiles the gate out: the resulting .so has ZERO curl/openssl
# dependency and loads anywhere. The hooks, package-0 finder and keystore are
# byte-for-byte unaffected.
#
# Default ON since 0.13.6: leaving the libcurl link in broke EVERY game launch
# under the Steam Linux Runtime. steam.sh's LD_PRELOAD of liblumalinux.so is
# inherited by the 32-bit game `reaper`; when the runtime's pinned libcurl
# dropped the CURL_OPENSSL_4 symbol version (SteamOS 3.8's Arch base bump), the
# reaper failed to start (`version 'CURL_OPENSSL_4' not found`) → exit 1 → games
# bounced back to the library. Dropping the gate trades lumalinux's
# self-disable-on-unknown-steamclient safety for not bricking game launches; the
# hooks already no-op gracefully on a pattern miss, so the safety loss is minor.
# Set -DLUMA_NO_UPDATE=OFF to compile the update-check back in.
option(LUMA_NO_UPDATE "Build without the SafeMode update-check (drops the libcurl/libcrypto dependency)" ON)
if(NOT LUMA_NO_UPDATE)
    list(APPEND LUMA_SOURCES src/curl.cpp src/sha256.cpp src/update.cpp)
endif()

add_library(lumalinux SHARED ${LUMA_SOURCES})

if(LUMA_NO_UPDATE)
    target_compile_definitions(lumalinux PRIVATE LUMA_NO_UPDATE)
endif()

# libcurl + OpenSSL via pkg-config. Both must be the 32-bit variants:
#   apt: libcurl4-openssl-dev:i386 libssl-dev:i386
#   pacman: lib32-curl lib32-openssl
# Invoke cmake with PKG_CONFIG_PATH=/usr/lib/i386-linux-gnu/pkgconfig on
# multiarch Debian/Ubuntu so it picks the i386 .pc files, not the amd64 ones.
# Skipped entirely under LUMA_NO_UPDATE (no curl/openssl user is compiled in).
if(NOT LUMA_NO_UPDATE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CURL REQUIRED libcurl)
    pkg_check_modules(OPENSSL REQUIRED libcrypto)
endif()

target_include_directories(lumalinux PRIVATE
    src/
    include/
    ${CMAKE_CURRENT_BINARY_DIR}/generated/
    ${CURL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
)
target_link_libraries(lumalinux PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/liblibmem.a
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/libyaml-cpp.a
    ${CMAKE_DL_LIBS}
    ${CURL_LIBRARIES}
    ${OPENSSL_LIBRARIES}
)

set_target_properties(lumalinux PROPERTIES
    OUTPUT_NAME "lumalinux"
    PREFIX "lib"
    SUFFIX ".so"
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(lumalinux PROPERTIES LINK_FLAGS_RELEASE "-s")
endif()


