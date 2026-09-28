# visionOS D435i 视频客户端

原生 SwiftUI 应用，通过 MediaMTX WHEP 接收 H.264 RGB 视频。工程位于上一级目录的 `Visonpro.xcodeproj`。

## 启动

1. 在 Ubuntu 上按 [推流端说明](d435i-webrtc-streamer/README.md) 启动 MediaMTX 和 D435i 推流。
2. Mac 使用 Xcode 26.3 或兼容版本打开 `Visonpro.xcodeproj`，等待 Swift Package 依赖下载完成。
3. 选择 `Visonpro` scheme 和 Apple Vision Pro。部署目标为 visionOS 26.2；真机运行需要在 Signing & Capabilities 中选择你的开发团队，并完成设备配对。
4. 运行 App，在首次出现的设置中输入：

```text
http://<Ubuntu局域网IP>:8889/d435i/whep
```

5. 默认选择“无需认证”，点击“保存并连接”，允许本地网络访问。

正常情况下状态会依次显示连接主机、协商、等待首帧和实时播放。首次成功后会记住地址，下次启动自动连接。切到后台或关闭窗口会释放连接；手动断开后不会因前台切换自动重连。

地址中不接受用户名、密码、query 或 fragment；Basic / Bearer 凭据通过设置单独配置，并按完整 WHEP 地址保存到 Keychain。修改地址时应同步填写相应凭据。

## 操作

- 齿轮：修改地址、认证、画面填充方式，查看开源许可。
- 缩放按钮：在完整画面与裁剪填充之间切换。
- 柱状图按钮：展开接收码率、帧率、解码/丢弃帧、区间丢包、RTT、jitter 和 ICE 路径。
- 重新连接：释放当前会话并重新协商。
- 断开：停止播放和自动重连。
- Debug 版本统计面板中的“打开网页诊断”：暂停原生连接，打开 MediaMTX 自带播放器；关闭诊断后可手动连接原生播放器。

诊断数据为缺失值时显示 `—`。RTT 是网络往返时间，不是摄像头到眼镜显示的完整延迟。

## 实现

| 位置 | 职责 |
|---|---|
| `Visonpro/ContentView.swift`、`Views/` | 播放界面、设置、Metal 渲染 |
| `Visonpro/Streaming/StreamSession.swift` | UI 状态、生命周期、重连和旧会话回调隔离 |
| `Visonpro/Streaming/WHEPClient.swift` | OPTIONS / POST / PATCH / DELETE、超时、资源清理 |
| `Visonpro/Streaming/WebRTCEngine.swift` | WebRTC API 适配、H.264 优先协商、远端轨道和统计 |
| `Packages/StreamingCore` | 可独立测试的 URL、ICE、SDP fragment、HTTP、退避和指标计算 |
| `VisonproTests` | visionOS 原生 WebRTC 和会话竞态测试 |

LiveKitWebRTC 精确锁定 `150.7871.02`，SwiftPM 显示为 `150.7871.2`。该版本的 visionOS 二进制未包含 SDK Metal View，因此渲染使用 `MTKView + CIContext`。解码线程只更新单帧邮箱，渲染时取最新帧；最多允许两个 GPU command 在途，不积压历史视频帧。原生 CVPixelBuffer 保留到 GPU 完成，I420 作为软件解码兜底转换为 NV12。

当前应用只协商一路 recvonly video，不采集麦克风或摄像头。默认模板 RealityKit 内容保留在仓库中，但已从 App 链接依赖移除。

## 构建与测试

在工作区根目录运行：

```bash
swift test --package-path Packages/StreamingCore --scratch-path .build/CoreTests

xcodebuild -project Visonpro.xcodeproj -scheme Visonpro \
  -destination 'generic/platform=visionOS Simulator' \
  -derivedDataPath .build/DerivedData \
  -clonedSourcePackagesDirPath .build/SourcePackages \
  CODE_SIGNING_ALLOWED=NO build

xcodebuild -project Visonpro.xcodeproj -scheme Visonpro \
  -destination 'platform=visionOS Simulator,name=Apple Vision Pro' \
  -derivedDataPath .build/DerivedData \
  -clonedSourcePackagesDirPath .build/SourcePackages \
  CODE_SIGNING_ALLOWED=NO test

xcodebuild -project Visonpro.xcodeproj -scheme Visonpro \
  -destination 'generic/platform=visionOS' -configuration Release \
  -derivedDataPath .build/DeviceBuild \
  -clonedSourcePackagesDirPath .build/SourcePackages \
  CODE_SIGNING_ALLOWED=NO build
```

无签名 device build 仅检查设备目标编译；安装到真机需通过 Xcode 配置签名。

## 联调排错

| 现象 | 检查 |
|---|---|
| 无法连接视频主机 | 地址是否为 Ubuntu IP；Wi-Fi 是否同网段；本地网络权限；TCP 8889 |
| HTTP 404 | `d435i-streamer` 是否启动；路径是否为 `/d435i/whep` |
| 协商成功但没有首帧 | UDP 8189、防火墙、Wi-Fi AP 隔离、`webrtcAdditionalHosts` 是否为实际 LAN IP |
| H.264 协商错误 | 推流是否使用 H.264 Baseline、无 B 帧；先用 `--encoder x264enc` |
| 网页正常，原生异常 | Xcode 控制台筛选 `com.liaoke.Visonpro`，保留状态和统计记录 |
| 短暂断网 | 默认 1/2/4/8/10 秒退避重试，带 ±20% 随机扰动；最后一帧超过 5 秒自动重建会话 |
| 401/403 | 修正凭据后手动连接，认证失败不会无限重试 |

HTTP 信令仅用于可信局域网开发。当前 Debug/Release 都支持局域网 HTTP；产品部署应给 MediaMTX 配置可信 HTTPS、只读鉴权与适当网络边界。App 不绕过证书校验，不跟随 WHEP HTTP 重定向，不把凭据发往跨来源会话地址。

## 真机验收待办

本地构建和测试不能代替真实 D435i → Ubuntu → Wi-Fi → Vision Pro 联调。仍需记录首帧时间、720p30 帧率、真实颜色、5～30 秒断网恢复、设备休眠恢复，以及 60 分钟内存/温度和 glass-to-glass 延迟。原开发文档中的 250/400 ms 延迟为目标值，尚无真机测量结果。
