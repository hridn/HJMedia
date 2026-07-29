# HJMedia 主要链路：从浅到深讲解

## 一、先建立整体认识

HJMedia 是一个跨平台 C++ 音视频框架，主要覆盖以下业务：

- 推流：采集相机和麦克风数据，经过处理、编码、封装后发送到 RTMP 服务器；
- 直播播放：接收网络直播流，完成解复用、解码、同步和渲染；
- 点播播放：播放本地或网络媒体文件，支持暂停、恢复、Seek 和倍速；
- 音乐播放：解码音频文件、转换 PCM 格式并输出到音频设备；
- 音频混音：将多路 PCM 对齐、重采样并混合；
- 图像处理：在编码或显示前执行美颜、贴纸、缩放、人脸检测等 GPU 处理。

理解项目时，可以把它划分为五层：

```text
产品 API
  ↓
Graph 场景编排
  ↓
Plugin / Node 媒体处理
  ↓
线程、队列、时间戳与反压
  ↓
HarmonyOS / Android / iOS / Windows 等平台实现
```

项目最核心的思想是：

> 把一条复杂音视频链路拆成多个职责单一的 Plugin 或 Node，再由 Graph 根据具体业务场景将它们组装起来。

## 二、五条主要业务链路

### 1. RTMP 推流链路

推流由视频和音频两条支路组成，最后在 RTMP 输出前汇合：

```text
视频：
相机原始图像
  → RTE/GPU 图像处理
  → Harmony 视频硬编码器
  → H.264/H.265 ES
                         ┐
                         ├→ 按 DTS 交织 → FLV Tag → RTMP → 服务器
                         │
麦克风 PCM               │
  → 重采样/格式转换       │
  → AAC 编码 ────────────┘
```

#### 1.1 相机采集

从媒体概念上说，相机产生的是未压缩图像，常见像素格式包括 NV12、NV21 和 YUV420。但具体传递方式与平台相关：

- CPU 路径中，数据可能表现为一块可以直接访问的 YUV buffer；
- HarmonyOS 主路径中，画面主要通过相机 Surface 进入 GPU，以 texture/Surface 的形式流转。

因此，不能把 HarmonyOS 主链路简单描述成“CPU 得到一帧 YUV，再逐级复制”。更准确的说法是：相机原始图像通过 Surface 进入渲染链，尽量避免 CPU 和 GPU 之间的大量像素拷贝。

#### 1.2 GPU 图像处理

RTE 的 `SourceBridge` 接收相机画面，随后可以按业务需要插入：

- 旋转、镜像、裁剪和缩放；
- 美颜、贴纸和分屏；
- 人脸检测、超分和降噪；
- 预览输出和编码输出。

处理前后仍然是未压缩图像，只是数据载体从相机输入 Surface/texture，变成经过 GPU 处理后的输出 Surface/texture。

#### 1.2.1 为什么 Harmony 相机入口必须使用 OES 纹理

在当前项目的 HarmonyOS 相机链路中，输入纹理必须使用 OES，根本原因是：

> 相机画面属于平台外部缓冲区，由 `OH_NativeImage/NativeWindow` 管理，并不是 OpenGL 通过 `glTexImage2D()` 创建的普通纹理。`GL_TEXTURE_EXTERNAL_OES` 是 OpenGL ES 用来采样这类外部图像的接口。

实际链路为：

```text
Harmony 相机
  → NativeWindow/Surface
  → OH_NativeImage
  → GL_TEXTURE_EXTERNAL_OES
  → samplerExternalOES
  → OES Copy Shader
  → FBO 中的 GL_TEXTURE_2D
  → Blur/Faceu/Gray 等普通滤镜
  → 预览或编码器 Surface
```

##### 相机输出的是 Surface，而不是普通 OpenGL 纹理

`HJOGRenderWindowBridge::init()` 首先创建 OES 纹理，再用它创建 `OH_NativeImage`，最后从 `OH_NativeImage` 取得相机可以写入的 `NativeWindow`：

```cpp
uint32_t target = GL_TEXTURE_EXTERNAL_OES;
m_texture = HJOGCommon::textureCreate(target);
m_nativeImage = OH_NativeImage_Create(m_texture, target);
m_nativeWindow = OH_NativeImage_AcquireNativeWindow(m_nativeImage);
```

新帧到达后，项目调用：

```cpp
OH_NativeImage_UpdateSurfaceImage(m_nativeImage);
```

将最新的外部图像更新到 OES 纹理。这里的纹理是外部图像在 OpenGL 中的采样入口，图像内存仍由相机和系统图形缓冲区管理。

##### 为什么不能直接使用 `GL_TEXTURE_2D`

普通 `GL_TEXTURE_2D` 通常代表 OpenGL 自己分配和管理的纹理存储：

```text
CPU 像素数据
→ glTexImage2D/glTexSubImage2D
→ GL_TEXTURE_2D
```

如果强行把相机画面转换成普通 2D 纹理，通常需要：

```text
相机 Surface
→ 读回 YUV/CPU 内存
→ YUV 转 RGB
→ glTexImage2D 上传 GPU
```

这会增加 CPU/GPU 内存复制、YUV 转换、内存带宽、处理延迟和同步等待。OES 允许 GPU 直接采样相机外部缓冲区，避免这轮读回和上传，是直播低延迟链路的重要基础。

##### OES Shader 与普通 2D Shader 的区别

OES 纹理不能使用普通 `sampler2D`，项目专门启用外部纹理扩展并使用 `samplerExternalOES`：

```glsl
#extension GL_OES_EGL_image_external_essl3 : require

uniform samplerExternalOES sTexture;

void main()
{
    vec4 color = texture(sTexture, v_texcood);
    FragColor = vec4(color.rgb, color.a);
}
```

绑定纹理时也必须使用：

```cpp
glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId);
```

相机底层缓冲通常是 NV12、NV21 等 YUV 格式，而 Shader 采样 OES 时得到 `vec4` 颜色。系统图形栈和驱动可在外部纹理采样过程中完成底层格式适配，包括 YUV 到 RGB 的转换，因此项目上层不需要先把相机 YUV 读回 CPU 再上传 GPU。

##### 为什么入口使用 OES，后续又转换为 2D

OES 适合接入平台外部图像，但限制较多：

- 不能像普通纹理一样直接作为 FBO 的颜色附件；
- 很多已有滤镜使用 `sampler2D`；
- mipmap、wrap 等纹理能力受限制；
- 多级处理时，不适合每一级都实现一套 OES Shader。

因此项目只在输入端保留 OES，然后通过一次绘制将它转换成普通 2D FBO 纹理：

```text
OES 外部纹理
→ OES Copy Shader
→ FBO
→ GL_TEXTURE_2D
→ 美颜/模糊/灰度/贴纸
→ UI 预览或视频编码器
```

HarmonyOS 下 Graph 选择 `HJNodeClass_FilterCopyOES`，对应 `HJRteComDrawCopyOESFBO`；该组件使用 `HJOGBaseShaderType_Copy_OES` 读取外部纹理并把结果绘制到 FBO。转换完成后，后续组件可以统一处理普通 `GL_TEXTURE_2D`。

这里的“必须使用 OES”不是所有相机实现的绝对要求。更准确地说：

> 当前项目采用 `OH_NativeImage + NativeWindow + GPU` 接入 Harmony 相机，因此入口纹理必须与外部图像目标匹配，使用 `GL_TEXTURE_EXTERNAL_OES`。如果改成 CPU YUV 采集后手动上传纹理，则可以使用 `GL_TEXTURE_2D`，但需要付出额外复制和转换成本。

面试时可以概括为：

> Harmony 相机通过 NativeWindow/Surface 输出画面，缓冲区由系统图形栈管理，不是 OpenGL 自己创建的普通纹理。项目用 `OH_NativeImage` 将外部缓冲区绑定到 OES 纹理，让 GPU 直接采样相机画面，避免 YUV 读回 CPU和重新上传。由于 OES 不适合直接参与所有 FBO 和普通滤镜处理，项目在入口处用 OES Shader 把它绘制到 2D FBO，后续美颜、预览和编码链统一使用 `GL_TEXTURE_2D`。

#### 1.3 视频硬件编码

RTE 的 `TargetEncoder` 连接到 Harmony 视频编码器提供的输入 Surface。编码器内部配置包括：

- 输入像素格式：NV12；
- 分辨率；
- 帧率；
- CBR 码率；
- GOP 和 I 帧间隔；
- H.264 或 H.265 编码类型。

编码器将未压缩图像压缩为：

- H.264/H.265 编码帧；
- I 帧、P 帧等不同类型的 NALU；
- SPS/PPS，或 VPS/SPS/PPS 等 codec data。

这里需要严格区分编码格式和容器格式：

> Harmony 编码器输出的是 H.264/H.265 编码裸流 ES，不是完整的 MP4 文件，也不是 FLV Tag。

编码数据可能采用 AVCC 这种 MP4 中常见的 NALU 组织方式，但 AVCC 只是编码数据的字节组织形式，不等于 MP4 容器。完整 MP4 还需要 Muxer 写入 `ftyp`、`moov`、轨道和样本表等结构。

源码入口：

- `src/plugins/hsys/HJPluginVideoOHEncoder.cpp`
- `src/media/codec/hsys/HJVEncOHCodec.cc`

#### 1.4 音频采集与编码

音频链路为：

```text
麦克风 PCM
  → AudioOHCapturer
  → AudioResampler
  → FIFO
  → FDK AAC Encoder
  → AAC 编码帧
```

采集到的 PCM 不一定直接满足 AAC 编码器要求，因此要统一：

- 采样率；
- 声道数和声道布局；
- S16、float 等样本格式；
- packed/planar 数据布局；
- 每个 AAC 编码帧所需的样本数。

`HJPluginAudioResampler` 负责重采样和格式转换，FIFO 负责把零散 PCM 累积或拆分为 AAC 编码器需要的固定帧长，`HJPluginFDKAACEncoder` 再输出 AAC 帧。

#### 1.5 音视频交织

音频和视频在不同线程中并行编码，到达下游的顺序不稳定。

`HJPluginAVInterleave` 分别查看音频和视频队首帧的 DTS：

```text
audio DTS <= video DTS → 先输出音频
video DTS < audio DTS  → 先输出视频
```

它解决的是发送顺序问题，不负责重新编码。

#### 1.6 FLV/RTMP 封装

H.264/H.265 编码帧还不能作为完整的 RTMP 视频消息直接发送，项目会进行以下转换：

```text
H.264/H.265 ES
  → 解析 SPS/PPS、关键帧和 NALU
  → 生成 AVC/HEVC sequence header
  → 写入 FLV Video Tag
  → RTMP 消息
```

具体过程为：

1. `HJFLVPacket::init()` 接收视频编码帧；
2. `HJESParser` 整理 H.264/H.265 NALU，识别关键帧和优先级；
3. `HJRTMPPacketManager` 根据 codec data 生成 sequence header；
4. `HJFLVUtils` 写入 FLV Video Tag 头、DTS 和 CTS；
5. `HJRTMPWrapper` 通过 librtmp 建立连接并发送消息。

完整的视频格式变化可以概括为：

```text
相机原始图像
→ GPU texture/Surface
→ 编码器输入 NV12
→ H.264/H.265 ES + SPS/PPS
→ FLV Video Tag
→ RTMP 网络消息
```

`HJGraphPusher` 实际组装的核心链路是：

```text
AudioOHCapturer
  → AudioResampler
  → FDKAACEncoder ─────────┐
                           ├→ AVInterleave → RTMPMuxer
VideoOHEncoder ────────────┘
```

源码入口：`src/graphs/HJGraphPusher.cpp`。

### 2. 直播播放链路

直播播放基本是推流的逆过程：

```text
RTMP/HTTP-FLV 网络流
  → 解复用
  → H.264/H.265、AAC 编码包
  → 直播丢帧控制
  → 解码
  → 原始音视频
  → 渲染
```

实际 Graph 结构大致为：

```text
                              → Video Decoder → Video Render
FFDemuxer → AVDropping ───────┤
                              → Audio Decoder
                                → Audio Resampler
                                → SpeedControl
                                → Audio Render
```

#### 2.1 解复用

`HJPluginFFDemuxer` 从网络输入中解析出：

- H.264/H.265 视频编码包；
- AAC 等音频编码包；
- PTS、DTS 和 duration；
- codec parameters；
- SEI 等附加数据。

解复用只负责把容器或网络流拆成音视频编码包，不负责解码。

#### 2.2 直播丢帧

直播链路加入了 `HJPluginAVDropping`，这是直播与点播的重要区别。

直播更关注实时性。如果网络抖动或解码速度不足导致队列积压，系统不能一直播放旧帧，否则延迟会不断增加。因此直播链路可能：

- 优先丢弃非关键视频帧；
- 跳到新的关键帧重新解码；
- 协调音频和视频的丢帧；
- 将播放延迟重新拉回合理范围。

#### 2.3 解码与渲染

视频编码包经过 FFmpeg 软解或 Harmony 硬解后，变为可以显示的原始图像，再交给视频渲染器。

音频编码包经过解码后变成 PCM，再经过重采样和速度控制，最后交给平台音频设备播放。

源码入口：`src/graphs/HJGraphLivePlayer.cpp`。

### 3. 点播播放链路

点播链路为：

```text
本地文件/HTTP 文件
  → FFDemuxer
  → 视频解码 → 视频渲染
  → 音频解码 → 重采样 → 音频渲染
```

Graph 结构大致为：

```text
              → Video Decoder → Video Render
FFDemuxer ────┤
              → Audio Decoder → Resampler → Audio Render
```

点播通常不需要直播式的追帧丢帧，更关注：

- 暂停和恢复；
- Seek；
- 倍速；
- EOF 和尾帧排空；
- 音视频同步；
- 播放进度。

#### Seek 为什么复杂

Seek 不是简单修改一个时间变量，而是跨线程切换整个时间轴：

```text
暂停音视频输出
→ 渲染器进入 preFlush
→ 清空旧帧队列
→ flush 解码器
→ Demuxer 定位到目标关键帧
→ 从新位置重新解码
→ 恢复渲染
```

如果没有先阻止渲染器继续输出，Seek 期间可能出现旧画面闪回、旧 PCM 残留或短暂音画不同步。

源码入口：`src/graphs/HJGraphVodPlayer.cpp`。

### 4. 音乐播放器链路

音乐播放器是理解 Graph 模型较简单的入口：

```text
音频文件
  → FFDemuxer
  → AudioFFDecoder
  → AudioResampler
  → 平台 AudioRender
```

数据格式变化为：

```text
MP3/AAC/FLAC 等媒体文件
→ 压缩音频包
→ 解码 PCM
→ 设备要求的 PCM
→ 声卡播放
```

根据平台不同，最终选择不同渲染器：

- HarmonyOS：`HJPluginAudioOHRender`；
- Windows：`HJPluginAudioWASRender`；
- iOS：`HJPluginAudioIOSRender`；
- Android：`HJPluginAudioAARender`。

源码入口：`src/graphs/HJGraphMusicPlayer.cpp`。

### 5. 音频混音链路

音频混音允许多路 PCM 同时输入：

```text
输入 1 PCM → Resampler ─┐
输入 2 PCM → Resampler ─┼→ AudioMixer → PCM 输出
输入 3 PCM → Resampler ─┘              → 可选 AAC 编码
```

混音前必须统一每一路的：

- 采样率；
- 声道数；
- 样本格式；
- 每帧样本数；
- 时间戳。

混音器还要处理输入暂时无数据、起始时间不同、增益、饱和裁剪，以及输入动态增加、删除和结束等问题。

源码入口：`src/graphs/HJGraphAudioMixer.cpp`。

## 三、Graph 与 Plugin 的职责

### 1. Graph 是业务编排者

Graph 不直接执行编解码算法，主要负责：

- 创建 Plugin；
- 连接上下游；
- 传递初始化参数；
- 分配和管理线程；
- 管理 `init/start/stop/done` 生命周期；
- 处理事件和错误回调；
- 执行 Seek、静音和调整码率等控制操作。

例如 `HJGraphPusher` 知道推流需要音频采集、重采样、AAC 编码、视频编码、音视频交织和 RTMP 输出，但真正执行 AAC 编码的是 `HJPluginFDKAACEncoder`。

### 2. Plugin 是具体处理单元

每个 Plugin 尽量只负责一种工作：

| Plugin | 职责 |
| --- | --- |
| `HJPluginFFDemuxer` | 解复用 |
| `HJPluginVideoOHEncoder` | Harmony 视频硬编码 |
| `HJPluginVideoOHDecoder` | Harmony 视频硬解码 |
| `HJPluginAudioResampler` | PCM 格式转换 |
| `HJPluginFDKAACEncoder` | AAC 编码 |
| `HJPluginAVInterleave` | 音视频交织 |
| `HJPluginVideoRender` | 视频渲染 |
| `HJPluginRTMPMuxer` | FLV/RTMP 封装和发送 |

可以把 Graph 理解为导演，把 Plugin 理解为演员。

## 四、媒体数据如何在组件之间流动

### 1. 消费者管理输入队列

当上游 A 连接下游 B 时，帧缓冲由消费者 B 管理：

```text
生产者 A → [B 的输入队列] → 消费者 B
```

这样设计是因为消费者最了解自己的处理速度和缓冲需求。

队列容量通常按时间窗口估算：

```text
视频容量 ≈ 帧率 × 缓冲秒数
音频容量 ≈ 采样率 × 缓冲秒数 ÷ 每帧样本数
```

例如 30 fps、2 秒缓冲，视频队列容量约为 60 帧。

### 2. 反压

如果下游 B 的输入队列已满，上游 A 暂停投递：

```text
B 处理变慢
→ B 的队列逐渐变满
→ A 发现 B 已满
→ A 暂停生产
→ B 消费一帧并腾出空间
→ 唤醒 A 继续生产
```

反压可以避免：

- 内存无限增长；
- 延迟无限增加；
- 上游无意义地持续生产；
- 慢节点拖垮整个进程。

## 五、推拉驱动和异步调度

HJMedia 不是让一帧数据通过一个同步函数调用到底，而是让多个节点在各自的异步执行环境中工作。

### 1. `HJNODE_DRIVE_NEXT`

当前 Node 成功向下游投递一帧后，继续调度自己生产下一帧。

适合主动生产或中间处理节点，例如解复用器、编码器和解码器。

### 2. `HJNODE_DRIVE_PRE`

当前 Node 消费输入帧后，通知前驱节点：下游已经腾出空间，可以继续生产。

适合音频渲染器、视频渲染器等终端消费者。

中间节点通常同时具有两种驱动能力：

```text
消费输入 → 唤醒上游
产生输出 → 继续调度自己
```

### 3. `m_isBusy`

同一个 Node 可能被多个事件同时唤醒：

- 上游送来新帧；
- 下游腾出空间；
- 定时器到期；
- 生命周期操作；
- Seek 或 flush。

`m_isBusy` 用于合并重复调度，防止同一个 `proRun()` 同时或重复执行。

仅给 `proRun()` 加互斥锁并不等价：互斥锁只能把重复任务串行执行，不能阻止大量重复任务进入任务队列。

## 六、时间戳是整个系统的主线

音视频帧除了包含数据 buffer，还必须携带正确的时间戳。

### 1. PTS 与 DTS

- DTS：编码包应该何时进入解码器；
- PTS：解码后的画面或声音应该何时呈现；
- `PTS - DTS`：视频重排序产生的 composition time offset。

推流时：

```text
AVInterleave 按 DTS 排列发送顺序
FLV Video Tag 写入 DTS 和 CTS（PTS-DTS）
```

播放时：

```text
按 DTS 解码
按 PTS 渲染
```

### 2. 为什么通常以音频为主时钟

音频设备按照固定采样率持续消费 PCM。随意暂停或丢弃音频容易造成爆音、卡顿和声音不连续，因此通常使用音频播放进度作为主时钟：

- 视频 PTS 落后太多：丢弃视频帧追赶音频；
- 视频 PTS 领先：延迟显示；
- 差值位于容许阈值内：正常渲染。

## 七、EOF、Flush 与生命周期

EOF 不代表整条链路可以立即销毁。

例如播放链路中：

```text
Demuxer 读到 EOF
→ Decoder 内部可能仍有重排序帧
→ 节点队列中可能还有待播放帧
→ Render 还没有消费完成
```

正确收尾过程是：

```text
上游发送 EOF
→ Decoder flush 内部尾帧
→ 输出剩余媒体数据
→ 再向下游传播 EOF
→ Render 消费完剩余队列
→ 所有分支完成
→ Graph 释放资源
```

Plugin 生命周期可以概括为：

```text
init：创建并配置资源
start：启动设备和工作线程
process：持续处理媒体数据
stop：停止运行，但可能保留可复用资源
release/done：等待异步任务结束并最终释放资源
```

`done()` 通常要同步等待相关异步任务完成，否则可能发生工作线程访问已释放对象的问题。

## 八、面试总结

可以用下面这段话概括项目：

> HJMedia 是一个以 Graph 为场景编排、以 Plugin/Node 为处理单元、以媒体帧和时间戳为数据载体，并通过异步调度、有界队列和反压机制驱动的跨平台音视频框架。它将采集、GPU 处理、编解码、音视频同步、渲染、封装和网络传输组合成推流、直播、点播、音乐播放及混音等产品链路。

面试准备时，建议优先掌握三条主线：

1. `HJGraphPusher`：采集、GPU 处理、编码、交织和 FLV/RTMP 输出；
2. `HJGraphLivePlayer`：解复用、直播丢帧、解码、同步和渲染；
3. `HJMediaNode/HJPlugin`：输入队列、反压、推拉驱动和异步调度。

## 九、核心源码导航

| 内容 | 路径 |
| --- | --- |
| 推流 Graph | `src/graphs/HJGraphPusher.cpp` |
| 直播播放 Graph | `src/graphs/HJGraphLivePlayer.cpp` |
| 点播播放 Graph | `src/graphs/HJGraphVodPlayer.cpp` |
| 音乐播放器 Graph | `src/graphs/HJGraphMusicPlayer.cpp` |
| 音频混音 Graph | `src/graphs/HJGraphAudioMixer.cpp` |
| Harmony 视频硬编码 | `src/media/codec/hsys/HJVEncOHCodec.cc` |
| Harmony 相机 Surface/OES 桥接 | `src/comp/graphic/hsys/HJOGRenderWindowBridge.cpp` |
| OES Shader 与纹理绑定 | `src/comp/graphic/HJOGShaderCommon.cpp`、`src/comp/graphic/HJOGCopyShaderStrip.cpp` |
| OES 转 2D FBO | `src/comp/rte/HJRteComDraw.cpp`、`src/comp/rte/HJRteGraphSetupInfo.cpp` |
| 音视频交织 | `src/plugins/HJPluginAVInterleave.cpp` |
| RTMP 帧管理 | `src/media/muxer/HJRTMPPacketManager.cc` |
| FLV Tag 组装 | `src/media/muxer/flv/HJFLVUtils.cc` |
| RTMP 连接与发送 | `src/media/muxer/HJRTMPWrapper.cc` |
| Node 队列和驱动模型 | `src/core/HJMediaNode.h` |
