# D435i RGB WebRTC 推流端

本项目只实现第一项功能，不包含目标检测、深度流、IMU 或 ROS 实时建模：

```text
Intel RealSense D435i RGB8
  -> GStreamer videoconvert (NV12)
  -> H.264（优先硬件编码，软件编码兜底）
  -> RTSP 发布到 MediaMTX
  -> MediaMTX 原码转发为 WebRTC/WHEP
  -> Vision Pro
```

MediaMTX 不会再次编码视频；RTSP 是采集程序到 WebRTC 网关的本机传输层。这样采集端不需要自行实现 SDP、ICE、DTLS、SRTP 和信令服务。

## 1. 安装编译依赖

已安装 `librealsense2-dev` 的 Ubuntu 22.04/24.04：

```bash
cd d435i-webrtc-streamer
./scripts/install-deps-ubuntu.sh
```

如果尚未安装 RealSense SDK，先运行工作区根目录中的 `install-realsense-ubuntu.sh`。

## 2. 构建

```bash
./scripts/build.sh
./build/d435i-streamer --list-encoders
```

`auto` 的选择顺序是 NVIDIA NVENC、Intel Quick Sync、VA-API、x264、OpenH264。程序不仅检查插件注册信息，还会尝试创建编码器元素；插件存在但驱动不可用时会自动跳过，并选择低延迟 x264。

## 3. 启动 WebRTC 网关

首次下载 MediaMTX：

```bash
./scripts/download-mediamtx.sh
```

启动网关；脚本默认使用本机第一个局域网 IP，也可以显式指定：

```bash
./scripts/run-mediamtx.sh
# 或
./scripts/run-mediamtx.sh 192.168.1.10
```

需要允许以下入站端口：

- TCP `8554`：采集程序发布 RTSP
- TCP `8889`：WebRTC HTTP/WHEP 信令
- UDP `8189`：WebRTC ICE 媒体

## 4. 启动 D435i 推流

将 D435i 直接连接到 Ubuntu 主机的 USB 3.x 端口，然后在第二个终端运行：

```bash
./build/d435i-streamer
```

常用参数示例：

```bash
./build/d435i-streamer \
  --width 1280 \
  --height 720 \
  --fps 30 \
  --bitrate-kbps 5000 \
  --encoder auto \
  --rtsp-url rtsp://127.0.0.1:8554/d435i
```

如果硬件编码器插件存在但驱动初始化失败，可先强制软件编码排除问题：

```bash
./build/d435i-streamer --encoder x264enc
```

## 5. 验证

同一局域网中的普通浏览器打开：

```text
http://<Ubuntu主机IP>:8889/d435i/
```

Vision Pro 原生客户端后续使用 WHEP 地址：

```text
http://<Ubuntu主机IP>:8889/d435i/whep
```

先通过浏览器验证可以把“相机/编码/网关问题”和“visionOS 客户端问题”分开定位。

## 延迟设置

- `queue` 只保留最多两帧，积压时丢弃旧帧，避免延迟不断增长。
- 输入为 RGB8，明确转换为 NV12 后再送给编码器。
- 编码器关闭 B 帧，GOP 默认约 1 秒；x264 使用 `zerolatency + ultrafast`。
- 建议 Ubuntu 主机和 Vision Pro 使用同一路由器的 Wi-Fi 6/6E 网络；采集主机优先使用有线网。
- 端到端延迟应以屏幕显示毫秒计时器或 LED 闪烁实测，不只看平均帧率。

## 当前范围与后续扩展

当前只开启 `RS2_STREAM_COLOR / RS2_FORMAT_RGB8`。以后加入深度与 IMU 时，应作为独立数据通道发布，并为每个数据包保留 RealSense 时间戳；不要把 16 位深度图直接当普通 H.264 彩色视频压缩。

MediaMTX 官方文档：<https://mediamtx.org/docs/usage/read-a-stream#webrtc>
