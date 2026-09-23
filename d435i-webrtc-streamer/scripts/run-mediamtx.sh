#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
MEDIAMTX="${PROJECT_DIR}/third_party/mediamtx/mediamtx"

if [[ ! -x "${MEDIAMTX}" ]]; then
  echo "尚未下载 MediaMTX，请先运行 scripts/download-mediamtx.sh。" >&2
  exit 1
fi

if [[ $# -ge 1 ]]; then
  ADVERTISE_IP="$1"
else
  ADVERTISE_IP="$(hostname -I | awk '{print $1}')"
fi

if [[ -z "${ADVERTISE_IP}" ]]; then
  echo "无法自动识别局域网 IP，请把 IP 作为第一个参数传入。" >&2
  exit 1
fi

echo "WebRTC 对外地址: ${ADVERTISE_IP}"
echo "浏览器预览: http://${ADVERTISE_IP}:8889/d435i/"
echo "Vision Pro WHEP: http://${ADVERTISE_IP}:8889/d435i/whep"

cd "${PROJECT_DIR}/third_party/mediamtx"
MTX_WEBRTCADDITIONALHOSTS="${ADVERTISE_IP}" exec "${MEDIAMTX}" mediamtx.yml
