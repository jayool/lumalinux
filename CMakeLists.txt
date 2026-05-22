cmake_minimum_required(VERSION 3.20)
project(lumalinux LANGUAGES C CXX VERSION 0.4.0)

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

include(FetchContent)

set(SUBHOOK_STATIC       ON  CACHE BOOL "")
set(SUBHOOK_TESTS        OFF CACHE BOOL "")
set(SUBHOOK_INSTALL      OFF CACHE BOOL "")
set(SUBHOOK_FORCE_32BIT  ON  CACHE BOOL "")

FetchContent_Declare(
    subhook
    GIT_REPOSITORY https://github.com/Cxbx-Reloaded/subhook.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(subhook)

add_library(lumalinux SHARED
    src/main.cpp
    src/log.cpp
    src/key_store.cpp
    src/patterns.cpp
    src/hooks/depot_key_hook.cpp
    src/hooks/depot_dependency_hook.cpp
    src/hooks/keyvalues_hook.cpp
)

target_include_directories(lumalinux PRIVATE src/)
target_link_libraries(lumalinux PRIVATE subhook)

set_target_properties(lumalinux PROPERTIES
    OUTPUT_NAME "lumalinux"
    PREFIX "lib"
    SUFFIX ".so"
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(lumalinux PROPERTIES LINK_FLAGS_RELEASE "-s")
endif()
