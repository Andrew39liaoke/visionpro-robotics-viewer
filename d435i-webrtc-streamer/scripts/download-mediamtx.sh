#!/usr/bin/env bash
set -euo pipefail

VERSION="1.19.3"
ARCH="$(uname -m)"

case "${ARCH}" in
  x86_64) PACKAGE_ARCH="amd64" ;;
  aarch64|arm64) PACKAGE_ARCH="arm64v8" ;;
  *)
    echo "暂不支持的 CPU 架构: ${ARCH}" >&2
    exit 1
    ;;
esac

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
TARGET_DIR="${PROJECT_DIR}/third_party/mediamtx"
ARCHIVE="mediamtx_v${VERSION}_linux_${PACKAGE_ARCH}.tar.gz"
URL="https://github.com/bluenviron/mediamtx/releases/download/v${VERSION}/${ARCHIVE}"

mkdir -p "${TARGET_DIR}"
curl --fail --location --retry 3 "${URL}" --output "${TARGET_DIR}/${ARCHIVE}"
tar -xzf "${TARGET_DIR}/${ARCHIVE}" -C "${TARGET_DIR}" mediamtx mediamtx.yml LICENSE
rm -f "${TARGET_DIR}/${ARCHIVE}"

echo "MediaMTX v${VERSION} 已安装到 ${TARGET_DIR}"
