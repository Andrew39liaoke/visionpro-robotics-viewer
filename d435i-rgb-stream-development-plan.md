# D435i RGB 视频流到 Apple Vision Pro 开发方案

## 1. 文档范围

本文只描述一个功能：将 Intel RealSense D435i 的 RGB 视频通过网络实时传输并显示在 Apple Vision Pro 中。

本文不涉及 ROS、深度图、点云、IMU、SLAM、三维建模及建模结果展示。

## 2. 开发目标

```text
Intel RealSense D435i
        │ USB 3
        ▼
Linux/Windows 视频发送主机
        │ librealsense 采集 RGB
        ▼
H.264 实时编码
        │
        ▼
WebRTC
        │ 局域网/公网
        ▼
Vision Pro 原生 visionOS App
        │
        ▼
实时 RGB 视频画面
```

第一版以稳定完成 `1280×720@30fps` 实时播放为目标。

## 3. 功能范围

### 3.1 包含内容

- D435i RGB 视频采集。
- RGB 图像格式转换。
- H.264 实时编码。
- WebRTC 视频发布、订阅和传输。
- visionOS 原生客户端。
- Vision Pro 窗口内视频播放。
- 连接状态、错误提示和自动重连。
- 视频帧率、码率、丢帧和延迟监控。
- Vision Pro 真机稳定性测试。
- 可选的 RealityKit 空间视频平面。

### 3.2 不包含内容

- D435i 深度流、红外流和 IMU 数据。
- ROS 或 DDS。
- 点云、网格和空间地图。
- SLAM 或三维重建。
- 音频、多摄像头、录像和回放。
- 摄像头画面与现实空间的坐标标定。

## 4. 关键技术决策

### 4.1 D435i 不直接连接 Vision Pro

D435i 的完整能力依赖 USB 3。Vision Pro 虽然可以通过 Developer Strap 访问部分 UVC 设备，但 Apple 当前文档说明该能力只支持 USB 2 设备、最大 500 mA，并需要 UVC entitlement。

正式方案采用：

```text
D435i → 视频发送主机 → WebRTC → Vision Pro
```

### 4.2 视频使用 WebRTC

- 面向实时音视频传输。
- 支持网络抖动缓冲、丢包处理和动态码率。
- 媒体通道默认加密。
- 支持局域网和公网连接。
- 提供 RTT、jitter、丢包和解码帧率等统计信息。
- Vision Pro 可通过原生 Swift WebRTC SDK 接收。

### 4.3 Vision Pro 使用原生 App

- 统一管理连接、重连和应用生命周期。
- 第一版在普通窗口中播放。
- 后续可将视频放入 RealityKit 空间场景。
- 可以精确控制视频帧、纹理和性能。

### 4.4 第一版先使用 SDK VideoView

第一版不开发自定义 Metal 视频渲染，先使用 WebRTC SDK 提供的视频 View 验证完整链路。稳定后再增加：

```text
RTCVideoFrame
  → CVPixelBuffer
  → Metal Texture
  → RealityKit LowLevelTexture
  → 空间视频平面
```

## 5. 推荐技术栈

### 5.1 视频发送端

- Ubuntu Linux 优先，也可使用 Windows。
- `librealsense` 采集 D435i Color Stream。
- C++ 作为首选开发语言。
- H.264 低延迟编码。
- 根据硬件使用 Intel Quick Sync、NVIDIA NVENC 或其他硬件编码器。
- WebRTC 可选 LiveKit、GStreamer `webrtcbin` 或等价实现。

推荐第一版使用现成 WebRTC 服务管理信令、鉴权和连接，不自行实现完整信令协议。

### 5.2 Vision Pro 客户端

- Swift、SwiftUI、原生 visionOS target。
- 支持 visionOS 的 WebRTC Swift SDK。
- 首版使用 SDK 提供的视频 View。
- 后续空间渲染使用 Metal 和 RealityKit。
- OSLog 记录日志。
- Swift Concurrency 管理异步连接。

第三方 SDK 需要在真机完成兼容性验证，并锁定依赖版本。

## 6. 发送端设计

### 6.1 D435i RGB 采集模块

职责：

1. 初始化 `librealsense` context 和 pipeline。
2. 查找并校验 D435i 设备。
3. 只启用 Color Stream。
4. 设置分辨率、帧率和像素格式。
5. 输出带采集时间戳的 RGB 帧。
6. 处理摄像头断开和重新连接。

初始采集参数：

```text
Stream: Color
Resolution: 1280×720
FPS: 30
Format: RGB8 或 BGR8
```

建议内部帧结构：

```cpp
struct CapturedVideoFrame {
    uint64_t sequence;
    int64_t captureTimestampNs;
    int width;
    int height;
    int stride;
    PixelFormat format;
    FrameBuffer buffer;
};
```

### 6.2 帧格式转换模块

- 将 `RGB8/BGR8` 转换成编码器需要的 `NV12/I420`。
- 保留帧序号和采集时间戳。
- 避免重复内存复制。
- 队列最多保留 1～2 帧。
- 处理不过来时丢弃旧帧，优先发送最新帧。
- 能使用 GPU 或硬件颜色转换时优先使用硬件路径。

### 6.3 H.264 编码模块

- 使用低延迟编码模式。
- 禁用 B 帧。
- 关键帧间隔设置为 1～2 秒。
- 禁用 lookahead 或设置为最小值。
- 避免额外编码缓存。
- 网络恶化时允许降码率或降帧率。

### 6.4 WebRTC 发布模块

- 连接 WebRTC 服务。
- 使用短期 Token 鉴权。
- 发布一条 RGB 视频轨道。
- 维护连接状态并自动重连。
- 上报编码、发送和网络指标。

推荐结构：

```text
RealSenseCapture
  → PixelConverter
  → LatestFrameQueue
  → H264/WebRTCPublisher
  → MetricsReporter
```

## 7. Vision Pro 客户端设计

客户端职责：

1. 获取短期访问 Token。
2. 连接 WebRTC 服务。
3. 加入指定房间或会话。
4. 查找指定摄像头发布者。
5. 自动订阅远端视频轨道。
6. 在 visionOS 窗口中显示视频。
7. 展示连接、重连和错误状态。
8. 正确处理前后台和资源释放。

状态机：

```text
idle
  → connecting
  → connected
  → subscribing
  → playing
  → reconnecting
  → failed
```

建议目录：

```text
VisionProVideoApp/
├── App/
│   ├── VisionProVideoApp.swift
│   └── AppConfiguration.swift
├── Video/
│   ├── VideoSession.swift
│   ├── VideoConnectionState.swift
│   ├── RemoteVideoView.swift
│   └── VideoMetrics.swift
├── UI/
│   ├── VideoScreen.swift
│   ├── ConnectionStatusView.swift
│   └── SettingsView.swift
└── Infrastructure/
    ├── Logger.swift
    └── SecureTokenProvider.swift
```

## 8. 参数基线

| 参数 | 第一版设置 | 说明 |
|---|---:|---|
| 分辨率 | 1280×720 | 优先稳定和低延迟 |
| 帧率 | 30 fps | 第一版基线 |
| 编码 | H.264 | Apple 平台兼容性好 |
| 编码输入 | NV12/I420 | 适合实时编码 |
| 目标码率 | 2.5～6 Mbps | 根据场景和网络调整 |
| B 帧 | 关闭 | 避免重排序延迟 |
| 关键帧间隔 | 1～2 秒 | 加快断流恢复 |
| 音频 | 关闭 | 本功能不需要 |
| 缓冲队列 | 1～2 帧 | 防止延迟累积 |

## 9. 配置示例

### 9.1 发送端

```yaml
camera:
  serial_number: ""
  width: 1280
  height: 720
  fps: 30
  pixel_format: rgb8

encoder:
  codec: h264
  bitrate_bps: 4000000
  keyframe_interval_seconds: 1
  b_frames: 0
  low_latency: true

webrtc:
  server_url: wss://example.internal
  room: d435i-rgb
  participant: d435i-camera-01
  token_endpoint: https://example.internal/api/token

buffer:
  max_frames: 2
  drop_old_frames: true
```

### 9.2 Vision Pro

```text
WEBRTC_SERVER_URL
ROOM_ID
CAMERA_PARTICIPANT_ID
TOKEN_ENDPOINT
LOG_LEVEL
```

禁止将 API Secret、管理员 Token 或长期凭据硬编码到 App 中。

## 10. 开发阶段

### 阶段 A：D435i RGB 采集验证

- 安装 `librealsense`。
- 使用 RealSense Viewer 验证 D435i。
- 编写 RGB 采集程序并只启用 Color Stream。
- 验证分辨率、帧率、颜色和时间戳。
- 测试摄像头拔插后的错误处理。

完成标准：稳定采集 1280×720@30fps，连续运行 30 分钟无断流或持续内存增长。

### 阶段 B：WebRTC 发布端

- 部署或连接 WebRTC 服务。
- 实现 RGB 帧格式转换和 H.264 低延迟编码。
- 发布单路视频轨道。
- 使用桌面客户端验证播放。
- 增加发布端日志和统计信息。

完成标准：桌面客户端稳定播放，网络变慢时不持续堆积旧帧。

### 阶段 C：Vision Pro 原生客户端

- 创建 visionOS 原生项目。
- 通过 Swift Package Manager 集成 WebRTC SDK。
- 实现 Token 获取、连接、订阅和重连。
- 使用视频 View 在 SwiftUI 窗口中显示。
- 处理 App 前后台状态。
- 在 Vision Pro 真机测试。

完成标准：Vision Pro 稳定显示 D435i RGB 实时画面，短时断网后可以恢复。

### 阶段 D：性能优化

- 测量 glass-to-glass 延迟。
- 采集 WebRTC stats。
- 对比 720p30、720p60 和 1080p30。
- 调整码率、关键帧间隔和缓冲策略。
- 验证硬件编码路径。
- 完成 30～60 分钟稳定性测试。

### 阶段 E：可选空间视频平面

- 自定义 WebRTC 视频帧接收器。
- 提取 `CVPixelBuffer`。
- 使用 Core Image 或 Metal 生成纹理。
- 使用 RealityKit `LowLevelTexture` 显示。
- 在 `ImmersiveSpace` 中放置可移动、缩放的视频平面。

## 11. 验收标准

### 11.1 功能验收

- Vision Pro 能连接指定视频会话并自动订阅视频轨道。
- 视频颜色、方向和宽高比正确。
- 摄像头停止时客户端显示断流状态。
- 摄像头和服务恢复后自动重连。
- App 前后台切换不会崩溃。
- App 退出后正确释放网络和视频资源。

### 11.2 性能验收

- 默认达到 1280×720@30fps。
- 正常局域网下，中位 glass-to-glass 延迟目标低于 250 ms。
- 正常局域网下，P95 延迟目标低于 400 ms。
- 连续运行 30 分钟无崩溃或明显持续内存增长。
- 网络恶化时不产生数秒级累计延迟。

### 11.3 可观测性

至少记录摄像头采集帧率、编码帧率、编码耗时、发送/接收码率、RTT、jitter、丢包、解码帧数、丢帧数、当前分辨率以及重连次数。

## 12. 测试方案

### 12.1 启停测试

- 不同顺序启动摄像头、发布端、服务端和 Vision Pro。
- 拔出并重新连接 D435i。
- 重启视频发布端和 WebRTC 服务。
- Vision Pro App 进入后台后重新进入。

### 12.2 网络测试

- 良好和弱信号 Wi-Fi。
- 1%、3%、5% 模拟丢包。
- 增加网络延迟和抖动。
- 网络断开 5～30 秒后恢复。

### 12.3 视频测试

- 静态、快速运动、低照度和高细节场景。
- 对比 720p30、720p60、1080p30。

### 12.4 Glass-to-glass 延迟

1. 在外部显示器运行毫秒计时器。
2. D435i 拍摄计时器。
3. 外部相机同时拍摄源计时器和 Vision Pro 最终显示结果。
4. 至少记录 100 个样本，统计中位值和 P95。

不能只使用网络 RTT 代替端到端视频延迟。

## 13. 风险与处理

| 风险 | 表现 | 处理方式 |
|---|---|---|
| 像素格式不匹配 | 颜色错误或无法编码 | 明确 RGB/BGR/NV12/I420 转换路径 |
| 颜色转换开销高 | CPU 占用和延迟升高 | 使用 SIMD、GPU 或硬件转换 |
| 队列积压 | 延迟逐渐增大 | 限制 1～2 帧并丢弃旧帧 |
| H.264 协商失败 | 已连接但无画面 | 固定 codec preference 并记录协商日志 |
| SDK 不支持目标 visionOS | 编译或真机失败 | 尽早完成真机 smoke test 并锁定版本 |
| Wi-Fi 不稳定 | 卡顿或降码率 | 使用 WebRTC 统计和自适应码率 |
| 凭据泄漏 | 未授权视频访问 | 使用服务端签发的短期 Token |

## 14. 交付物

- D435i RGB 采集模块。
- RGB 帧格式转换模块。
- H.264/WebRTC 视频发布程序。
- WebRTC 服务部署配置。
- visionOS 原生客户端工程。
- Vision Pro 实时 RGB 视频窗口。
- 自动连接和重连功能。
- 视频和网络指标日志。
- 真机测试记录和延迟测试报告。
- 部署、配置和启动说明。

## 15. 开发前确认项

- 视频发送主机的操作系统和硬件配置。
- 主机是否具备硬件 H.264 编码能力。
- Vision Pro 的 visionOS 版本和 Xcode 版本。
- 是否仅在局域网使用。
- 是否允许部署自托管 WebRTC 服务。
- 是否已有 LiveKit、GStreamer 或其他 WebRTC 基础设施。
- 第一版只需要窗口播放，还是必须包含 `ImmersiveSpace`。

## 16. 参考资料

- [Intel RealSense D435i 规格](https://www.intel.com/content/www/us/en/products/sku/190004/intel-realsense-depth-camera-d435i/specifications.html)
- [RealSense SDK](https://github.com/realsenseai/librealsense)
- [Apple：visionOS UVC Device Access](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.avfoundation.uvc-device-access)
- [Apple：Displaying low-latency connected video](https://developer.apple.com/documentation/realitykit/displaying-low-latency-connected-video)
- [W3C WebRTC Recommendation](https://www.w3.org/TR/webrtc/)
- [LiveKit Swift SDK](https://github.com/livekit/client-sdk-swift)

