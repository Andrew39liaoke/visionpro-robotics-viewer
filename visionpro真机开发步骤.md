# Vision Pro 真机开发步骤

本文说明如何将本项目安装到 Apple Vision Pro 真机，并验证 D435i RGB 视频通过 MediaMTX WebRTC/WHEP 在 visionOS App 中实时播放。

## 1. 验证目标

完整链路如下：

```text
Intel RealSense D435i
  → Ubuntu 推流主机
  → H.264 / RTSP
  → MediaMTX
  → WebRTC / WHEP
  → Vision Pro 原生 visionOS App
```

真机验证需要确认：

- Vision Pro 可以安装和启动 App。
- App 可以访问 Ubuntu 局域网地址。
- WHEP 信令可以正常完成。
- Vision Pro 可以收到并显示 H.264 视频。
- 视频分辨率、颜色、方向和宽高比正确。
- 断流、重连和前后台切换工作正常。
- 长时间播放没有明显内存增长或延迟累积。

## 2. 环境要求

### 2.1 Ubuntu 推流主机

- Intel RealSense D435i，通过 USB 3.x 连接。
- 已编译 `d435i-webrtc-streamer`。
- 已下载 MediaMTX。
- Ubuntu 与 Vision Pro 位于同一个局域网。
- 推荐 Ubuntu 使用有线网络，Vision Pro 使用 Wi-Fi 6/6E。

### 2.2 Mac 开发环境

- Xcode 26.3 或兼容版本。
- 已安装 visionOS 26.2 SDK。
- 已登录 Apple ID。
- 有可用于真机签名的 Apple Developer Team。
- Mac 与 Vision Pro 位于同一个局域网。

### 2.3 Vision Pro

- visionOS 版本与当前 Xcode 兼容。
- 已开启开发者模式。
- 已与当前 Mac 完成开发配对。

## 3. 启动 Ubuntu 视频服务

### 3.1 查询 Ubuntu 局域网 IP

```bash
hostname -I
```

假设 Ubuntu 的局域网 IP 是：

```text
10.252.68.17
```

应使用 Vision Pro 可以直接访问的局域网 IP，不要使用 `127.0.0.1`、Docker 内部地址或不可路由的虚拟网卡地址。

### 3.2 启动 MediaMTX

打开第一个终端：

```bash
cd visionpro-robotics-viewer/d435i-webrtc-streamer
./scripts/run-mediamtx.sh 10.252.68.17
```

将示例 IP 替换为 Ubuntu 的实际局域网 IP。

启动后应看到类似信息：

```text
浏览器预览: http://10.252.68.17:8889/d435i/
Vision Pro WHEP: http://10.252.68.17:8889/d435i/whep
```

显式传入 IP 很重要。MediaMTX 会通过 WebRTC SDP 向 Vision Pro 公布可访问地址；如果公布了错误网卡的 IP，HTTP 信令可能成功，但视频不会到达 Vision Pro。

### 3.3 启动 D435i 推流

打开第二个终端：

```bash
cd visionpro-robotics-viewer/d435i-webrtc-streamer
./build/d435i-streamer \
  --serial 406122071612 \
  --width 1280 \
  --height 720 \
  --fps 30 \
  --bitrate-kbps 5000 \
  --encoder x264enc \
  --rtsp-url rtsp://127.0.0.1:8554/d435i
```

需要将 `--serial` 替换为实际 D435i 序列号。如果当前只有一台相机，也可以省略该参数。

第一次真机联调推荐固定使用 `x264enc`，先排除硬件编码器和驱动兼容性问题。链路稳定后再切换硬件编码。

## 4. 在浏览器中验证推流

在同一局域网中的 Mac 打开：

```text
http://10.252.68.17:8889/d435i/
```

确认浏览器能够持续显示 D435i 画面，再进行 Vision Pro 测试。

如果浏览器也无法播放，应先检查 D435i、GStreamer、RTSP、MediaMTX 和 Ubuntu 网络，不需要立即排查 visionOS App。

## 5. 配置网络端口

Ubuntu 至少需要允许：

| 端口 | 协议 | 用途 |
|---|---|---|
| `8889` | TCP | MediaMTX WebRTC/WHEP HTTP 信令 |
| `8189` | UDP | WebRTC ICE、DTLS 和 SRTP 视频 |
| `8554` | TCP | 推流程序向 MediaMTX 发布 RTSP |

当前推流程序通过 `127.0.0.1:8554` 发布 RTSP，因此 `8554` 通常只需在 Ubuntu 本机可用。Vision Pro 必须能访问 TCP 8889 和 UDP 8189。

还需要确认：

- Vision Pro 和 Ubuntu 位于同一局域网或存在可路由路径。
- 路由器没有启用 AP Isolation、Client Isolation 或访客网络隔离。
- Ubuntu 防火墙没有拦截 UDP 8189。
- 企业 Wi-Fi 没有禁止客户端之间通信。

## 6. 发起 Vision Pro 与 Xcode 配对

> 第一次配对时，应先从 Xcode 发起配对。只有开始配对或设备以前与 Mac 配对过以后，Vision Pro 的“开发者模式”选项才会出现在“隐私与安全性”中。

1. 确保 Mac 和 Vision Pro 位于同一个局域网，并确认该网络支持 IPv6。
2. 保持 Vision Pro 解锁和佩戴状态。
3. 在 Vision Pro 中打开并停留在：

```text
设置 → 通用 → 远程设备
```

4. 在 Mac 上打开 Xcode，从屏幕顶部的菜单栏选择：

```text
Window → Devices and Simulators
```

也可以使用快捷键：

```text
Shift + Command + 2
```

部分新版 Xcode 将设备管理界面称为 `Device Hub`，也可以从运行目标菜单底部的 `Manage Devices…` 打开。若 `Xcode → Open Developer Tool` 中没有 `Device Hub`，属于正常情况，直接使用 `Window → Devices and Simulators` 即可。

5. 在窗口顶部选择 `Devices`，等待左侧设备列表出现 Vision Pro。
6. 选择搜索到的 Vision Pro，点击 `Pair`，然后按照两台设备上的提示输入配对码。部分新版界面需要先点击 `+`，再选择 `Pair Nearby Device…`。
7. 如果 Xcode 提示需要开启开发者模式，继续执行下一节。

## 7. 开启 Vision Pro 开发者模式并完成配对

Xcode 发起配对后，在 Vision Pro 中打开：

```text
设置 → 隐私与安全性
```

滚动到页面底部的“安全性”区域，打开：

```text
开发者模式
```

然后完成以下操作：

1. 在警告窗口中确认开启并重启 Vision Pro。
2. 重启后解锁并佩戴 Vision Pro。
3. 根据系统提示再次确认启用开发者模式。
4. 回到 Mac 的 `Devices and Simulators` 或 `Device Hub`，继续完成配对，并等待 Xcode 准备设备支持文件。

设备状态正常时，Xcode 顶部的运行目标列表中会显示这台 Vision Pro。

如果“开发者模式”仍未出现或 Xcode 搜索不到设备，可依次检查：

- Vision Pro 是否一直停留在“设置 → 通用 → 远程设备”页面。
- Mac 和 Vision Pro 是否位于同一个支持 Bonjour 和 IPv6 的局域网。
- 路由器是否启用了 AP Isolation、Client Isolation 或访客网络隔离。
- Mac 和 Vision Pro 是否使用兼容的 Xcode、macOS 和 visionOS 版本。
- Vision Pro 是否处于解锁和佩戴状态。
- 在 Vision Pro 的“远程设备”中移除旧的 Mac 配对记录，然后重新配对。
- 关闭并重新打开 `Devices and Simulators`；仍然无效时，重启 Xcode、Mac 和 Vision Pro 后重试。

## 8. 配置 Xcode 签名

打开工作区根目录中的：

```text
Visonpro.xcodeproj
```

在 Xcode 中执行：

1. 选择 `Visonpro` Target。
2. 打开 `Signing & Capabilities`。
3. 启用自动签名。
4. 在 `Team` 中选择自己的 Apple Developer Team。
5. 检查 Bundle Identifier。

当前 Bundle Identifier 是：

```text
com.liaoke.Visonpro
```

如果该标识无法用于当前开发团队，将它改成自己的唯一标识，例如：

```text
com.example.robotics.visionproviewer
```

当前项目无需 Vision Pro 摄像头或麦克风权限。App 只接收远程视频。

## 9. 安装并启动 App

1. Xcode 顶部选择 `Visonpro` Scheme。
2. 运行目标选择已配对的 Vision Pro 真机。
3. 点击 Run，或按 `Command + R`。
4. 等待 Xcode 编译、签名并安装 App。
5. 如果 Vision Pro 出现开发者 App 或设备配对提示，按照系统提示确认。

项目已经通过以下本地验证：

- visionOS Simulator Debug 编译。
- visionOS device Release 无签名编译。
- WHEP 协议单元测试。
- visionOS 原生 WebRTC offer、视频帧邮箱和断开竞态测试。

无签名编译只能证明设备目标可以编译。安装到实际 Vision Pro 仍需要开发团队和设备签名。

## 10. 配置视频连接

App 第一次启动时会打开连接设置。

输入 WHEP 地址：

```text
http://10.252.68.17:8889/d435i/whep
```

将 IP 替换为 Ubuntu 的实际局域网 IP。

第一阶段使用以下设置：

```text
认证方式：无需认证
裁剪画面以填满区域：关闭
```

点击“保存并连接”。

系统第一次访问局域网时会显示本地网络权限提示。选择“允许”。

正常状态变化如下：

```text
正在连接视频主机
  → 正在协商视频
  → 等待首帧
  → 实时播放
```

第一次成功播放后，App 会记住连接地址，并在下次启动时自动连接。

## 11. 查看视频统计

点击播放界面中的柱状图按钮，可以查看：

- 当前视频分辨率。
- 接收帧率。
- 接收码率。
- WebRTC RTT。
- 网络 jitter。
- 区间丢包率。
- 已解码帧数和丢弃帧数。
- ICE candidate 类型和 UDP/TCP 路径。
- 自动重连次数。

正常播放时建议观察：

```text
分辨率：1280 × 720
帧率：约 27～30 fps
接收码率：持续非零
传输协议：优先 UDP
```

RTT 只是网络往返时间，不能代表摄像头到 Vision Pro 显示的完整 glass-to-glass 延迟。

## 12. 真机功能验收

### 12.1 基础播放

- App 能成功进入“实时播放”。
- 画面不是静止的旧帧。
- 视频方向正确。
- 视频颜色正常，没有明显红蓝通道颠倒或偏色。
- 16:9 宽高比正确，没有拉伸。
- 完整显示和裁剪填充两种模式都能正常切换。

### 12.2 断流与恢复

测试以下场景：

1. 播放过程中停止 `d435i-streamer`。
2. 确认 App 在约 5 秒内识别长时间没有新视频帧。
3. 重新启动 `d435i-streamer`。
4. 确认 App 经过自动重连后恢复播放。

自动重连使用约 1、2、4、8、10 秒退避，并加入少量随机扰动。

### 12.3 网络恢复

1. 播放过程中断开 Vision Pro Wi-Fi。
2. 分别等待 5、15、30 秒。
3. 恢复 Wi-Fi。
4. 确认 App 能重新连接并恢复视频。

### 12.4 App 生命周期

- 让 App 进入非活动状态或后台。
- 返回 App。
- 确认旧 PeerConnection 已释放。
- 确认 App 可以重新建立视频连接。
- 点击“断开”后切换前后台，不应自动重新连接。

### 12.5 稳定性

连续运行 30～60 分钟，检查：

- App 没有崩溃。
- 视频延迟没有逐渐增加到数秒。
- 接收帧率没有持续下降。
- Vision Pro 没有出现明显热失控。
- App 内存没有持续增长。
- Ubuntu 推流端没有持续堆积旧帧。

## 13. 延迟测试

不能只用 WebRTC RTT 作为视频延迟结果。建议执行 glass-to-glass 测试：

1. 在外部显示器运行毫秒计时器。
2. 让 D435i 拍摄计时器。
3. 使用另一台相机同时拍到源计时器和 Vision Pro 最终显示结果。
4. 至少记录 100 个样本。
5. 计算中位数、P95 和最大延迟。

目标值：

| 指标 | 目标 |
|---|---:|
| 中位 glass-to-glass 延迟 | 小于 250 ms |
| P95 glass-to-glass 延迟 | 小于 400 ms |
| 正常局域网接收帧率 | 27～30 fps |

以上是目标值，必须以真实 Vision Pro 和真实局域网的测量结果为准。

## 14. 常见故障排查

### 14.1 无法连接视频主机

检查：

- WHEP 地址是否使用 Ubuntu 实际 IP。
- Vision Pro 和 Ubuntu 是否在同一局域网。
- TCP 8889 是否开放。
- Vision Pro 是否允许 App 访问本地网络。
- Mac 浏览器是否能打开 MediaMTX 预览页面。

如果之前拒绝了本地网络权限，在 Vision Pro 中打开：

```text
设置 → 隐私与安全性 → 本地网络
```

为 App 重新启用权限。

### 14.2 HTTP 404

表示 WHEP path 不存在或当前没有对应视频流。检查：

- 地址是否以 `/d435i/whep` 结尾。
- `d435i-streamer` 是否正在运行。
- RTSP 发布地址是否为 `/d435i`。
- MediaMTX 是否接收到了发布流。

### 14.3 一直显示“等待首帧”

通常表示 WHEP HTTP 信令已经成功，但 ICE 媒体连接不可用。重点检查：

- Ubuntu UDP 8189 是否开放。
- 启动 MediaMTX 时是否传入了正确 LAN IP。
- `webrtcAdditionalHosts` 是否包含 Vision Pro 可以访问的 IP。
- 路由器是否启用客户端隔离。
- Vision Pro 和 Ubuntu 是否位于不同 VLAN。

### 14.4 H.264 协商失败

检查发送端：

- 使用 H.264 Baseline。
- 没有 B 帧。
- GOP 约为 1 秒。
- 第一次验证使用 `--encoder x264enc`。

### 14.5 浏览器正常，但原生 App 异常

Debug 版本中：

1. 展开统计面板。
2. 点击“打开网页诊断”。
3. App 会断开原生会话并加载 MediaMTX 自带网页播放器。

如果网页播放器正常，应重点检查 Xcode 控制台中的 WHEP、SDP、ICE 和解码错误。如果网页播放器也异常，应检查网络或服务器。

### 14.6 修改 Ubuntu IP 后无法连接

需要同时修改：

- `run-mediamtx.sh` 启动参数。
- App 中保存的 WHEP URL。
- 防火墙或路由规则。

重新启动 MediaMTX 后再连接。

## 15. 安全说明

当前局域网开发环境允许使用：

```text
http://<LAN-IP>:8889/d435i/whep
```

HTTP 信令只适合可信开发网络。准备部署时应：

- 为 MediaMTX 配置可信 HTTPS/WHEPS。
- 增加只读用户或短期 Bearer Token。
- 限制视频 path 的读取权限。
- 公网或复杂 NAT 环境部署 TURN。
- 不在代码和仓库中保存真实密码或 Token。

App 的 Basic/Bearer 凭据单独保存在 Vision Pro Keychain 中，不允许将凭据写入 URL。

## 16. 相关文档

- [visionOS App 使用说明](visionos-app-README.md)
- [Vision Pro WebRTC 客户端开发方案](visionos-webrtc-viewer-development-plan.md)
- [D435i WebRTC 推流端说明](d435i-webrtc-streamer/README.md)
- [D435i RGB 视频开发方案](d435i-rgb-stream-development-plan.md)
