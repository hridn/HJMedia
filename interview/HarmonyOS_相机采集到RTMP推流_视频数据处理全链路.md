# HarmonyOS：相机采集到 RTMP 推流的视频数据处理全链路

## 1. 文档范围

本文只说明 HJMedia 在 HarmonyOS 平台上的**视频链路**，从相机产生一帧画面开始，一直追踪到该帧被封装成 FLV Video Tag 并通过 RTMP 发送。

音频链路只在音视频交织处简要提及。其独立流程为：

```text
麦克风 PCM
→ AudioOHCapturer
→ AudioResampler/FIFO
→ FDK AAC Encoder
→ AAC 编码帧
→ 与视频按 DTS 交织
```

视频主链路可以先记成：

```text
相机平台缓冲区
→ NativeWindow/Surface
→ OH_NativeImage
→ GL_TEXTURE_EXTERNAL_OES
→ OES Shader
→ RGBA Texture 2D/FBO
→ 2D GPU 滤镜链
→ 编码器 EGLSurface/NativeWindow
→ H.264/H.265 ES
→ AVCC 长度前缀 NALU
→ FLV Video Tag
→ RTMP
```

这里最重要的三个边界是：

1. **相机到 GPU**：外部 Surface 缓冲区以 OES 纹理形式被 GPU 采样；
2. **GPU 到硬编码器**：Texture 2D 被绘制到编码器的 NativeWindow/EGLSurface；
3. **编码器到网络**：H.264/H.265 编码裸流被重封装为 FLV Tag，再通过 RTMP 发送。

## 2. 每个阶段的数据格式总表

| 阶段 | 输入数据形态 | 核心处理 | 输出数据形态 |
| --- | --- | --- | --- |
| 相机采集 | 传感器原始数据 | ISP、曝光、白平衡、缩放等平台处理 | 相机 Surface 中的未压缩图像，通常是平台 YUV 缓冲 |
| NativeImage 桥接 | NativeWindow/Surface buffer | 将平台外部缓冲关联到 OpenGL 外部纹理 | `GL_TEXTURE_EXTERNAL_OES` + transform matrix |
| OES → 2D | OES 外部纹理 | `samplerExternalOES` 采样并绘制到 FBO | `GL_TEXTURE_2D`，仓库中显式创建为 RGBA8 |
| GPU 处理 | RGBA Texture 2D | 镜像、旋转、裁剪、缩放、美颜、Blur、Faceu 等 | 处理后的 RGBA Texture 2D/FBO |
| 编码器输入 | Texture 2D | 绘制到编码器 EGLSurface，`eglSwapBuffers` 提交 | 编码器 NativeWindow 中的图像；编码器配置期望 NV12 |
| 视频编码 | 编码器 Surface 图像 | Harmony `OH_VideoEncoder` 硬编码 | H.264/H.265 ES、codec data、关键帧/普通帧 |
| ES 整理 | Annex B 或长度前缀 NALU | 提取/转换参数集，NALU 统一为 4 字节长度前缀 | AVCC 风格 H.264/H.265 NALU |
| A/V 交织 | H.264/H.265 + AAC | 比较音视频 DTS | 按时间顺序排列的编码帧 |
| FLV 封装 | 编码帧、SPS/PPS、PTS/DTS | 生成 sequence header、FLV Video Tag | FLV Tag 字节流 |
| RTMP 发送 | FLV metadata/header/tag | 建连、建流、异步写网络 | RTMP chunk/message 到流媒体服务器 |

需要注意：“平台 YUV”“OES”“RGBA Texture 2D”“H.264/H.265”和“FLV”不是同一层的概念：

- YUV、RGBA：未压缩像素格式；
- OES、Texture 2D：GPU 纹理目标和数据载体；
- H.264/H.265：视频压缩编码格式；
- AVCC/Annex B：NALU 的字节组织方式；
- FLV：音视频消息封装格式；
- RTMP：网络传输协议。

## 3. 初始化阶段：先建立两条 Surface 通道

每帧数据开始流动之前，项目先建立相机输入通道和编码器输出通道。

### 3.1 创建 RTE 图并取得相机 Surface ID

应用调用 `openPreview()` 后，`HJNAPILiveStream` 初始化 RTE 渲染图，默认占位图在 HarmonyOS 上大致为：

```text
SourceBridge
→ FilterCopyOES
→ CustomSourceFilter（可选）
→ Blur（可选）
→ UI Target（可选）
→ Encoder Target（推流开启后启用）
```

`HJRteComSourceBridge::renderWindowBridgeAcquire()` 创建 `HJOGRenderWindowBridge`。Bridge 内部创建 OES 纹理和 `OH_NativeImage`，再通过 `OH_NativeImage_GetSurfaceId()` 把 Surface ID 返回到 ArkTS。

Harmony 示例把这个 Surface ID 交给相机服务：

```text
HJPusher.openPreview()
→ 得到 previewSurfaceId
→ cameraService.bindSurfaceId(previewSurfaceId)
→ CameraManager.createPreviewOutput(profile, surfaceId)
→ 相机开始向该 Surface 生产画面
```

因此，相机并不是把一块 CPU YUV buffer 回调给 `HJGraphPusher`，而是直接成为 NativeWindow 缓冲队列的生产者。

相关代码：

- `examples/harmony/entry/src/main/ets/pusher/store/PusherPreviewStore.ets`
- `examples/harmony/entry/src/main/ets/camera/service/CameraService.ets`
- `src/entry/pusher/hsys/verify/HJNAPILiveStream.cpp`
- `src/comp/rte/HJRteComSourceBridge.cpp`

### 3.2 创建视频硬编码器的输入 Surface

应用调用 `openPusher()` 后，`HJGraphPusher` 创建：

```text
HJPluginVideoOHEncoder
→ HJVEncOHCodec
→ OH_VideoEncoder
```

编码器根据 `HJVideoInfo` 配置：

```cpp
OH_MD_KEY_WIDTH
OH_MD_KEY_HEIGHT
OH_MD_KEY_FRAME_RATE
OH_MD_KEY_PIXEL_FORMAT = AV_PIXEL_FORMAT_NV12
OH_MD_KEY_VIDEO_ENCODE_BITRATE_MODE = BITRATE_MODE_CBR
OH_MD_KEY_BITRATE
OH_MD_KEY_I_FRAME_INTERVAL
```

编码类型根据参数选择：

```text
video/avc  → H.264
video/hevc → H.265
```

随后通过 `OH_VideoEncoder_GetSurface()` 取得编码器的 `NativeWindow`。`HJPluginVideoOHEncoder` 再通过 `surfaceCb` 将该 NativeWindow 注册到 RTE 的 `TargetEncoder`。

RTE 为这个 NativeWindow 创建 EGL window surface：

```text
Encoder NativeWindow
→ EGLSurfaceCreate(window)
→ HJOGEGLSurfaceType_EncoderPusher
→ 绑定到 HJRteComDrawEGLEncoder
```

至此形成两端：

```text
相机是输入 Surface 的生产者
RTE 是相机 Surface 的消费者

RTE 是编码器 Surface 的生产者
OH_VideoEncoder 是编码器 Surface 的消费者
```

相关代码：

- `src/graphs/HJGraphPusher.cpp`
- `src/plugins/hsys/HJPluginVideoOHEncoder.cpp`
- `src/media/codec/hsys/HJVEncOHCodec.cc`
- `src/comp/graphic/hsys/HJOGRenderEnv.cpp`
- `src/comp/rte/HJRteGraphProc.cpp`

## 4. 第一段每帧数据流：相机 Surface → OES 纹理

### 4.1 创建外部纹理和 NativeImage

`HJOGRenderWindowBridge::init()` 明确以 OES target 创建纹理：

```cpp
uint32_t target = GL_TEXTURE_EXTERNAL_OES;
m_texture = HJOGCommon::textureCreate(target);
m_nativeImage = OH_NativeImage_Create(m_texture, target);
m_nativeWindow = OH_NativeImage_AcquireNativeWindow(m_nativeImage);
```

其含义是：

- `m_nativeWindow` 提供给相机写画面；
- `m_nativeImage` 连接系统 BufferQueue 和 OpenGL；
- `m_texture` 是 GPU 访问最新相机缓冲区的外部纹理入口；
- 像素内存的所有权仍在平台 Surface/图形系统中，并非由 `glTexImage2D()` 分配。

### 4.2 新帧回调只做“可用”通知

项目注册 `OH_OnFrameAvailableListener`。相机提交一帧后，`priOnFrameAvailable()` 调用 `priSetAvailable()`：

```text
相机提交新 buffer
→ OnFrameAvailable
→ Bridge 状态变为 Available
→ 可选 manualDrive 回调唤醒渲染图
```

回调中不会复制整帧像素，也不做复杂 Shader 运算，避免阻塞相机生产线程。

### 4.3 在渲染线程更新外部图像

RTE 的 Source 更新阶段调用：

```cpp
OH_NativeImage_UpdateSurfaceImage(m_nativeImage);
```

它将最新可用 Surface buffer 关联到 OES 纹理。成功后还会读取：

```cpp
OH_NativeImage_GetTimestamp(m_nativeImage);
OH_NativeImage_GetTransformMatrixV2(m_nativeImage, m_matrix);
OH_NativeWindow_NativeWindowHandleOpt(..., GET_BUFFER_GEOMETRY, ...);
```

得到的数据包括：

- OES texture ID；
- 相机图像宽高；
- Surface 变换矩阵；
- 相机帧 timestamp。

其中 transform matrix 很重要，因为相机外部缓冲区可能存在上下翻转、裁剪或平台坐标系变换。它会在 OES Shader 绘制时作为纹理矩阵使用。

当前实现虽然读取了 `OH_NativeImage_GetTimestamp()`，但没有把这个值继续写入视频编码帧；编码时间戳在硬编码输出回调处重新生成，后文会单独说明。

### 4.4 为什么这里必须是 OES

普通 `GL_TEXTURE_2D` 的典型路径是：

```text
CPU 像素内存
→ glTexImage2D/glTexSubImage2D
→ OpenGL 自己管理的 Texture 2D
```

而相机输出的是系统拥有的外部 Surface buffer。OES 的用途正是让 Shader 直接采样这种外部图像，避免：

```text
相机 YUV
→ 读回 CPU
→ CPU 或 Shader 前置转换
→ 再上传 Texture 2D
```

所以“必须使用 OES”是当前 `OH_NativeImage + NativeWindow` 接入方式的要求，并非所有相机实现的绝对要求。如果改成 CPU YUV 回调后手动上传，入口也可以使用 Texture 2D，但会付出额外复制、带宽和延迟。

### 4.5 本阶段输入和输出

```text
输入：相机 NativeWindow 中的平台图像缓冲，通常为 YUV 类原始图像
处理：BufferQueue 更新、外部图像绑定、读取 transform matrix
输出：GL_TEXTURE_EXTERNAL_OES + 宽高 + 纹理变换矩阵
```

这里可以称为“外部缓冲直接采样”或“避免 CPU 回读”，但不能说整条 GPU 链完全零拷贝，因为下一步 OES → Texture 2D 是一次 GPU 绘制。

## 5. 核心转换：OES → Texture 2D/FBO

### 5.1 为什么必须做这次转换

OES 非常适合接入相机，但不适合作为整条滤镜链的通用中间格式：

- OES 使用 `samplerExternalOES`，普通滤镜普遍使用 `sampler2D`；
- OES 纹理不能像普通 Texture 2D 那样作为 FBO 颜色附件；
- mipmap、wrap、离屏多级处理等能力受到限制；
- 如果每个滤镜都实现 OES 版本，Shader 数量和维护成本会大幅增加；
- 美颜、Blur、多通道合成等处理通常需要反复读写 FBO。

因此，本项目把 OES 仅作为输入边界，第一步就转换成统一的 Texture 2D。

### 5.2 RTE 图如何选择 OES 转换节点

`HJRteGraphConfigConstructor::priConstructPlaceHolder()` 中有平台分支：

```cpp
#if defined(HarmonyOS)
video2DName = HJNodeClass_FilterCopyOES;
#else
video2DName = HJNodeClass_FilterCopy2D;
#endif
```

HarmonyOS 对应组件为 `HJRteComDrawCopyOESFBO`，初始化时创建 `HJOGBaseShaderType_Copy_OES`。

### 5.3 输入：OES DriftInfo

Source 准备好后，RTE 创建 `HJRteDriftInfo`：

```text
textureId   = OES texture ID
textureType = HJRteTextureType_OES
srcWidth    = 相机图像宽度
srcHeight   = 相机图像高度
textureMat  = NativeImage transform matrix
```

`HJRteDriftInfo` 是 RTE 内部描述“一份当前可用 GPU 图像”的轻量对象，它传递的是纹理引用和图像元数据，而不是复制像素。

### 5.4 输出 FBO 的实际格式

`HJOGFBOCtrl::init()` 创建普通 2D 纹理：

```cpp
glBindTexture(GL_TEXTURE_2D, m_texture);
glTexImage2D(
    GL_TEXTURE_2D,
    0,
    GL_RGBA,
    width,
    height,
    0,
    GL_RGBA,
    GL_UNSIGNED_BYTE,
    nullptr);
glFramebufferTexture2D(
    GL_FRAMEBUFFER,
    GL_COLOR_ATTACHMENT0,
    GL_TEXTURE_2D,
    m_texture,
    0);
```

所以这一步的明确输出是：

```text
GL_TEXTURE_2D
内部/外部格式：GL_RGBA
数据类型：GL_UNSIGNED_BYTE
可理解为 RGBA8 FBO 纹理
```

### 5.5 Shader 如何完成转换

OES Copy Shader 使用：

```glsl
#extension GL_OES_EGL_image_external_essl3 : require
uniform samplerExternalOES sTexture;

void main()
{
    vec4 color = texture(sTexture, v_texcood);
    FragColor = vec4(color.rgb, color.a);
}
```

绘制过程为：

```text
绑定 RGBA FBO
→ glBindTexture(GL_TEXTURE_EXTERNAL_OES, cameraTexture)
→ 设置 NativeImage transform matrix
→ 绘制全屏 triangle strip
→ fragment shader 采样外部纹理
→ RGBA 结果写入 FBO 的 Texture 2D
→ 解绑 FBO
```

相机底层通常是 YUV buffer，但 Shader 得到的是 `vec4` 颜色。YUV 到可采样颜色的格式适配由系统图形栈、外部纹理扩展和驱动完成，仓库中没有先把 YUV 读回 CPU 再手写转换。

### 5.6 FBO 所有权与复用

RTE 从 FBO Pool 获取临时 FBO。完成转换后，将 FBO 的所有权附着到 `HJRteDriftInfo` 中的 `HJFboLease`：

```text
FBO Pool acquire
→ Shader 写入 Texture 2D
→ takeFbo()
→ HJFboLease 持有
→ DriftInfo 向下游传递
→ DriftInfo 生命周期结束
→ FBO 自动归还 Pool
```

这样可以避免每帧反复 `glGenTexture/glDeleteTexture`，也能保证下游仍在使用纹理时，FBO 不会提前被复用。

### 5.7 本阶段输入和输出

```text
输入：GL_TEXTURE_EXTERNAL_OES + transform matrix
处理：samplerExternalOES 采样、坐标变换、GPU 全屏绘制
输出：RGBA8 GL_TEXTURE_2D/FBO
```

这不是 CPU 格式转换，而是一次纯 GPU render-to-texture。

## 6. Texture 2D 后续 GPU 处理

OES 转成 2D 后，后续链路统一处理普通 Texture 2D。默认图中的典型顺序是：

```text
RGBA Texture 2D
→ CustomSourceFilter（可选）
→ Blur/Gray/Faceu/贴纸/分屏等（按配置启用）
→ 最终 RGBA Texture 2D
```

每个 FBO Filter 的通用工作方式是：

```text
上游 DriftInfo（Texture 2D）
→ 从 FBO Pool 申请目标 FBO
→ 绑定目标 FBO
→ 使用 sampler2D Shader 采样上游纹理
→ 写入新的 RGBA Texture 2D
→ 生成新的 DriftInfo
→ 继续传给下游
```

被禁用的简单 Filter 可以直接透传上游 `DriftInfo`，避免无意义绘制。需要多 Pass 的 Blur 等滤镜则会使用多个中间 FBO。

同一份处理结果可以分叉到多个目标：

```text
最终 Texture 2D
├→ UI EGLSurface：预览
├→ Encoder EGLSurface：推流编码
├→ PBO：CPU 读回/检测（可选）
└→ ImageReceiver：平台图像输出（可选）
```

## 7. Texture 2D → 视频硬编码器 Surface

### 7.1 Encoder Target 的输入

`HJRteComDrawEGLEncoder` 是 RTE 的编码目标。其输入为前一级处理后的：

```text
textureId   = 最终 GL_TEXTURE_2D
textureType = HJRteTextureType_2D
width/height
textureMat
```

连接 Encoder Target 的 Link 使用普通 `Copy2D` Shader。

### 7.2 绘制到编码器 EGLSurface

Encoder Target 每帧执行：

1. `makeCurrent(encoderEGLSurface)`；
2. 清空目标 Surface；
3. 根据裁剪、镜像和目标分辨率设置 `glViewport()`；
4. 用 `sampler2D` 读取最终 Texture 2D；
5. 把结果绘制到编码器 EGLSurface；
6. 调用 `eglSwapBuffers()` 提交给编码器的 NativeWindow 缓冲队列。

数据关系为：

```text
RGBA Texture 2D
→ OpenGL Copy2D Shader
→ Encoder EGLSurface back buffer
→ eglSwapBuffers
→ Encoder NativeWindow buffer queue
→ OH_VideoEncoder 消费
```

### 7.3 RGBA 与 NV12 的边界怎么理解

RTE 的 FBO 明确是 RGBA Texture 2D，而编码器配置中明确写入：

```cpp
OH_MD_KEY_PIXEL_FORMAT = AV_PIXEL_FORMAT_NV12;
```

但仓库中没有一段 CPU 代码执行“RGBA buffer → NV12 buffer”。从框架代码能看到的是：

```text
OpenGL 将颜色绘制到编码器 NativeWindow 对应的 EGLSurface
OH_VideoEncoder 按 NV12 输入能力进行配置
```

因此，图形缓冲格式协商以及必要的 RGB/YUV 适配发生在 Harmony 的 EGL、NativeWindow、图形驱动和 Codec Surface 边界，而不是 HJMedia 在 CPU 上逐像素转换。面试中不要描述成“项目调用某个 C++ RGB2NV12 函数”，因为当前链路没有这段实现。

### 7.4 这一段的性能意义

像素数据始终留在 GPU/系统 Surface 通道中：

```text
Camera Surface → OES → GPU FBO → Encoder Surface
```

CPU 只参与状态控制、纹理 ID/矩阵传递和编码后小体积码流处理，不搬运每一帧原始像素。这显著降低了内存带宽和推流延迟。

## 8. Harmony 硬编码器：Surface 图像 → H.264/H.265 ES

### 8.1 编码器输出回调

当硬编码器产生数据时，`OnNewOutputBuffer` 将 `OH_AVBuffer` 和索引放入 `m_outputQueue`，并调用 `newBufferCb` 唤醒 `HJPluginVideoOHEncoder::runTask()`。

Plugin 随后调用 `m_codec->getFrame()` 取出编码结果。

### 8.2 三类输出

`HJVEncOHCodec::getFrame()` 根据 `OH_AVCodecBufferAttr::flags` 分三类处理。

#### Codec data

```cpp
AVCODEC_BUFFER_FLAGS_CODEC_DATA
```

这类 buffer 保存编码器配置数据，例如：

- H.264：SPS/PPS；
- H.265：VPS/SPS/PPS。

项目将它保存到 `m_headerBuf`，并创建 `AVCodecParameters/extradata`，供后续生成 FLV video sequence header。

#### 普通编码帧

普通输出被包装为 `HJMediaFrame + AVPacket`：

```cpp
info->m_dataType = HJDATA_TYPE_ES;
```

因此此时的数据是 H.264/H.265 Elementary Stream，不是 MP4 文件，也不是 FLV Tag。

#### EOS

```cpp
AVCODEC_BUFFER_FLAGS_EOS
```

项目生成视频 EOF frame，通知下游进入收尾流程。

### 8.3 关键帧处理

如果编码器标记：

```cpp
AVCODEC_BUFFER_FLAGS_SYNC_FRAME
```

项目会把 `m_headerBuf` 和当前关键帧数据拼接，并在该帧的 `HJVideoInfo` 中设置 codec parameters。这样 RTMP Muxer 收到关键帧时可以取得参数集并建立视频头。

数据形态是：

```text
关键帧：codec data + IDR/IRAP NALU
普通帧：P/B 等 VCL NALU
```

### 8.4 当前时间戳行为

`priOnNewOutputBuffer()` 中使用：

```cpp
m_timestamp = HJCurrentSteadyMS();
```

随后创建帧时：

```text
PTS = timestamp
DTS = timestamp
timeBase = 毫秒
extraTS = timestamp
```

也就是说，当前 Harmony 推流实现使用“编码输出回调到达时的 steady clock”作为视频 PTS/DTS，没有直接沿用前面读取到的相机 timestamp；同时令 `PTS == DTS`，对应当前链路不表达 B 帧重排序时间差。

这是理解延迟统计的重要细节：`extraTS` 后续用于估算从编码输出到网络 Tag 发送的延迟，而不是完整的“传感器曝光到服务器”的端到端延迟。

### 8.5 可选 SEI

`HJPluginVideoOHEncoder::requireSEI()` 可以查询业务 SEI，并以 AVCC NAL 格式构造 H.264/H.265 SEI，再附加到 `HJMediaFrame`。RTMP 包装阶段会将 SEI 插入对应视频帧。

## 9. Video Plugin → AVInterleave

`HJPluginVideoOHEncoder::runTask()` 取到一个编码帧后执行：

```text
getFrame()
→ 可选 requireSEI()
→ deliverToOutputs()
→ HJPluginAVInterleave 的视频输入队列
```

`HJGraphPusher` 的视频连接为：

```cpp
connectPlugins(m_videoEncoder, m_avInterleave, HJMEDIA_TYPE_VIDEO);
```

音频编码链同时连接到 `m_avInterleave`：

```text
AudioOHCapturer
→ AudioResampler
→ FDKAACEncoder
→ AVInterleave.audioInput
```

### 9.1 为什么需要交织

音频和视频由不同设备、线程和编码器产生，回调先后顺序不代表媒体时间顺序。`HJPluginAVInterleave` 预览两路队首帧：

```text
audio DTS <= video DTS → 先取音频
video DTS < audio DTS  → 先取视频
```

输出仍然是独立的 AAC 或 H.264/H.265 编码帧，只是被排列成适合单路 Muxer 消费的时间顺序。

## 10. H.264/H.265 ES → 统一长度前缀 NALU

编码器输出的 NALU 可能有两种组织形式。

### 10.1 Annex B

使用 start code 分隔：

```text
00 00 00 01 [NALU]
00 00 00 01 [NALU]
```

### 10.2 AVCC/HVCC 风格

使用 4 字节长度前缀：

```text
[4-byte NAL length][NALU]
[4-byte NAL length][NALU]
```

`HJESParser::proc_avc_data()` 和 `proc_hevc_data()` 会先检查 start code：

- 如果是 Annex B，则遍历 NALU，将 start code 改写成 4 字节 big-endian 长度；
- 如果已经是长度前缀，则直接解析并保留；
- 同时根据 NAL type 判断关键帧，并计算包的发送优先级。

所以进入 FLV 封装前，视频负载被统一成 RTMP/FLV 所需要的长度前缀 NALU。

这里仍然不是“MP4 转 FLV”：AVCC 是 H.264 NALU 的组织方式，MP4 是包含 `ftyp/moov/mdat` 等结构的完整容器。当前推流链没有先生成 MP4 文件。

## 11. 编码帧 → HJFLVPacket

`HJRTMPMuxer::addRTMPPacket()` 创建 `HJFLVPacket`。视频 packet 记录：

```text
codec ID
key-frame 标记
PTS
DTS
extraTS
track index
NALU 数据
发送优先级
```

时间戳会减去起始 offset，使 RTMP 流从接近 0 的时间开始。

如果帧携带 SEI，`HJFLVPacket::init()` 会先把 SEI NAL 插入视频 NALU，再执行 H.264/H.265 解析和格式归一化。

## 12. FLV/RTMP 发送顺序

`HJRTMPPacketManager` 不会一收到任意视频帧就立即发送，而是按照协议初始化顺序输出。

### 12.1 首次发送顺序

```text
1. FLV metadata
2. AAC AudioSpecificConfig / audio sequence header（有音频时）
3. AVCDecoderConfigurationRecord 或 HEVC configuration / video sequence header
4. 等待第一个视频关键帧
5. 连续音视频媒体 Tag
```

在拿到首个关键帧前，PacketManager 会丢弃不适合起播的包。这样服务器或播放器从关键帧开始就具备正确的解码参数。

### 12.2 视频 sequence header

`buildVideoHeader()` 从 `AVCodecParameters::extradata` 取得参数集：

```text
H.264 SPS/PPS
→ parse_avc_header()
→ AVCDecoderConfigurationRecord

H.265 VPS/SPS/PPS
→ parse_hevc_header()
→ HEVCDecoderConfigurationRecord
```

随后再把 configuration record 写进 FLV/Enhanced RTMP 的 sequence start 包。

### 12.3 经典 FLV Video Tag

`HJFLVUtils::buildVideoTag()` 为每帧写入：

```text
FLV TagType = Video
DataSize
Timestamp = DTS - startDTSOffset
StreamID
FrameType + CodecID
AVCPacketType：0=sequence header，1=NALU
CompositionTime = PTS - DTS
长度前缀 NALU payload
PreviousTagSize
```

H.264 的首字节典型值为：

```text
0x17：关键帧 + AVC
0x27：非关键帧 + AVC
```

在当前 Harmony 编码帧 `PTS == DTS` 的情况下，composition time offset 通常为 0。

### 12.4 H.265 与 Enhanced RTMP

项目同时包含经典兼容路径和 Enhanced RTMP 路径。Enhanced RTMP 会使用扩展视频头和 FourCC，并区分：

```text
Sequence Start
Frames/FramesX
Sequence End
```

当 HEVC 帧满足 `PTS == DTS` 时，可以使用 `FramesX`，省略值为 0 的 composition time offset。

## 13. RTMP 建连和实际网络写入

### 13.1 异步建立连接

`HJRTMPMuxer` 创建 `HJRTMPAsyncWrapper`，由独立 executor 建立网络连接：

```text
解析 RTMP URL
→ 创建 librtmp 对象
→ RTMP_Connect
→ HJRTMP_EVENT_CONNECTED
→ RTMP_ConnectStream
→ 设置 chunk size
→ HJRTMP_EVENT_STREAM_CONNECTED
```

`HJNAPILiveStream` 将 `HJRTMP_EVENT_STREAM_CONNECTED` 映射为上层的 `HJ_PUSHER_NOTIFY_CONNECT_SUCCESS`。

因此，创建编码器成功、拿到编码帧，甚至 TCP/RTMP 连接建立，都不等价于最终的“流发布成功”；项目以上层收到 stream connected 事件作为成功节点。

### 13.2 异步发送循环

连接流成功后，`HJRTMPAsyncWrapper::start()` 启动发送循环：

```text
从 HJRTMPMuxer 获取下一个 FLV Tag
→ 处理 socket 接收事件
→ RTMPWrapper::send()
→ RTMP_Write(tag bytes)
→ librtmp 拆成 RTMP message/chunk
→ 网络发送
```

主媒体 Tag 路径使用 `RTMP_Write()`；`RTMP_SendPacket()` 还用于 chunk size、footer 等特定控制或辅助包。

### 13.3 网络慢时的数据管理

PacketManager 会根据 NAL 类型设置视频包优先级，并维护排队、发送、丢弃和延迟统计。网络阻塞导致队列增长时，可按优先级丢弃视频帧，同时通过事件向上层报告：

- 丢帧；
- 当前网络码率；
- 推流延迟；
- 低码率；
- 自动调整编码码率；
- 断线与重试。

`extraTS` 用于计算编码输出到 Tag 发送阶段的排队延迟。网络反馈还可触发 `HJGraphPusher::adjustBitrate()`，形成：

```text
网络吞吐下降
→ RTMP 统计/自适应事件
→ 上层通知
→ 调整视频编码码率
→ 减少后续码流量
```

## 14. 每帧的完整执行时序

下面按一帧相机画面的时间顺序串联整个过程：

```text
1. CameraService 将图像写入 previewSurfaceId 对应的 NativeWindow

2. OH_NativeImage 触发 OnFrameAvailable
   → Bridge 标记 Available
   → 唤醒或等待 RTE render tick

3. RTE Source update
   → OH_NativeImage_UpdateSurfaceImage
   → OES texture 指向最新相机 buffer
   → 获取 transform matrix、宽高和相机 timestamp

4. RTE 从 Encoder Target 反向递归请求输入
   → Source 生成 OES DriftInfo

5. FilterCopyOES 绑定 RGBA FBO
   → samplerExternalOES 采样相机 OES
   → 应用纹理矩阵/镜像/裁剪
   → 写入 RGBA Texture 2D
   → 输出 2D DriftInfo + FBO lease

6. 后续 GPU Filter
   → sampler2D 读取上游纹理
   → Blur/Faceu/Gray/自定义处理
   → 输出最终 RGBA Texture 2D

7. Encoder Target
   → makeCurrent(encoder EGLSurface)
   → Copy2D Shader 绘制最终纹理
   → eglSwapBuffers
   → 图像提交到 OH_VideoEncoder NativeWindow

8. Harmony 硬编码器
   → Surface 图像被编码为 H.264/H.265
   → 输出 codec data 或媒体 access unit
   → OnNewOutputBuffer

9. HJVEncOHCodec::getFrame
   → 保存 SPS/PPS 或 VPS/SPS/PPS
   → 识别关键帧
   → 包装 HJMediaFrame/AVPacket
   → dataType = HJDATA_TYPE_ES
   → 当前实现 PTS = DTS = steady clock ms

10. HJPluginVideoOHEncoder
    → 可选插入 SEI
    → deliver 到 AVInterleave

11. HJPluginAVInterleave
    → 与 AAC 队首比较 DTS
    → 输出时间更早的编码帧

12. HJRTMPMuxer/HJFLVPacket
    → Annex B 转 4 字节长度前缀，或校验已有 AVCC
    → 识别关键帧和优先级
    → 建立时间戳 offset

13. HJRTMPPacketManager
    → metadata
    → audio sequence header
    → video sequence header
    → 等待首个关键帧
    → buildVideoTag/buildPacketFrames

14. HJRTMPAsyncWrapper
    → 取得 FLV Tag
    → RTMP_Write
    → librtmp 分块并发往流媒体服务器
```

## 15. 线程与异步边界

这条链路至少包含以下执行边界：

| 执行环境 | 主要工作 |
| --- | --- |
| Harmony Camera/Surface 线程 | 向 Surface 提交相机 buffer，触发 frame available |
| RTE 渲染线程 | 更新 NativeImage、OES→2D、滤镜处理、绘制到目标 EGLSurface |
| Harmony Codec 回调环境 | 通知新的编码输出 buffer |
| VideoOHEncoder Plugin 调度 | 从 codec output queue 取帧，包装并投递 |
| AVInterleave 工作线程 | 比较 DTS，排列音视频编码帧 |
| RTMP Plugin/Muxer | NALU 解析、FLV Tag 构建和包队列管理 |
| RTMP Async executor | 建连、重试、从 Muxer 取 Tag、网络发送 |

GPU 与编码器之间通过 NativeWindow buffer queue 和 `eglSwapBuffers()` 完成生产/消费同步，不是普通 C++ 函数同步调用。

## 16. 关于“零拷贝”的准确说法

可以说该链路避免了原始视频帧在 CPU 中反复搬运，但不要笼统地说“全链路一份内存、完全零拷贝”。更准确的分段描述是：

```text
相机 Surface → OES：外部缓冲直接 GPU 采样，避免 CPU readback
OES → 2D：发生一次 GPU render-to-texture
2D Filter → 2D Filter：GPU FBO 间绘制
2D → Encoder Surface：发生一次 GPU render-to-surface
Encoder → RTMP：CPU 处理的是压缩后的 H.264/H.265 小码流
```

所以项目优化的重点是：

- 不把每帧 YUV 拉回 CPU；
- 不在 CPU 上执行大分辨率 YUV/RGB 转换；
- 使用 FBO Pool 避免频繁创建纹理；
- 仅在必要 GPU 阶段进行纹理绘制；
- 网络侧只处理已经压缩的数据。

## 17. 常见面试误区

### 误区 1：相机回调直接给 `HJGraphPusher` 一帧 YUV

Harmony 主路径不是这样。相机把画面写到 `OH_NativeImage` 提供的 Surface，RTE 从 OES 纹理读取。

### 误区 2：OES 转 2D 只是修改纹理类型

不是。项目真正执行一次 GPU 绘制：从 `samplerExternalOES` 采样，写入 RGBA FBO 的 `GL_TEXTURE_2D`。

### 误区 3：OES 和 Texture 2D 都是 YUV

不准确。OES 是外部纹理 target，底层可能关联 YUV 平台缓冲；项目显式创建的 2D FBO 是 `GL_RGBA/GL_UNSIGNED_BYTE`。

### 误区 4：Harmony 编码器输出 MP4

当前推流代码把输出标记为 `HJDATA_TYPE_ES`，得到的是 H.264/H.265 codec data 和 access unit。MP4 需要额外 Muxer，不在 RTMP 主链中。

### 误区 5：AVCC 就是 MP4

AVCC 是长度前缀 NALU 和 AVC 配置记录的组织方式，MP4 是完整容器。FLV/RTMP 的 H.264 负载同样使用类似 AVCC 的组织。

### 误区 6：编码完成就代表推流成功

编码成功只说明产生了压缩帧。还必须完成 RTMP 建连、建流、sequence header 和媒体 Tag 发送；项目收到 `HJRTMP_EVENT_STREAM_CONNECTED` 才向上报告连接成功。

## 18. 面试回答版本

> HarmonyOS 上，相机不是把 CPU YUV 直接交给推流图，而是向 `OH_NativeImage` 提供的 NativeWindow 写平台图像缓冲。HJMedia 将这个外部缓冲绑定为 `GL_TEXTURE_EXTERNAL_OES`，新帧到达后更新 NativeImage，并取得纹理矩阵和宽高。RTE 的第一步使用 `samplerExternalOES` 把 OES 图像绘制到 RGBA FBO，因此明确得到普通 `GL_TEXTURE_2D`；后续美颜、Blur、Faceu 等滤镜都围绕 2D FBO 工作。最终 2D 纹理被绘制到 Harmony 硬编码器的 EGLSurface，`eglSwapBuffers` 后由 `OH_VideoEncoder` 消费，输出 H.264/H.265 的 codec data 和 ES 编码帧，而不是 MP4。项目再将 Annex B 或已有长度前缀 NALU 统一成 AVCC 风格，按 DTS 与 AAC 交织，生成 metadata、video sequence header 和 FLV Video Tag，最后由异步 RTMP 线程通过 librtmp 发送到服务器。整条链路避免了原始 YUV 的 CPU 回读，主要的数据转换是 OES 外部纹理到 RGBA Texture 2D、Texture 2D 到编码器 Surface、以及编码 ES 到 FLV/RTMP 的重封装。

## 19. 核心源码导航

| 处理阶段 | 源码位置 |
| --- | --- |
| Harmony 推流/预览入口 | `src/entry/pusher/hsys/verify/HJNAPILiveStream.cpp` |
| ArkTS 相机绑定 Surface | `examples/harmony/entry/src/main/ets/camera/service/CameraService.ets` |
| OES/NativeImage Bridge | `src/comp/graphic/hsys/HJOGRenderWindowBridge.cpp` |
| RTE 相机 Source | `src/comp/rte/HJRteComSourceBridge.cpp` |
| 默认 OES→2D Graph 配置 | `src/comp/rte/HJRteGraphSetupInfo.cpp` |
| OES/2D/FBO/EGL Draw | `src/comp/rte/HJRteComDraw.cpp` |
| RTE 递归渲染和 DriftInfo | `src/comp/rte/HJRteGraph.cpp` |
| RGBA FBO 创建 | `src/comp/graphic/HJOGFBOCtrl.cpp` |
| OES Shader | `src/comp/graphic/HJOGShaderCommon.cpp` |
| OES/2D 纹理绑定与绘制 | `src/comp/graphic/HJOGCopyShaderStrip.cpp` |
| 编码器 EGLSurface 环境 | `src/comp/graphic/hsys/HJOGRenderEnv.cpp` |
| 推流 Graph | `src/graphs/HJGraphPusher.cpp` |
| Harmony 视频编码 Plugin | `src/plugins/hsys/HJPluginVideoOHEncoder.cpp` |
| Harmony 视频硬编码器 | `src/media/codec/hsys/HJVEncOHCodec.cc` |
| 音视频 DTS 交织 | `src/plugins/HJPluginAVInterleave.cpp` |
| Annex B/AVCC NALU 处理 | `src/media/muxer/flv/HESParser.cc` |
| 编码帧进入 RTMP Muxer | `src/media/muxer/HJRTMPMuxer.cc` |
| RTMP 队列与 sequence header | `src/media/muxer/HJRTMPPacketManager.cc` |
| FLV Video Tag 生成 | `src/media/muxer/flv/HJFLVUtils.cc` |
| RTMP 建连和发送 | `src/media/muxer/HJRTMPWrapper.cc`、`src/media/muxer/HJRTMPAsyncWrapper.cc` |
