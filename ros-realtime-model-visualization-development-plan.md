# ROS 实时建模结果在 Apple Vision Pro 中展示的开发方案

## 1. 文档范围

本文只描述一个功能：接收 ROS 中已经生成的实时建模结果，并在 Apple Vision Pro 中进行三维显示。

本文假设 SLAM、点云融合、网格生成等建模工作已经在 ROS 主机上完成。Vision Pro 只负责接收和渲染，不负责执行建模算法。

本文不涉及 D435i RGB 视频采集、WebRTC 视频播放，也不在 Vision Pro 端根据原始深度数据重新建模。

## 2. 开发目标

```text
ROS 建模程序
   │
   ├─ PointCloud2 / Mesh / Marker / OccupancyGrid
   ├─ /tf
   └─ /tf_static
          │
          ▼
ROS-VisionPro Gateway
          │ 二进制网络协议
          ▼
Vision Pro 原生 visionOS App
          │
          ▼
Metal / RealityKit 实时三维显示
```

## 3. 功能范围

### 3.1 包含内容

- 配置指定 ROS 建模话题。
- 订阅点云、网格、Marker 或地图消息。
- 订阅 `/tf` 和 `/tf_static`。
- ROS 消息到网络消息的转换。
- 点云降采样、裁剪和压缩。
- 模型完整快照和增量更新。
- Vision Pro 网络接收和协议解析。
- ROS 坐标系到 RealityKit 坐标系转换。
- Metal 点云渲染。
- RealityKit 网格、Marker 和地图显示。
- 模型显示、隐藏、移动、缩放和重置。
- 断线重连和模型恢复。

### 3.2 不包含内容

- 摄像头视频流。
- 深度相机驱动和原始深度图处理。
- SLAM、点云配准或建图算法开发。
- ROS 控制指令和机器人运动控制。

## 4. 输入数据要求

开发前必须确认建模系统实际发布的消息类型和话题名称。

| 数据 | ROS 消息类型 | 用途 |
|---|---|---|
| 实时点云 | `sensor_msgs/msg/PointCloud2` | 点云模型显示 |
| 三角网格 | `shape_msgs/msg/Mesh` | 表面模型显示 |
| 可视化对象 | `visualization_msgs/msg/Marker` | 线、点、文本和模型 |
| 可视化对象集合 | `visualization_msgs/msg/MarkerArray` | RViz 风格场景 |
| 二维地图 | `nav_msgs/msg/OccupancyGrid` | 平面地图显示 |
| 体素地图 | `octomap_msgs/msg/Octomap` | 三维占据地图 |
| 动态坐标 | `tf2_msgs/msg/TFMessage` | 实体实时位姿 |
| 静态坐标 | `tf2_msgs/msg/TFMessage` | 固定外参 |

必要检查命令：

```bash
ros2 topic list
ros2 topic type <topic_name>
ros2 topic info <topic_name> -v
ros2 topic hz <topic_name>
ros2 topic bw <topic_name>
ros2 interface show <message_type>
```

如果上游只发布深度图而没有点云、网格或地图，该输入不属于已完成的建模结果，需要先由 ROS 建模节点生成可显示的数据。

## 5. 技术方案

### 5.1 不让 Vision Pro 直接加入 ROS 2 DDS

不推荐直接把 ROS 2、RMW 和 DDS 移植到 visionOS：

- visionOS 平台依赖适配成本高。
- DDS 发现机制对无线网络和网络隔离较敏感。
- App 会与 ROS 发行版、RMW 实现及自定义消息强耦合。
- 原始点云带宽高，不适合直接透传。
- 不利于认证、限流、重连和协议演进。

因此增加独立网关：

```text
ROS Topic → ROS-VisionPro Gateway → Vision Pro Protocol
```

### 5.2 网络传输

- 控制、状态、TF：WebSocket。
- 点云和网格：二进制 WebSocket 或 QUIC。
- 不使用 JSON 传输大型点云。
- 不把点云或网格编码成视频。

原型阶段可以使用 `rosbridge_suite` 验证连接，但其默认 JSON/Base64 路径不适合作为密集点云的正式传输方案。

正式版本建议实现轻量 ROS 2 网关节点，对消息进行过滤、二进制序列化、压缩和增量传输。

### 5.3 Vision Pro 渲染

| 输入类型 | 推荐渲染方式 |
|---|---|
| `PointCloud2` | Metal Vertex Buffer 或 RealityKit LowLevelMesh |
| `Mesh` | RealityKit MeshResource |
| `Marker.POINTS` | Metal 点渲染 |
| `Marker.LINE_LIST` | 线段或细圆柱体 |
| `Marker.TRIANGLE_LIST` | RealityKit 三角网格 |
| `OccupancyGrid` | 动态纹理平面 |
| `Octomap` | 体素实例化渲染 |
| `/tf` | RealityKit Entity Transform |

## 6. ROS-VisionPro Gateway

### 6.1 职责

1. 连接现有 ROS 2 graph。
2. 订阅配置的话题。
3. 解析标准消息或项目自定义消息。
4. 应用 TF，将数据转换到选定根坐标系。
5. 对点云进行裁剪和体素降采样。
6. 将 ROS 消息转换为稳定的网络协议。
7. 压缩大型数据。
8. 发送完整快照和增量更新。
9. 处理客户端断开和重新同步。
10. 输出带宽、处理耗时和丢帧指标。

### 6.2 内部结构

```text
RosTopicSubscriber
  → TfResolver
  → ModelNormalizer
  → PointCloudDownsampler / MeshOptimizer
  → ProtocolEncoder
  → Compression
  → ClientSessionManager
```

### 6.3 配置示例

```yaml
ros:
  root_frame: map
  tf_topic: /tf
  tf_static_topic: /tf_static

model:
  topic: /mapping/model
  message_type: sensor_msgs/msg/PointCloud2
  update_rate_hz: 5
  voxel_size_m: 0.03
  max_range_m: 20.0
  include_rgb: true

transport:
  protocol: binary_websocket
  listen_address: 0.0.0.0
  port: 8765
  tls: true
  compression: zstd

snapshot:
  send_on_connect: true
  incremental_updates: true
  resync_interval_seconds: 30
```

## 7. 网络协议设计

### 7.1 通用消息头

每条消息至少包含：

```text
protocolVersion
messageType
sessionId
modelId
sequence
timestampNs
frameId
payloadEncoding
compression
payloadLength
```

### 7.2 消息类型

```text
HELLO
CAPABILITIES
MODEL_SNAPSHOT_BEGIN
MODEL_SNAPSHOT_CHUNK
MODEL_SNAPSHOT_END
MODEL_DELTA
TF_UPDATE
DELETE_ENTITY
CLEAR_MODEL
RESYNC_REQUEST
HEARTBEAT
ERROR
```

### 7.3 点云载荷

首版使用固定布局，避免 Vision Pro 动态解释任意 `PointCloud2.fields`：

```text
PointXYZRGB32
  x: Float32
  y: Float32
  z: Float32
  rgba: UInt32
```

每点 16 字节。传输前执行：

- 去除 NaN 和 Inf。
- 范围裁剪。
- VoxelGrid 降采样。
- 可选 Zstd 压缩。

### 7.4 网格载荷

```text
vertices: Float32[x, y, z]
normals: Float32[x, y, z]（可选）
colors: UInt8[r, g, b, a]（可选）
indices: UInt32[i0, i1, i2]
transform: Float32[16]
```

静态网格优先发送一次完整数据，后续只发送 Transform 或变化分块。

## 8. 坐标系处理

### 8.1 ROS 根坐标系

网关选择统一根坐标系，例如 `map`。所有模型数据在传输前转换到该坐标系，或者明确携带 `frame_id` 和对应 TF。

### 8.2 RealityKit 坐标转换

Vision Pro 端定义唯一的 ROS-to-RealityKit 转换矩阵：

```text
T_realitykit_ros
```

统一处理轴方向、左右手坐标系差异、米制单位、四元数顺序和矩阵存储顺序。禁止在各渲染器中分别硬编码坐标转换。

### 8.3 显示模式

模型查看器模式：

- 模型显示在独立 Volume 或 ImmersiveSpace 中。
- 用户可以移动、缩放和旋转整个模型。
- 不要求与现实世界精确对齐。

现实空间叠加模式：

- 需要求解 ROS `map` 与 visionOS 世界坐标之间的变换。
- 可使用 AprilTag、二维码、已知锚点或人工标定。
- 应作为独立子阶段实现。

## 9. Vision Pro 客户端设计

建议目录：

```text
VisionProModelApp/
├── App/
│   ├── VisionProModelApp.swift
│   └── AppConfiguration.swift
├── Network/
│   ├── ModelStreamClient.swift
│   ├── ProtocolDecoder.swift
│   ├── ReconnectionPolicy.swift
│   └── ModelStreamMetrics.swift
├── Model/
│   ├── ModelSnapshot.swift
│   ├── ModelDelta.swift
│   ├── PointCloudFrame.swift
│   ├── MeshFrame.swift
│   └── TransformTree.swift
├── Rendering/
│   ├── PointCloudRenderer.swift
│   ├── MeshRenderer.swift
│   ├── MarkerRenderer.swift
│   ├── OccupancyGridRenderer.swift
│   └── CoordinateConverter.swift
└── UI/
    ├── ModelViewer.swift
    ├── ConnectionStatusView.swift
    └── ModelControlsView.swift
```

状态机：

```text
idle
  → connecting
  → negotiating
  → receivingSnapshot
  → live
  → resyncing
  → reconnecting
  → failed
```

GPU 更新原则：

- 网络线程不直接修改 RealityKit 场景。
- 协议解析和解压在后台执行。
- 使用双缓冲或三缓冲更新 GPU Buffer。
- 完整分块到达后再交换缓冲。
- 旧更新可以丢弃，不允许无限排队。
- 大型网格按空间分块管理。

## 10. 数据量与性能策略

密集点云不能以传感器原始分辨率和帧率直接发送。

| 数据 | 建议更新率 |
|---|---:|
| 实时局部点云 | 5～10 Hz |
| 全局点云地图 | 1～5 Hz 或增量更新 |
| 网格变化 | 事件触发或 1～5 Hz |
| `/tf` | 10～30 Hz，按需求限制 |
| 静态 TF | 连接时发送一次 |

优化手段：

- VoxelGrid 降采样。
- 距离和视野裁剪。
- 删除无效点。
- 按空间块传输。
- 只发送变化块。
- 颜色字段可选。
- 使用 Zstd、Draco 或适合数据类型的压缩。
- 客户端可请求更低的数据密度。

## 11. 开发阶段

### 阶段 A：确定 ROS 输出契约

- 确认 ROS 版本和发行版。
- 获取完整话题列表。
- 确认建模话题类型、更新率、带宽和消息尺寸。
- 确认根坐标系和 TF 链。
- 保存一段 rosbag2 作为固定测试数据。

完成标准：明确至少一个模型话题及必需 TF，rosbag2 可以稳定复现输出。

### 阶段 B：ROS 网关原型

- 订阅模型话题。
- 转换为固定内部格式。
- 实现点云过滤或网格分块。
- 实现二进制序列化。
- 通过局域网发送给桌面测试客户端。
- 增加完整快照和重新同步。

完成标准：桌面客户端持续解析模型数据，客户端变慢时网关不持续积压。

### 阶段 C：Vision Pro 显示

- 创建 visionOS 原生项目。
- 实现网络客户端和协议解析。
- 完成坐标转换。
- 优先实现项目实际使用的一种消息类型。
- 使用 Metal 或 RealityKit 显示。
- 增加模型移动、缩放和重置。

完成标准：Vision Pro 稳定显示实时模型，方向、比例和位置正确。

### 阶段 D：增量更新和优化

- 实现模型分块、增量更新和删除。
- 实现断线重连后的快照恢复。
- 调整降采样和压缩参数。
- 完成 30～60 分钟稳定性测试。

### 阶段 E：可选现实空间配准

- 选择标定方法。
- 求解 `T_visionWorld_rosMap`。
- 保存和恢复标定结果。
- 验证模型与现实对象的空间误差。

## 12. 验收标准

### 12.1 功能验收

- 能连接指定 ROS 网关。
- 能接收完整模型快照并持续应用更新。
- 能正确应用 `/tf` 和 `/tf_static`。
- 点云或网格的方向、比例和颜色正确。
- 用户可以显示、隐藏、移动、缩放和重置模型。
- 断线重连后可以自动请求完整快照。
- 删除和清空命令能够正确更新场景。

### 12.2 性能验收

- 连续运行 30 分钟无崩溃。
- 内存和 GPU Buffer 不持续增长。
- 网络线程不阻塞 UI 和渲染线程。
- 网络变慢时跳过旧更新，不累积数秒延迟。
- 模型更新过程中头部移动和系统交互保持流畅。

### 12.3 可观测性

至少记录 ROS 输入消息率和带宽、降采样前后点数、序列化与压缩耗时、压缩比、网络速率、客户端解析耗时、GPU 上传耗时、显示点数或三角形数量、丢弃更新数和重新同步次数。

## 13. 测试方案

### 13.1 数据正确性

- 使用固定 rosbag2 重放。
- 与 RViz2 同时显示并比较。
- 检查坐标轴方向、米制比例、颜色字段和 TF 更新。

### 13.2 网络测试

- 正常和弱信号局域网。
- 延迟、抖动和丢包。
- 网关中断后恢复。
- Vision Pro App 重启。
- 完整快照过程中断线。

### 13.3 模型规模测试

- 小型、中型和最大预期模型。
- 高频局部点云。
- 大型静态网格。
- 长时间增量更新。

## 14. 风险与处理

| 风险 | 表现 | 处理方式 |
|---|---|---|
| ROS 话题类型未确定 | 无法设计客户端 | 先冻结输入契约并保存 rosbag2 |
| 直接发送原始点云 | 带宽和内存过高 | 网关降采样、裁剪、压缩和分块 |
| JSON/Base64 开销大 | 延迟和 CPU 增加 | 正式版本使用二进制协议 |
| TF 缺失或错误 | 模型位置和方向错误 | 明确根坐标系并校验 TF 链 |
| 坐标转换分散 | 不同对象方向不一致 | 统一 `CoordinateConverter` |
| GPU Buffer 频繁重建 | 卡顿和内存抖动 | 预分配并使用多缓冲 |
| 增量更新丢失 | 模型状态不一致 | sequence 校验和完整重同步 |
| 模型无限增长 | 内存耗尽 | 空间分块、删除策略和资源上限 |

## 15. 交付物

- ROS 输入话题与消息契约文档。
- 可复现测试数据 `rosbag2`。
- ROS-VisionPro Gateway。
- 二进制网络协议说明。
- Vision Pro 模型接收客户端。
- 项目实际消息类型的渲染器。
- TF 和坐标系转换模块。
- 模型交互界面。
- 性能指标和稳定性测试报告。
- 部署与启动说明。

## 16. 开发前确认项

- ROS 1还是 ROS 2，以及具体发行版。
- 建模程序名称。
- 建模结果话题名称和消息类型。
- 是否使用自定义消息。
- 模型更新频率和单条消息大小。
- 根坐标系名称。
- 是否需要颜色。
- 显示点云、网格还是两者都需要。
- 是否只做独立模型查看器。
- 是否需要模型与现实空间精确叠加。

## 17. 参考资料

- [rosbridge_suite](https://github.com/RobotWebTools/rosbridge_suite)
- [ROS 2 sensor_msgs/PointCloud2](https://docs.ros.org/en/rolling/p/sensor_msgs/msg/PointCloud2.html)
- [ROS 2 visualization_msgs/Marker](https://docs.ros.org/en/rolling/p/visualization_msgs/msg/Marker.html)
- [Apple RealityKit](https://developer.apple.com/documentation/realitykit)
- [Apple LowLevelMesh](https://developer.apple.com/documentation/realitykit/lowlevelmesh)

