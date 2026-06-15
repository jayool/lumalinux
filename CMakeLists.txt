cmake_minimum_required(VERSION 3.20)
project(lumalinux LANGUAGES C CXX VERSION 0.11.0)

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

add_library(lumalinux SHARED
    src/main.cpp
    src/log.cpp
    src/key_store.cpp
    src/patterns.cpp
    src/lmhook.cpp
    src/hooks/depot_key_hook.cpp
    src/hooks/depot_dependency_hook.cpp
    src/hooks/load_package_hook.cpp
    src/hooks/gmrc_hook.cpp
    src/globals.cpp
    src/curl.cpp
    src/sha256.cpp
    src/update.cpp
)

# libcurl + OpenSSL via pkg-config. Both must be the 32-bit variants:
#   apt: libcurl4-openssl-dev:i386 libssl-dev:i386
#   pacman: lib32-curl lib32-openssl
# Invoke cmake with PKG_CONFIG_PATH=/usr/lib/i386-linux-gnu/pkgconfig on
# multiarch Debian/Ubuntu so it picks the i386 .pc files, not the amd64 ones.
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(OPENSSL REQUIRED libcrypto)

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


