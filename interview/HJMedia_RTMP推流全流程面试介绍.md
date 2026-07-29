# HJMedia：从相机采集到 RTMP 推流成功

## 30 秒面试版

HJMedia 的推流可以概括为两条并行支路汇合：视频侧把摄像头画面经过预览和可选的 GPU 特效处理后，交给硬件编码器压缩成 H.264/H.265；音频侧把麦克风 PCM 统一成编码器需要的采样格式后，编码为 AAC。随后 `HJGraphPusher` 按时间戳交织两路编码帧，RTMP 模块将它们组织为 FLV Tag 并完成连接、建流和发送。整个过程由 Graph 负责组装和生命周期管理，由 Plugin 负责采集、处理、编码、交织和发送等单一职责；各环节之间通过有容量上限的帧队列传递数据，以反压控制延迟和内存。

## 一分钟面试版

以 HarmonyOS 的实际主链路为例，应用层调用 `openPusher` 后，先把分辨率、帧率、码率、GOP 和 RTMP 地址交给 `HJGraphPusher`。视频数据经历四次明确的格式变化。

第一步是**采集格式**：相机输出的是原始画面。在 CPU 读回场景中它可以是 NV12、NV21 或 YUV420 等 YUV buffer；但本项目 Harmony 主路径不把它固定为 CPU YUV，而是通过相机 Surface 进入 RTE，表现为 GPU texture / Surface 中的原始图像。

第二步是**渲染处理格式**：`SourceBridge` 接收 texture，RTE 在 GPU 上完成旋转、镜像、缩放，以及可选的美颜、贴纸、人脸处理。处理前后本质上仍是未压缩的图像，只是数据载体从“相机输入 Surface/texture”变成“编码器输出 Surface/texture”；期间不必读回 CPU。这解决了直播中 YUV 在 CPU 与 GPU 间反复拷贝造成的带宽和时延问题。

第三步是**压缩编码格式**：`TargetEncoder` 把处理后的画面送入 `HJPluginVideoOHEncoder`，Harmony 硬编器按项目配置的 `video/avc` 或 `video/hevc` 编码。代码中输入像素格式配置为 NV12；编码后输出不是 MP4 文件，也不是 FLV，而是 H.264/H.265 的 ES 编码帧（关键帧/普通帧）以及 codec data，例如 H.264 的 SPS/PPS。码率从原始图像的每帧大量像素数据，变为适于网络传输的压缩 NALU 数据。

第四步是**传输封装格式**：`HJPluginAVInterleave` 按 DTS 将 H.264/H.265 视频帧与 AAC 音频帧交织。`HJRTMPPacketManager` 先把 SPS/PPS 等 codec data 转成 AVC/HEVC sequence header，再经 `HJFLVUtils` 给每帧补上 FLV Video Tag 头、DTS 和 CTS（PTS-DTS），最终得到 RTMP 可发送的 FLV Tag。底层完成 `RTMP_Connect`、`RTMP_ConnectStream` 后持续发送这些 Tag；收到 `HJRTMP_EVENT_STREAM_CONNECTED` 才说明流已建立、可以认为推流成功。

一句话总结视频格式链路就是：**相机原始图像（YUV buffer 或 Surface/texture）→ GPU 处理后的 Surface/texture → H.264/H.265 ES + SPS/PPS → FLV Video Tag → RTMP 网络包。**

## 可在白板上画出的全链路

```text
应用层 openPusher(videoInfo, audioInfo, rtmpUrl)
                 │
                 ▼
          HJGraphPusher：组装、连接、启动、停止

视频支路（HarmonyOS 主路径）
摄像头 → RTE SourceBridge → 可选 GPU 特效/人脸处理 → TargetEncoder Surface
                                                     → VideoOHEncoder
                                                     → H.264/H.265 编码帧

音频支路
麦克风 → AudioOHCapturer → PCM → AudioResampler + FIFO → FDKAACEncoder
                                                     → AAC 编码帧

编码帧汇合
H.264/H.265 + AAC → AVInterleave（按 DTS 交织） → RTMPMuxer
                                                → FLV Tag / RTMP 消息
                                                → RTMP 连接、建流、发送
                                                → 流媒体服务器
```

## 分阶段说明

### 1. 配置与建图

入口 `src/entry/pusher/hsys/verify/HJNAPILiveStream.cpp` 将宽高、码率、帧率、GOP、音频采样率/声道和 RTMP URL 转为 `HJVideoInfo`、`HJAudioInfo`、`HJMediaUrl`，并创建 `HJGraphPusher`。

`src/graphs/HJGraphPusher.cpp` 中的核心连接是：

```text
AudioOHCapturer → AudioResampler → FDKAACEncoder ─┐
                                                   ├→ AVInterleave → RTMPMuxer
VideoOHEncoder（H.264/H.265 ES）───────────────────┘
```

视频的相机采集与 GPU 处理链由入口渲染系统建立；`HJGraphPusher` 为它创建 `VideoOHEncoder` 并提供编码输出 Surface，因此两部分在编码器处对接。

### 2. 相机画面、YUV 与 GPU 零拷贝

面试中不要绝对地说“相机采集后一定先得到 CPU 中的 YUV”。原始视频在概念上可以理解为 YUV/原始图像数据，但具体载体取决于平台：可能是 CPU 可访问的 YUV buffer，也可能是相机 Surface / GPU texture。

本项目 HarmonyOS 推流主路径偏向后者：RTE 中的 `HJNodeClass_SourceBridge` 输入画面，`HJNodeClass_TargetEncoder` 对应 `HJRteComDrawEGLEncoder`，将 GPU 渲染结果送到编码 Surface。硬编器在内部把输入转换为其需要的编码格式并输出压缩码流。这是低延迟直播中更合理的路径，因为减少了跨 CPU/GPU 的内存拷贝和带宽压力。

如果业务拿到的是 CPU YUV，则中间会多出颜色格式转换、缩放、旋转、上传纹理或直接送编码器等步骤；其目标仍是将分辨率、像素格式、时间戳与编码器配置对齐。

### 3. 视频处理和编码

视频处理链可以按需插入 RTE、Prio、Faceu 或检测模块；它们不是 RTMP 推流的必经环节。处理完成后，`HJPluginVideoOHEncoder` 根据 `videoInfo` 中的分辨率、码率、帧率、GOP 等参数进行硬件编码，输出的是 H.264/H.265 **编码帧**，不是 FLV。

关键帧和 codec configuration 很重要：推流端要先让服务端/播放器拿到 SPS/PPS（或 H.265 对应参数集）等 sequence header，播放器才可以正确初始化解码器；此后按 GOP 发送关键帧和预测帧。

### 4. 音频采集、格式统一和 AAC 编码

`HJPluginAudioOHCapturer` 采集的是 PCM。PCM 只说明“未压缩”，不意味着它已经符合 AAC 编码器要求，因此需要重采样器统一：

- 采样率，例如 44.1 kHz 转 48 kHz；
- 声道数和布局，例如单声道转双声道；
- 采样格式和数据布局，例如 S16/float、packed/planar；
- 每个编码帧的样本数。

`HJPluginAudioResampler` 的 FIFO 会积累或拆分采样，使输入对齐 AAC 编码帧大小；`HJPluginFDKAACEncoder` 再输出 AAC 帧。转换后还要保持连续 PTS，否则音视频同步会漂移。

### 5. A/V 交织，而不是重新编码或“把视频编码成 FLV”

`HJPluginAVInterleave` 同时预览音频、视频输入队列的队首帧，并比较 DTS：谁的 DTS 更早，就先取谁并转交给下游。它解决的是输出顺序问题，避免两路异步编码导致 RTMP 包的时间顺序混乱。

这里应严格区分三个概念：

- H.264/H.265、AAC：压缩编码格式；
- FLV Tag：RTMP 常用的音视频消息封装形式；
- RTMP：将这些消息发送至流媒体服务器的传输协议。

### 5.1 Harmony 编码输出到 RTMP/FLV 的转换：不是“MP4 转 FLV”

这部分在面试中最容易把 **编码格式** 和 **封装容器** 混淆。结合当前仓库源码，准确说法是：Harmony 硬编器输出的是 H.264/H.265 的**编码视频数据（ES，Elementary Stream）**及 codec configuration，而不是一个 MP4 文件；RTMP 模块再将这些编码数据重封装为 FLV Video Tag 后发送。

代码证据如下：

- `HJVEncOHCodec::init()` 以 `video/avc` 或 `video/hevc` MIME 创建 `OH_VideoEncoder`，并配置宽高、帧率、NV12、CBR 码率和 I 帧间隔；这里没有创建 MP4 容器。
- `HJVEncOHCodec::getFrame()` 收到 `AVCODEC_BUFFER_FLAGS_CODEC_DATA` 时，将其保存为编码器配置数据（例如 H.264 的 SPS/PPS）；普通输出帧被设置为 `HJDATA_TYPE_ES`，关键帧会携带 codec parameters。这明确表明交给推流链路的是编码裸流帧，而非 MP4 文件。
- MP4 是另一条本地录制封装能力：`HJOHMuxer` 依据 `video/avc` / `video/hevc` 建立轨道并写入样本；它与 `HJRTMPMuxer` 是不同的 Muxer，`HJGraphPusher::openRecorder()` 也单独创建 `HJPluginFFMuxer` 用于录制。不能把“系统可以写 MP4”理解成“视频编码器只能产出 MP4”。

RTMP 前的实际转换步骤是：

```text
OH VideoEncoder
  → codec configuration（SPS/PPS 或 VPS/SPS/PPS）+ H.264/H.265 编码帧
  → HJFLVPacket::init()
      └─ HJESParser::proc_avc_data() / proc_hevc_data()
         解析、整理 NALU，识别关键帧和发送优先级
  → HJRTMPPacketManager::buildVideoHeader()
      └─ parse_avc_header() / parse_hevc_header() 生成 sequence header
  → HJFLVUtils::buildVideoTag()
      └─ 写入 FLV Video Tag 头、DTS、CTS（PTS-DTS）和视频负载
  → RTMP_SendPacket()
```

其中 AVCC（长度前缀 NALU）常出现在 MP4 样本和 FLV/RTMP 的 AVC 视频负载中，因此容易被误称为“MP4 格式”。它只是 H.264/H.265 NALU 的一种字节流组织方式；**MP4 文件的 `ftyp`、`moov`、轨道索引等容器结构并不会走进 RTMP 发送链路。** 项目也在 `HJPluginVideoOHEncoder::requireSEI()` 中明确以 `HJNALFormat::AVCC` 构造 SEI NAL，印证推流链路处理的是 NALU 格式，而非 MP4 文件。

面试可以这样回答：

> Harmony 硬编器给我的不是 FLV Tag，因此不能原样塞进 RTMP；它给出的是 H.264/H.265 编码帧和参数集。HJMedia 的 RTMP Muxer 会先把参数集转换成 AVC/HEVC sequence header，再把每个编码帧按 DTS/PTS 封成 FLV Video Tag，最后用 RTMP 发送。这里是编码裸流到 FLV/RTMP 的重封装，不是 MP4 文件转 FLV；MP4 封装是本地录制的另一条独立能力。

### 6. RTMP 连接、发送和“成功”的定义

`HJRTMPWrapper` 使用 `RTMP_Connect` 建立 RTMP 连接，再用 `RTMP_ConnectStream` 建立/发布媒体流。`HJRTMPMuxer` 接收交织后的帧，负责生成发送所需的 metadata、音视频 sequence header 和连续媒体消息，并交给异步网络发送部分。

项目通过 RTMP 通知向上层报告状态：

- `HJRTMP_EVENT_CONNECTED`：RTMP 连接建立；
- `HJRTMP_EVENT_STREAM_CONNECTED`：流连接/发布成功，对应上层 `HJ_PUSHER_NOTIFY_CONNECT_SUCCESS`；
- `HJRTMP_EVENT_DROP_FRAME`、`HJRTMP_EVENT_AUTOADJUST_BITRATE`：网络拥塞时的丢帧或自适应码率信号；
- `HJRTMP_EVENT_DISCONNECTED`、发送/接收错误：链路异常，需要重试或收尾。

因此，“推流成功”至少应指 RTMP 流建立成功并能够持续发送编码后的 A/V 数据；仅仅创建编码器或拿到第一帧并不等于成功。

## Graph、Plugin、线程和反压：加分点

`HJGraphPusher` 是场景编排者：创建插件、调用 `connectPlugins` 连接数据边、统一 `init/start/done` 生命周期，并向上层透出通知。Plugin 是具体工作单元，例如采集、重采样、AAC 编码、音视频交织和 RTMP 输出。

帧通过有界队列在插件间流转。消费者侧管理输入缓冲；当下游队列满时，上游不再继续投递，形成反压，避免网络或编码变慢时内存无限增长。音频采集还使用独立 `audioThread`，而 GPU 渲染、编码和网络发送也各有异步执行边界，因此不能把这条链路理解成一个同步函数调用栈。

## 收尾时可补的一句话

这套设计的核心不是“把相机数据编码后发出去”这么简单，而是把平台采集、GPU 零拷贝处理、音频格式适配、双路时间戳同步、网络拥塞反馈和异步生命周期拆成可组合的 Plugin，再由 `HJGraphPusher` 按推流场景组织起来，从而兼顾跨平台、低延迟和可扩展性。

## 代码定位

| 责任 | 主要位置 |
| --- | --- |
| 推流图组装 | `src/graphs/HJGraphPusher.cpp` |
| Harmony 推流入口与状态回调 | `src/entry/pusher/hsys/verify/HJNAPILiveStream.cpp` |
| RTMP 连接与建流 | `src/media/muxer/HJRTMPWrapper.cc` |
| RTMP 帧写入与统计 | `src/media/muxer/HJRTMPMuxer.cc` |
| Harmony 硬编码输出 ES / codec data | `src/media/codec/hsys/HJVEncOHCodec.cc` |
| A/V 按 DTS 交织 | `src/plugins/HJPluginAVInterleave.cpp` |
| H.264/H.265 到 FLV Tag 的组装 | `src/media/muxer/HJRTMPPacketManager.cc`、`src/media/muxer/flv/HJFLVUtils.cc` |
| Harmony MP4 本地封装（独立于 RTMP） | `src/media/muxer/hsys/HJOHMuxer.cc` |
| RTE 编码目标创建 | `src/comp/rte/HJRteGraphProcConfigSetup.cpp` |



图库中与音视频相关的内容