#!/usr/bin/env bash
# Fetches the pre-built libmem static library + headers from the official
# release on github.com/rdbo/libmem and drops them into lib/ + include/libmem/
# so cmake can find them. Pinned to a specific version so the build is
# reproducible — bump LIBMEM_VERSION and LIBMEM_SHA256 together when upgrading.
#
# Idempotent: if both artifacts already exist, exits 0 without re-downloading.
#
# Used by:
#   - .github/workflows/build.yml (CI)
#   - developers building locally (run once after `git clone`)
set -euo pipefail

LIBMEM_VERSION="5.1.5"
# LUMA_ARCH=i686 (default, the 32-bit client) or x86_64 (the steamrt64 client,
# CMake -DLUMA_X86_64=ON). The 64-bit copy lives under lib/x86_64/ with its
# own headers so the two never mix.
LUMA_ARCH="${LUMA_ARCH:-i686}"
case "${LUMA_ARCH}" in
    i686)
        LIBMEM_SHA256="644b4897814aff5bca6323b45f1088922309c6048e705662370fcabe6d7e9048"
        LIB_SUBDIR="lib"; INC_SUBDIR="include/libmem" ;;
    x86_64)
        LIBMEM_SHA256="20fcdf4595b14bb6f85f57f1b375c147cf1655f009680f878215df21ac85164a"
        LIB_SUBDIR="lib/x86_64"; INC_SUBDIR="lib/x86_64/include/libmem" ;;
    *) echo "fetch_libmem.sh: LUMA_ARCH must be i686 or x86_64 (got '${LUMA_ARCH}')" >&2; exit 2 ;;
esac
LIBMEM_ASSET="libmem-${LIBMEM_VERSION}-${LUMA_ARCH}-linux-gnu-static.tar.gz"
LIBMEM_URL="https://github.com/rdbo/libmem/releases/download/${LIBMEM_VERSION}/${LIBMEM_ASSET}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIB_DIR="${REPO_ROOT}/${LIB_SUBDIR}"
INC_DIR="${REPO_ROOT}/${INC_SUBDIR}"
LIB_FILE="${LIB_DIR}/liblibmem.a"
INC_FILE="${INC_DIR}/libmem.h"

if [[ -f "${LIB_FILE}" && -f "${INC_FILE}" ]]; then
    echo "fetch_libmem.sh: ${LIB_FILE} and ${INC_DIR} already present — skip."
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

echo "fetch_libmem.sh: downloading libmem ${LIBMEM_VERSION} (${LUMA_ARCH} linux gnu static)"
curl -fsSL "${LIBMEM_URL}" -o "${TMP}/libmem.tar.gz"

actual_sha="$(sha256sum "${TMP}/libmem.tar.gz" | cut -d' ' -f1)"
if [[ "${actual_sha}" != "${LIBMEM_SHA256}" ]]; then
    echo "fetch_libmem.sh: SHA256 mismatch!" >&2
    echo "  expected: ${LIBMEM_SHA256}" >&2
    echo "  got:      ${actual_sha}" >&2
    exit 1
fi

tar -xzf "${TMP}/libmem.tar.gz" -C "${TMP}"
EXTRACTED="${TMP}/libmem-${LIBMEM_VERSION}-${LUMA_ARCH}-linux-gnu-static"

mkdir -p "${LIB_DIR}" "${INC_DIR}"
cp -f "${EXTRACTED}/lib/liblibmem.a" "${LIB_FILE}"
cp -rf "${EXTRACTED}/include/libmem/." "${INC_DIR}/"

echo "fetch_libmem.sh: installed:"
ls -la "${LIB_FILE}" "${INC_DIR}"
