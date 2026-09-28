# Vision Pro D435i WebRTC 视频客户端开发方案

> 状态：客户端代码已实现，真机推流与性能验收待完成  
> 调研日期：2026-09-24  
> 适用工程：工作区根目录下的 `Visonpro.xcodeproj`  
> 上游链路：`d435i-webrtc-streamer` → RTSP → MediaMTX → WebRTC/WHEP

### 实施说明（2026-09-24）

运行方法见 [visionOS App 使用说明](visionos-app-README.md)。依赖锁定为 LiveKitWebRTC `150.7871.02`（SwiftPM 规范化为 `150.7871.2`）。检查实际二进制后发现 visionOS slice **不导出 `RTCMTLVideoView`**，因此本次实现采用 `LKRTCVideoRenderer` 接收帧、单帧邮箱缓存、`MTKView + Core Image` GPU 显示；支持 `CVPixelBuffer` 直接路径和 I420 转 NV12 兜底。下文关于“SDK Metal video view”的原始设想以本说明为准。

协议解析、HTTP、指标计算和退避策略位于本地 Swift Package `Packages/StreamingCore`；App 会话控制集中在 MainActor，SDK 回调先转为事件，帧邮箱通过锁保护。此结构取代下文建议的独立 WHEP actor。尚未完成的设备性能指标不视为已验收。

## 1. 结论

在不改动现有 D435i 推流端和 MediaMTX 部署的前提下，开发一个原生 visionOS 窗口应用，通过 WHEP 接收 `http://<Ubuntu-IP>:8889/d435i/whep`，解码 H.264 并显示实时画面。

正式实现推荐使用：

- SwiftUI 构建 visionOS 窗口、状态栏、设置和错误界面。
- `LiveKitWebRTC` XCFramework 只作为底层 WebRTC 实现，不引入 LiveKit 房间或 LiveKit Server。
- App 自行实现轻量 WHEP 信令，包括 `OPTIONS`、SDP `POST`、Trickle ICE `PATCH` 和会话释放。
- 第一版通过 SDK 的 Metal 视频 View 显示远端轨道，不做自定义 Metal/RealityKit 纹理管线。
- 内嵌 MediaMTX WebRTC 页面作为开发期诊断兜底，不作为正式播放架构。

选择该方案的原因：

1. 当前服务已经稳定提供 WHEP，没有迁移媒体服务器的必要。
2. MediaMTX 直接转发已有 H.264 编码，不在 Vision Pro 客户端前增加转码环节。
3. `livekit/webrtc-xcframework` 当前明确提供 visionOS 设备和模拟器二进制，最低 visionOS 2.2；现有工程目标为 visionOS 26.2，满足要求。
4. 原生视频轨道后续可以接入 Metal 或 RealityKit；`WKWebView` 难以向原生空间渲染管线提供逐帧数据。

## 2. 当前项目基线

### 2.1 已验证链路

仓库现有实现已经完成：

```text
D435i RGB8 1280×720@30fps
  → GStreamer videoconvert/NV12
  → H.264 Baseline，无 B 帧
  → RTSP/TCP localhost:8554/d435i
  → MediaMTX
  → WebRTC/WHEP
  → 局域网浏览器
```

现有端点：

```text
浏览器诊断页：http://<Ubuntu-IP>:8889/d435i/
WHEP 端点：   http://<Ubuntu-IP>:8889/d435i/whep
ICE 媒体端口：udp://<Ubuntu-IP>:8189
```

### 2.2 visionOS 工程现状

- `Visonpro.xcodeproj` 是原生 visionOS 工程。
- `XROS_DEPLOYMENT_TARGET = 26.2`。
- 当前仅有默认的 `ContentView`、`Model3D` 和 “Hello, world!”。
- 当前没有 WebRTC 依赖、网络权限说明、连接管理或视频渲染代码。
- 工程启用了 Main Actor 默认隔离，新增的 WebRTC delegate 和网络回调必须明确处理线程边界。

## 3. 范围

### 3.1 第一版包含

- 输入和保存 WHEP URL。
- 连接 MediaMTX WHEP 端点。
- 接收并显示单路 D435i RGB 视频。
- 保持 16:9 宽高比，支持适应窗口和裁剪填充两种模式。
- 显示连接状态、首帧状态和可理解的错误信息。
- 手动连接、断开、重试。
- ICE/WebRTC 失败后的自动重连。
- App 前后台切换时正确释放和恢复会话。
- 展示基础统计：分辨率、接收帧率、接收码率、丢包、jitter、RTT。
- 通过 Vision Pro 真机完成稳定性和 glass-to-glass 延迟测试。

### 3.2 第一版不包含

- 深度流、红外流、IMU、点云或 ROS 数据。
- 音频发送或接收。
- Vision Pro 摄像头、麦克风权限。
- 多路相机切换。
- 录像、截图和回放。
- 沉浸空间或 RealityKit 视频平面。
- 公网 TURN 部署。
- 在 App 中配置或启动 Ubuntu 推流进程。

## 4. 技术方案比较

| 方案 | 优点 | 局限 | 用途 |
|---|---|---|---|
| 原生 WebRTC + 自研 WHEP | 原生状态管理、低延迟、可获取统计和视频帧、便于后续 RealityKit | 需要维护 WHEP 和第三方二进制依赖 | **正式方案** |
| `WKWebView` 加载 MediaMTX 页面 | 无 WebRTC SDK，最快验证；MediaMTX 已提供完整网页客户端 | 原生可控性较弱，逐帧接入 RealityKit 困难 | 联调与故障隔离兜底 |
| HLS + `AVPlayer` | Apple 原生 API，接入简单 | 延迟通常明显高于 WebRTC | WebRTC 不可用时的非实时降级 |
| 将服务端迁移到 LiveKit | Swift SDK 功能完整，房间、鉴权和重连成熟 | 需要重构当前 MediaMTX 链路，超出本阶段目标 | 暂不采用 |

Google/社区常见的 iOS `WebRTC.xcframework` 不能仅凭 iOS arm64 slice 推断支持 visionOS；必须确认 XCFramework 中存在 `xros-arm64` 和 `xros-arm64-simulator` slice。依赖集成后，第一项工作就是在模拟器和真机执行最小编译/启动测试。

## 5. 总体架构

```text
┌──────────────── Ubuntu 主机 ────────────────┐
│ D435i → d435i-streamer → RTSP → MediaMTX   │
│                                      │      │
│       HTTP :8889（WHEP 信令）         │      │
│       UDP  :8189（ICE/DTLS/SRTP）     │      │
└──────────────────────────────────────┼──────┘
                                       │ LAN
┌──────────────── Vision Pro ──────────┼──────┐
│ URLSession ── OPTIONS/POST/PATCH ─────┘      │
│                    │                         │
│              WHEPClient actor                │
│                    │                         │
│        LKRTCPeerConnection / SRTP            │
│                    │                         │
│             remote video track               │
│                    │                         │
│       Metal video renderer in SwiftUI        │
│                    │                         │
│  StreamViewModel → status / metrics / retry  │
└──────────────────────────────────────────────┘
```

MediaMTX 的 HTTP 端口只负责信令。视频不经过 `:8889` 持续下载，而是在 ICE 协商后通过 `:8189/UDP` 的 DTLS/SRTP 连接传输。因此出现“POST 成功但一直黑屏”时，应优先检查 UDP 8189、防火墙和 SDP candidate 地址。

## 6. 依赖与版本策略

### 6.1 WebRTC 依赖

Swift Package：

```text
https://github.com/livekit/webrtc-xcframework
```

集成要求：

- 使用明确的 release tag，不跟踪 `main` 分支。
- 将解析出的精确版本提交到 `Package.resolved`。
- 在升级依赖前验证 visionOS device/simulator slices、H.264 解码、Metal renderer 和统计字段。
- App 代码通过项目内的 `WebRTCEngine` 协议隔离第三方 API，避免 UI 层直接依赖大量 `LKRTC*` 类型。
- 记录第三方许可证并随最终产品分发。

### 6.2 为什么不直接依赖 LiveKit Swift 房间 SDK

当前服务器使用的是 WHEP，不是 LiveKit 的房间信令协议。引入 LiveKit 房间 SDK 不能直接连接 MediaMTX。这里使用的是 LiveKit 发布的 WebRTC 二进制及其低层 PeerConnection API，信令仍由项目自己的 `WHEPClient` 完成。

### 6.3 WebKit 诊断实现

开发期增加一个编译开关或隐藏诊断入口，用 `WKWebView` 加载：

```text
http://<Ubuntu-IP>:8889/d435i/?controls=false&muted=true&autoplay=true&playsInline=true
```

如果 WebKit 页面可播放而原生视图不可播放，问题位于原生 SDK、WHEP 实现或渲染层；如果两者都不可播放，优先检查推流、MediaMTX 和网络。

## 7. WHEP 会话设计

### 7.1 建连流程

```text
App                   MediaMTX                 ICE/WebRTC
 │                       │                         │
 │ OPTIONS /d435i/whep   │                         │
 │──────────────────────>│                         │
 │ Link: ICE servers     │                         │
 │<──────────────────────│                         │
 │ create PeerConnection + recvonly video          │
 │ createOffer / setLocalDescription               │
 │ POST application/sdp  │                         │
 │──────────────────────>│                         │
 │ 201 + Location + SDP answer                     │
 │<──────────────────────│                         │
 │ setRemoteDescription                            │
 │ PATCH local ICE candidates to Location          │
 │──────────────────────>│                         │
 │              ICE checks / DTLS / SRTP           │
 │<───────────────────────────────────────────────>│
 │ onTrack → attach renderer                       │
```

### 7.2 请求规则

1. 对 WHEP URL 发送 `OPTIONS`。
   - 解析所有 `Link: <...>; rel="ice-server"`。
   - 支持无 `Link` 的局域网场景，此时使用空 ICE server 列表。
   - Basic 或 Bearer 鉴权头需要同时用于 `OPTIONS` 和 `POST`。
2. 创建 Unified Plan PeerConnection。
   - 添加一个 `recvonly` video transceiver。
   - 第一版不添加音频 transceiver。
   - 优先协商 H.264，并记录最终 SDP 中的 codec/profile。
3. 创建 offer 并设置 local description。
4. 将 offer SDP 发送到 WHEP URL：

```http
POST /d435i/whep HTTP/1.1
Content-Type: application/sdp
Authorization: Bearer <optional-token>

<offer-sdp>
```

5. 只接受成功状态 `201 Created`。
   - body 是 answer SDP。
   - `Location` 是后续会话资源 URL，可能为相对地址，必须相对于 WHEP URL 解析。
6. 设置 remote description。
7. 对本地 ICE candidate 进行 Trickle ICE：

```http
PATCH <session-location> HTTP/1.1
Content-Type: application/trickle-ice-sdpfrag
If-Match: *

<sdp-fragment>
```

   - 在收到 `Location` 前产生的 candidate 先入队。
   - 收到 `Location` 后按顺序发送，期望 `204 No Content`。
   - candidate 队列必须有上限，且在断开时清空。
8. 收到远端 video track 后，绑定 SDK Metal renderer。
9. 主动断开时关闭 PeerConnection，并对会话 URL 最佳努力发送 `DELETE`；旧版或特定 MediaMTX 版本返回 404 不应导致 App 崩溃或继续重连。

### 7.3 与 MediaMTX 官方客户端保持一致

原生实现应以当前 MediaMTX `reader.js` 为互操作基准，特别是：

- 先用 `OPTIONS` 获取 ICE servers。
- SDP offer 使用 `POST application/sdp`。
- 从 201 响应读取 `Location` 和 answer SDP。
- candidate 使用 `PATCH application/trickle-ice-sdpfrag`，带 `If-Match: *`。
- 不要自己发明 WebSocket 信令。

### 7.4 HTTP 和协议错误映射

| 条件 | 用户提示 | 是否自动重试 |
|---|---|---|
| URL 格式错误或不是 HTTP(S) | 地址无效 | 否 |
| 401/403 | 鉴权失败 | 否，修改凭据后重试 |
| 404 | 视频流尚未发布 | 是 |
| 400/415 | WHEP/SDP 不兼容 | 有限次数，保留 SDP 日志 |
| HTTP 超时/主机不可达 | 无法连接视频主机 | 是 |
| ICE failed | 媒体网络连接失败，检查 UDP 8189 | 是 |
| 已连接但 5 秒无首帧 | 已连接但未收到视频 | 是 |
| 解码错误 | H.264 解码失败 | 有限次数 |

日志中可以记录 SDP 类型、m-line、codec 和 candidate 类型，但不得记录 Bearer Token、Basic 密码或完整鉴权头。

## 8. 客户端模块设计

建议目录：

```text
Visonpro/
├── App/
│   ├── VisonproApp.swift
│   └── AppConfiguration.swift
├── Streaming/
│   ├── StreamSession.swift
│   ├── StreamState.swift
│   ├── WHEPClient.swift
│   ├── WHEPHTTPClient.swift
│   ├── ICELinkParser.swift
│   ├── SDPFragmentBuilder.swift
│   ├── WebRTCEngine.swift
│   └── StreamMetrics.swift
├── Views/
│   ├── ContentView.swift
│   ├── RemoteVideoView.swift
│   ├── ConnectionOverlay.swift
│   ├── MetricsOverlay.swift
│   └── SettingsView.swift
└── Support/
    ├── AppLogger.swift
    └── WebDiagnosticView.swift
```

### 8.1 `StreamSession`

- 标记为 `@MainActor` 和 `@Observable`。
- 是 UI 唯一观察的数据源。
- 维护状态、错误、远端轨道、统计、当前 URL 和重连任务。
- 负责响应 `scenePhase`，但不直接实现 SDP 或 HTTP 细节。
- 每次 `connect()` 分配 session generation ID；旧会话的迟到回调必须被忽略。

### 8.2 `WHEPClient`

- 使用 actor 或专用串行队列管理会话可变状态。
- 持有 PeerConnection、session URL、待发送 candidates 和 URLSession tasks。
- `connect(endpoint:credential:)` 是一次性异步操作。
- `disconnect(reason:)` 必须幂等，可在任何中间状态调用。
- WebRTC delegate 回调先进入串行上下文，再将 UI 事件发送给 `StreamSession`。

### 8.3 `WHEPHTTPClient`

- 设置连接和资源超时。
- 严格校验 HTTP 状态码、MIME type、非空 answer SDP 和 `Location`。
- 支持 Basic 与 Bearer 两种可选鉴权，凭据存储到 Keychain；第一阶段无鉴权时不创建凭据。
- 在单元测试中通过自定义 `URLProtocol` 模拟响应。

### 8.4 `RemoteVideoView`

- 用 `UIViewRepresentable` 包装 `LKRTCMTLVideoView` 或当前锁定 SDK 提供的等价 Metal renderer。
- `makeUIView` 只创建 renderer，`updateUIView` 只处理轨道变更和显示模式。
- 旧轨道必须先 `remove(renderer)`，新轨道再 `add(renderer)`。
- 在 `dismantleUIView` 中解除 renderer，避免轨道或 View 循环持有。
- 默认 `aspectFit`；设置页允许切换 `aspectFill`。

### 8.5 `StreamMetrics`

每秒调用一次 `getStats`，提取：

- `framesDecoded`、`framesDropped`、`framesPerSecond`。
- `frameWidth`、`frameHeight`。
- `bytesReceived`，通过相邻采样差计算接收码率。
- `packetsLost`、`jitter`。
- 当前 selected candidate pair 的 RTT、local/remote candidate type 和协议。
- `keyFramesDecoded` 和 decoder implementation（SDK 提供时）。

统计任务在断开或 App 非 active 时取消。UI 默认只显示状态和帧率，详细数据放入可折叠诊断面板。

## 9. 状态机与重连

```text
idle
  └─ connect ─> requestingICEServers
                  └─> negotiating
                        └─> connectingMedia
                              └─ first frame ─> playing

任意活动状态 ─ user disconnect/background ─> disconnecting ─> idle
任意活动状态 ─ recoverable error ─> waitingToRetry ─> requestingICEServers
任意活动状态 ─ fatal/config error ─> failed
```

要求：

- `connect()`、`disconnect()` 和自动重连不能并发创建多个 PeerConnection。
- 自动重连退避：1、2、4、8、10 秒，之后保持 10 秒，并加入 ±20% jitter。
- 用户主动断开、URL 无效、401 和 403 不自动重连。
- 404、网络中断、ICE failed、PeerConnection closed 自动重连。
- 连接成功并持续播放 30 秒后重置退避计数。
- `scenePhase != .active` 时断开；恢复 active 后仅在用户未手动关闭时重连。
- 用 `Task` 取消机制中止旧的延时重连，不使用不可取消的 `DispatchQueue.asyncAfter`。

## 10. UI 设计

第一版使用普通 `WindowGroup`，默认窗口约 960×620 points：

```text
┌─────────────────────────────────────────────────────┐
│ D435i RGB     ● 已连接    1280×720  29.8 fps   ⚙︎  │
├─────────────────────────────────────────────────────┤
│                                                     │
│                  16:9 实时视频                      │
│                                                     │
│            未播放时显示状态与重试按钮               │
├─────────────────────────────────────────────────────┤
│ 4.8 Mbps  RTT 8 ms  丢包 0.1%          断开/重连   │
└─────────────────────────────────────────────────────┘
```

交互要求：

- 首次打开显示地址设置；默认值可在 Debug 配置中预填，但不可硬编码实际生产凭据。
- 连接中显示进度，但视频区域保持稳定尺寸，避免窗口跳动。
- 黑屏和“视频画面本身是黑色”要能区分：未收到首帧时必须显示状态浮层。
- 错误提示包含下一步动作，例如“检查 Vision Pro 的本地网络权限”和“检查 Ubuntu UDP 8189”。
- 凝视和捏合目标遵守 visionOS 标准控件尺寸；不要在首版自定义复杂手势。

## 11. visionOS 配置与权限

### 11.1 本地网络权限

`Info.plist` 增加：

```xml
<key>NSLocalNetworkUsageDescription</key>
<string>用于连接局域网中的机器人摄像头并显示实时视频。</string>
```

App 首次连接局域网地址时系统会请求本地网络权限。拒绝后应提示用户到 Settings → Privacy & Security → Local Network 重新开启。

当前使用固定 IP 直连，不需要 Bonjour，因此第一版不添加 `NSBonjourServices`。以后增加自动发现时，再声明项目实际使用的服务类型。

### 11.2 HTTP 与 ATS

开发环境的 MediaMTX 是局域网 HTTP。优先使用范围最小的本地网络声明：

```xml
<key>NSAppTransportSecurity</key>
<dict>
    <key>NSAllowsLocalNetworking</key>
    <true/>
</dict>
```

不得为了方便默认启用全局 `NSAllowsArbitraryLoads`。如果真机 SDK 行为仍拦截裸 IP HTTP，再针对 Debug 配置验证最小可行例外，并把 Release 改为可信 HTTPS/WHEPS。

### 11.3 不需要的权限

客户端只接收远端视频，不采集 Vision Pro 的摄像头和麦克风，因此不要添加相机或麦克风 Usage Description，也不要请求相关权限。

## 12. 安全设计

局域网开发版可暂时使用 HTTP，但 DTLS/SRTP 媒体本身仍由 WebRTC 加密。HTTP 信令未受 TLS 保护，生产部署需完成：

- 为 MediaMTX 配置可信证书和 HTTPS/WHEPS。
- 使用短期 Bearer Token 或受控的只读账号。
- Token 存储在 Keychain，不进入 `UserDefaults`、日志或崩溃报告。
- 限制 MediaMTX path 的 read 权限，避免匿名查看机器人画面。
- 不在仓库中提交实际 IP、用户名、密码和 Token。
- 公网访问时部署 TURN，并使用临时凭据；不要把长期 TURN secret 放进 App。

## 13. 实施阶段

### 阶段 0：依赖与网络冒烟测试

任务：

- 添加并锁定 `LiveKitWebRTC`。
- 验证 visionOS simulator 和 Vision Pro device 均可链接、启动。
- 添加本地网络说明和最小 ATS 配置。
- 在真机内用 `WKWebView` 打开 MediaMTX 页面并验证画面。

完成标准：真机 App 能通过 WebKit 播放当前 D435i 流；WebRTC 包在两个目标上编译成功。

### 阶段 1：原生 WHEP 最小闭环

任务：

- 实现 `OPTIONS` ICE server 解析。
- 创建 recvonly PeerConnection。
- 实现 SDP POST、answer 设置和 Trickle ICE PATCH。
- 收到远端 H.264 轨道并使用 Metal View 显示。
- 实现手动连接和断开。

完成标准：Vision Pro 真机连续显示 1280×720@30fps 五分钟，颜色、方向和比例正确。

### 阶段 2：产品化连接管理

任务：

- 完成状态机、超时、错误映射和自动重连。
- 处理 scene phase 和旧回调竞态。
- 增加 URL 设置、Keychain 凭据和诊断日志。
- 增加基础 stats 面板。

完成标准：推流端重启、MediaMTX 重启和 Wi-Fi 短时断开后，App 均能自动恢复，且不会出现重复播放会话。

### 阶段 3：性能与稳定性

任务：

- 测量 glass-to-glass 中位数与 P95 延迟。
- 执行 60 分钟真机稳定性测试。
- 记录 CPU、内存、温度、接收帧率、丢帧和网络统计。
- 调整发布端码率、GOP 和 App 渲染模式。
- 测试 UDP 被阻断后的 TCP/TURN 方案，但局域网默认仍使用 UDP。

完成标准：达到第 15 节验收指标，且无持续内存增长和热失控。

### 阶段 4：可选 RealityKit 空间视频平面

只有窗口播放稳定后再实施：

```text
remote RTCVideoFrame
  → CVPixelBuffer/I420 转换
  → Metal texture
  → RealityKit LowLevelTexture / material
  → Volume 或 ImmersiveSpace 中的视频平面
```

此阶段需要单独验证像素格式、色彩空间、帧同步和纹理生命周期，不与第一版耦合。

## 14. 测试计划

### 14.1 单元测试

- 相对和绝对 `Location` URL 解析。
- 单个/多个 ICE `Link` header 解析以及带引号凭据转义。
- SDP fragment 生成，包括多 m-line、mid、ufrag 和 pwd。
- 200、201、204、400、401、403、404、415、500 和超时映射。
- 状态机非法跳转、重复 connect、重复 disconnect。
- 自动重连退避、取消和 generation ID 丢弃旧事件。
- 相邻 WebRTC stats 样本的码率与丢包计算。

### 14.2 集成测试

- 推流先启动、App 后启动。
- App 先启动，推流延迟 30 秒启动。
- 停止并重启 `d435i-streamer`。
- 停止并重启 MediaMTX。
- Vision Pro Wi-Fi 关闭 5、15、30 秒后恢复。
- App active/inactive/background 往返。
- 本地网络权限允许和拒绝。
- 错误 IP、错误端口、错误 path、错误 Token。
- UDP 8189 被防火墙拦截。

### 14.3 视频与性能测试

- 静态场景、快速运动、高细节、弱光。
- 720p30 基线；之后对比 720p60、1080p30。
- 检查画面拉伸、裁剪、旋转、颜色偏差和解码花屏。
- 每次测试记录 SDK 版本、MediaMTX 版本、Vision Pro/visionOS 版本和网络拓扑。

### 14.4 延迟测试

1. 显示器运行毫秒计时器。
2. D435i 拍摄该计时器。
3. 外部相机同一画面拍到源计时器和 Vision Pro 内最终显示；必要时使用设备画面录制或镜像辅助取样。
4. 采集至少 100 个样本，计算中位数、P95 和最大值。
5. 同时保存 WebRTC RTT/jitter，区分网络和编解码/渲染延迟。

WebRTC RTT 不能代替 glass-to-glass 延迟。

## 15. 验收标准

### 15.1 功能

- Vision Pro 真机能连接配置的 WHEP URL 并显示 D435i RGB 画面。
- 首帧前、播放中、重连中和失败状态清晰可见。
- 颜色、方向和 16:9 比例正确。
- 用户可断开、重连和修改地址。
- 推流或网络恢复后自动恢复播放。
- App 进入非活动状态后无残留 PeerConnection；恢复后按预期重连。
- 无需 Vision Pro 摄像头或麦克风权限。

### 15.2 性能目标

- 基线：1280×720、30 fps。
- 正常局域网接收帧率：P95 采样不低于 27 fps。
- 正常局域网 glass-to-glass 延迟：中位数低于 250 ms，P95 低于 400 ms。
- 连续运行 60 分钟无崩溃、无数秒级延迟累积、无明显持续内存增长。
- 5～30 秒网络中断后，在网络恢复 15 秒内重新开始播放。

性能目标需要在真机实测后确认；模拟器只用于 UI、状态机和协议开发，不作为解码性能或延迟验收环境。

## 16. 风险与对策

| 风险 | 表现 | 对策 |
|---|---|---|
| WebRTC 二进制与 Xcode/visionOS 不兼容 | 链接失败或真机启动崩溃 | 阶段 0 真机冒烟；锁定版本；保留 WebKit 兜底 |
| WHEP 不是普通视频 URL | 直接交给 `AVPlayer` 无法播放 | 使用 PeerConnection 和 SDP/WHEP 信令 |
| 信令成功但 ICE 失败 | 201 后一直无画面 | 检查 `webrtcAdditionalHosts`、UDP 8189 和 candidate 地址 |
| H.264 协商失败 | PeerConnection 已连接但无视频 | 保持 Baseline、无 B 帧；记录双方 SDP 和 decoder stats |
| HTTP/ATS 或本地网络权限阻断 | URLSession 报错、服务端无请求 | 增加用途说明和最小 ATS 配置，真机检查设置 |
| 异步旧回调污染新会话 | 重连后状态跳回或重复 renderer | session generation ID、幂等断开、串行状态所有权 |
| 重连产生资源泄漏 | 内存/连接数不断增加 | 取消 URLSession task、移除 renderer、关闭 PC、取消 stats task |
| Wi-Fi 抖动导致累计延迟 | 画面越来越落后 | 发送端只保留 1～2 帧；监控 dropped frames；优先最新帧 |
| 裸 HTTP 暴露信令 | Token 或 SDP 被旁路观察 | 开发环境限定可信 LAN，发布前迁移 HTTPS/WHEPS |

## 17. 交付物

- 可编译运行的 visionOS App。
- 原生 WHEP 客户端和 WebRTC 视频渲染。
- URL/鉴权设置、状态机、自动重连和错误界面。
- WebRTC stats 诊断面板和结构化日志。
- WHEP 协议、HTTP 解析和状态机单元测试。
- Vision Pro 真机功能、稳定性和延迟测试记录。
- 更新后的启动与部署说明。

## 18. 开发时的默认决策

如无额外产品要求，实施时按以下默认值进行：

| 项目 | 默认值 |
|---|---|
| 展示形态 | 普通 visionOS 窗口 |
| WHEP URL | 用户可编辑；Debug 可预填 `http://<LAN-IP>:8889/d435i/whep` |
| 视频 | H.264 Baseline，1280×720@30fps |
| 音频 | 不协商 |
| 渲染 | SDK Metal video view，aspect fit |
| 自动连接 | 启动时连接上次成功地址 |
| 自动重连 | 开启，指数退避上限 10 秒 |
| 鉴权 | 第一阶段无；接口保留 Basic/Bearer |
| 传输 | 局域网 UDP 8189 优先 |
| 最低系统 | 保持工程当前 visionOS 26.2 |

## 19. 参考资料

- [MediaMTX：通过 WebRTC/WHEP 读取视频](https://mediamtx.org/docs/read/webrtc)
- [MediaMTX：WebRTC codec、ICE、TCP 与 TURN 配置](https://mediamtx.org/docs/features/webrtc-specific-features)
- [MediaMTX 官方 WebRTC reader.js](https://github.com/bluenviron/mediamtx/blob/main/internal/servers/webrtc/reader.js)
- [MediaMTX：在网页中嵌入 WebRTC 流](https://mediamtx.org/docs/read/web-browsers)
- [LiveKit WebRTC XCFramework：visionOS 支持矩阵](https://github.com/livekit/webrtc-xcframework)
- [Apple：NSLocalNetworkUsageDescription](https://developer.apple.com/documentation/BundleResources/Information-Property-List/NSLocalNetworkUsageDescription)
- [Apple：Understanding local network privacy](https://developer.apple.com/documentation/technotes/tn3179-understanding-local-network-privacy)
- [Apple：NSAllowsLocalNetworking](https://developer.apple.com/documentation/bundleresources/information-property-list/nsapptransportsecurity/nsallowslocalnetworking)
- [Apple：WKWebView](https://developer.apple.com/documentation/webkit/wkwebview)
- [W3C WebRTC Recommendation](https://www.w3.org/TR/webrtc/)
