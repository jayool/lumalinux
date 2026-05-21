cmake_minimum_required(VERSION 3.20)
project(lumalinux LANGUAGES C CXX VERSION 0.1.0)

# Steam Linux client is 32-bit. We MUST build as 32-bit to be loadable via LD_PRELOAD.
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -m32")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32 -D_GLIBCXX_USE_CXX11_ABI=0")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -m32")
set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} -m32")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Compiler hardening + clean output
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter -fvisibility=hidden)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# libmem dependency
#
# libmem is the function-detour library SLSsteam also uses. We need a 32-bit
# build of it for our .so.
#
# Approach: try FetchContent from source. If that fails for your build setup,
# alternative paths documented in README:
#   - Vendor liblibmem.a from SLSsteam (it's GPL-3.0, same license as us)
#   - Build libmem manually with -m32 and point CMake at the install dir
# ─────────────────────────────────────────────────────────────────────────────

include(FetchContent)

set(LIBMEM_BUILD_STATIC ON  CACHE BOOL "")
set(LIBMEM_BUILD_TESTS  OFF CACHE BOOL "")

FetchContent_Declare(
    libmem
    GIT_REPOSITORY https://github.com/rdbo/libmem.git
    GIT_TAG        5.0.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(libmem)

# ─────────────────────────────────────────────────────────────────────────────
# Our shared library
# ─────────────────────────────────────────────────────────────────────────────

add_library(lumalinux SHARED
    src/main.cpp
    src/log.cpp
    src/key_store.cpp
    src/patterns.cpp
    src/hooks/depot_key_hook.cpp
)

target_include_directories(lumalinux PRIVATE src/)
target_link_libraries(lumalinux PRIVATE libmem)

set_target_properties(lumalinux PROPERTIES
    OUTPUT_NAME "lumalinux"
    PREFIX "lib"
    SUFFIX ".so"
)

# Strip in release builds — but keep version info readable
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(lumalinux PROPERTIES LINK_FLAGS_RELEASE "-s")
endif()
