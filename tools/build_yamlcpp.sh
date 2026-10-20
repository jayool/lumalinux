#!/usr/bin/env bash
# Builds yaml-cpp as a static, position-independent x86_64 library for the
# LUMA_X86_64 build (CMake -DLUMA_X86_64=ON) and installs it under lib/x86_64/
# (liblibmem.a's neighbour, see tools/fetch_libmem.sh) with its headers.
#
# Why not the runtime SDK's libyaml-cpp-dev: Debian's static libyaml-cpp.a is
# not PIC (R_X86_64_PC32 relocations), so it cannot be linked into a shared
# object; its shared library would add a NEEDED entry the Steam Runtime 3
# platform does not promise. The 32-bit build keeps the vendored
# lib/libyaml-cpp.a + include/yaml-cpp (old libstdc++ ABI); this one is built
# with the default (new) ABI, like the rest of the 64-bit build.
#
# Pinned to the 0.8.0 tag's commit. Idempotent.
set -euo pipefail

YAMLCPP_TAG="0.8.0"
YAMLCPP_COMMIT="f7320141120f720aecc4c32be25586e7da9eb978"
YAMLCPP_REPO="https://github.com/jbeder/yaml-cpp"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIB_DIR="${REPO_ROOT}/lib/x86_64"
LIB_FILE="${LIB_DIR}/libyaml-cpp.a"
INC_DIR="${LIB_DIR}/include/yaml-cpp"

if [[ -f "${LIB_FILE}" && -f "${INC_DIR}/yaml.h" ]]; then
    echo "build_yamlcpp.sh: ${LIB_FILE} and ${INC_DIR} already present — skip."
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

echo "build_yamlcpp.sh: cloning yaml-cpp ${YAMLCPP_TAG}"
git clone -q --depth 1 --branch "${YAMLCPP_TAG}" "${YAMLCPP_REPO}" "${TMP}/src"
got="$(git -C "${TMP}/src" rev-parse HEAD)"
if [[ "${got}" != "${YAMLCPP_COMMIT}" ]]; then
    echo "build_yamlcpp.sh: tag ${YAMLCPP_TAG} is at ${got}, expected ${YAMLCPP_COMMIT}" >&2
    exit 1
fi

cmake -S "${TMP}/src" -B "${TMP}/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_C_FLAGS=-m64 -DCMAKE_CXX_FLAGS=-m64 \
    -DYAML_BUILD_SHARED_LIBS=OFF \
    -DYAML_CPP_BUILD_TESTS=OFF -DYAML_CPP_BUILD_TOOLS=OFF -DYAML_CPP_BUILD_CONTRIB=OFF \
    -DYAML_CPP_INSTALL=OFF >/dev/null
cmake --build "${TMP}/build" >/dev/null

mkdir -p "${LIB_DIR}" "${INC_DIR}"
cp -f "${TMP}/build/libyaml-cpp.a" "${LIB_FILE}"
cp -rf "${TMP}/src/include/yaml-cpp/." "${INC_DIR}/"

echo "build_yamlcpp.sh: installed:"
ls -la "${LIB_FILE}"; ls "${INC_DIR}" | head -3
