#!/usr/bin/env bash
set -euo pipefail

if [[ ! -r /etc/os-release ]]; then
  echo "无法识别操作系统。"
  exit 1
fi

source /etc/os-release
if [[ "${ID:-}" != "ubuntu" || "${VERSION_CODENAME:-}" != "jammy" ]]; then
  echo "该脚本只用于 Ubuntu 22.04 (jammy)，当前系统：${PRETTY_NAME:-unknown}"
  exit 1
fi

task_tmp_dir="$(mktemp -d)"
trap 'rm -rf "${task_tmp_dir}"' EXIT

echo "[1/5] 下载 RealSense 官方仓库签名密钥..."
curl -sSfL \
  https://librealsense.realsenseai.com/Debian/librealsenseai.asc \
  -o "${task_tmp_dir}/librealsenseai.asc"

gpg --dearmor --yes \
  --output "${task_tmp_dir}/librealsenseai.gpg" \
  "${task_tmp_dir}/librealsenseai.asc"

echo "[2/5] 安装仓库签名密钥..."
sudo install -D -m 0644 \
  "${task_tmp_dir}/librealsenseai.gpg" \
  /etc/apt/keyrings/librealsenseai.gpg

echo "[3/5] 配置 RealSense 官方软件源..."
echo "deb [signed-by=/etc/apt/keyrings/librealsenseai.gpg] https://librealsense.realsenseai.com/Debian/apt-repo jammy main" \
  | sudo tee /etc/apt/sources.list.d/librealsense.list >/dev/null

echo "[4/5] 更新软件包索引..."
sudo apt-get update

echo "[5/5] 安装 RealSense SDK、Viewer 和开发头文件..."
sudo apt-get install -y librealsense2-utils librealsense2-dev

echo
echo "安装完成。"
echo "Viewer 命令：realsense-viewer"
echo "设备检查命令：rs-enumerate-devices"
echo
read -r -p "按 Enter 关闭此窗口..."
