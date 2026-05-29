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
LIBMEM_ASSET="libmem-${LIBMEM_VERSION}-i686-linux-gnu-static.tar.gz"
LIBMEM_URL="https://github.com/rdbo/libmem/releases/download/${LIBMEM_VERSION}/${LIBMEM_ASSET}"
LIBMEM_SHA256="644b4897814aff5bca6323b45f1088922309c6048e705662370fcabe6d7e9048"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIB_DIR="${REPO_ROOT}/lib"
INC_DIR="${REPO_ROOT}/include/libmem"
LIB_FILE="${LIB_DIR}/liblibmem.a"
INC_FILE="${INC_DIR}/libmem.h"

if [[ -f "${LIB_FILE}" && -f "${INC_FILE}" ]]; then
    echo "fetch_libmem.sh: lib/liblibmem.a and include/libmem/ already present — skip."
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

echo "fetch_libmem.sh: downloading libmem ${LIBMEM_VERSION} (i686 linux gnu static)"
curl -fsSL "${LIBMEM_URL}" -o "${TMP}/libmem.tar.gz"

actual_sha="$(sha256sum "${TMP}/libmem.tar.gz" | cut -d' ' -f1)"
if [[ "${actual_sha}" != "${LIBMEM_SHA256}" ]]; then
    echo "fetch_libmem.sh: SHA256 mismatch!" >&2
    echo "  expected: ${LIBMEM_SHA256}" >&2
    echo "  got:      ${actual_sha}" >&2
    exit 1
fi

tar -xzf "${TMP}/libmem.tar.gz" -C "${TMP}"
EXTRACTED="${TMP}/libmem-${LIBMEM_VERSION}-i686-linux-gnu-static"

mkdir -p "${LIB_DIR}" "${INC_DIR}"
cp -f "${EXTRACTED}/lib/liblibmem.a" "${LIB_FILE}"
cp -rf "${EXTRACTED}/include/libmem/." "${INC_DIR}/"

echo "fetch_libmem.sh: installed:"
ls -la "${LIB_FILE}" "${INC_DIR}"
