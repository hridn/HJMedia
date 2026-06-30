# CLAUDE.md

本文档用于指导 Claude Code（claude.ai/code）在本仓库中工作。

## 项目概览

HJMedia 是一个跨平台的 C++ 多媒体框架，由花椒直播开发，当前版本 **v1.0.7**（见 `CHANGELOG.md`）。

产品链路：
- **Pusher**（采集 / 处理 / 编码 / RTMP 推流）
- **Player**（解复用 / 解码 / 渲染 / 播放）
- **Inference / Render**（AI 推理 / 人脸检测 / 萌颜 / 渲染管线）
- **Music Player**（纯音频播放 + 混音）

支持平台：**HarmonyOS（主力）**、Android、iOS、Windows、macOS、Linux。

## 对象模型与生命周期

理解 HJMedia 的类继承体系是阅读和编写任何代码的前提。

### 继承链

```
HJObject  (src/utils/HJObject.h)
  └─ HJSyncObject  (src/utils/HJSyncObject.h) — 添加 init/done 生命周期 + 互斥锁状态机
       └─ HJPlugin  (src/plugins/HJPlugin.h) — 媒体处理单元
       └─ HJGraph   (src/graphs/HJGraph.h) — 图编排基类
       └─ HJExecutor (src/utils/HJExecutor.h) — 线程执行器
```

**HJObject** — 所有类的根，提供：
- `HJ_DECLARE_PUWTR(ClassName)` — 生成 `Ptr`/`Utr`/`Wtr` 类型别名
- `getGlobalName(prefix)` / `getGlobalID()` — 全局唯一名称和 ID
- `sharedFrom(this)` — 增强的 shared_from_this，支持动态类型转换
- 静态工厂：`HJObject::creates<T>(args...)` / `HJObject::createu<T>(args...)`

**HJSyncObject** — 在 HJObject 基础上添加带锁的 init/done 生命周期：
- `init(HJKeyStorage::Ptr)` — 同步初始化，内部调用 `internalInit()` → `afterInit()`
- `done()` — 同步清理，内部调用 `beforeDone()` → `internalRelease()`
- `getStatus()` — 返回 `HJSTATUS_NONE` / `HJSTATUS_Inited` / `HJSTATUS_Done`
- 锁宏：`SYNCHRONIZED_SYNC_LOCK`、`SYNC_WAIT`、`SYNC_PROD_LOCK`、`SYNC_CONS_LOCK`
- 便捷宏：`SHARED_FROM_THIS`（获取自身的 typed shared_ptr）、`CHECK_DONE_STATUS`

**智能指针约定**：类定义中使用 `ClassName::Ptr`、`ClassName::WPtr`，几乎不使用裸指针。

### HJKeyStorage — 参数传递机制

所有 `init()` 方法都接受 `HJKeyStorage::Ptr i_param`，它本质是 `std::unordered_map<std::string, std::any>`。通过以下宏提取参数：

```cpp
// 可选参数（不存在时返回默认值）
GET_PARAMETER(TYPE, NAME)           // auto NAME = i_param->getValue<TYPE>("NAME")
GET_PARAMETER2(TYPE, NAME, DEFAULT) // TYPE NAME = DEFAULT; 若存在则覆盖
// 必需参数（不存在时返回 HJErrInvalidParams）
MUST_GET_PARAMETER(TYPE, NAME)
MUST_HAVE_PARAMETERS                // if (!i_param) return HJErrInvalidParams
```

### 初始化和错误处理模式

使用 `do { ... } while (false)` + `break` 实现可中断的顺序初始化：
```cpp
int init() {
    int res = HJ_OK;
    do {
        res = step1(); if (HJ_OK != res) break;
        res = step2(); if (HJ_OK != res) break;
        res = step3(); if (HJ_OK != res) break;
    } while (false);
    return res;
}
```

错误码定义在 `src/utils/HJError.h`：`HJ_OK`、`HJ_EOF`、`HJErrInvalidParams`、`HJErrNotAlready`、`HJErrNotSupport`、`HJ_WOULD_BLOCK`、`HJ_IDLE`、`HJ_AGAIN` 等（完整列表见该文件）。

## 构建与依赖命令

请在仓库根目录下执行。

### 同步第三方依赖

```bash
./sync_deps.sh
```

此脚本安装 `depsync` 工具并拉取 `DEPS` 文件中声明的所有第三方源码到 `third_party/`，随后自动执行 `apply_patches.sh` 为各第三方库打补丁。
DEPS 文件（JSON）定义依赖项，每个条目录包含 url、commit、dir 三元组。

补丁文件位于 `patches/` 目录，覆盖的库包括：`sonic`、`soundtouch`、`librtmp`、`yyjson`、`fdk-aac`、`spdlog`、`imgui`、`implot`、`glfw`、`ImFileDialog`、`ImGuiFileDialog`、`zlib`、`lz4`、`imgui-node-editor`。补丁通过 `git apply --directory=third_party/<name>` 应用，可幂等执行（已应用的补丁自动跳过）。

### CMake Presets 构建

`CMakePresets.json` 定义了以下 configure preset：

| Preset | 用途 | 生成器 | toolchain |
|---|---|---|---|
| `build-windows-x64` | Windows VS 2022 Debug | Visual Studio 17 2022 | — |
| `build-android-arm64-v8a` | Android arm64 release | Ninja | android.toolchain.cmake |
| `build-android-armeabi-v7a` | Android armv7 release | Ninja | android.toolchain.cmake |
| `build-harmony-arm64-v8a` | HarmonyOS arm64 release | Ninja | ohos.toolchain.cmake |

#### HarmonyOS

```bash
cmake --preset build-harmony-arm64-v8a
cmake --build --preset build-harmony
```

- 需要 DevEco Studio SDK（`CMakePresets.json` 中 `TOOL_HOME` 环境变量指向 DevEco Studio 安装目录）。
- 输出到 `build_harmony/`。
- 产物 `.so` 需手动拷贝到 Harmony har 包目录（见 `build_harmony.bat` / `build_hos.sh` 注释掉的 copy 命令作为参考）。

#### Android

```bash
cmake --preset build-android-arm64-v8a
cmake --build --preset build-arm64
```

- 需要 `ENV_ANDROID_SDK`、`ENV_ANDROID_NDK_VERSION`、`ENV_ANDROID_CMAKE_VERSION`、`ENV_ARCH`、`ENV_API` 环境变量。
- 也可以用 `build_android.bat` 一键构建（内联 NDK 路径和 CMake 参数）。

#### Windows

```bash
cmake --preset build-windows-x64
cmake --build --preset build-windows-x64
```

### 平台专用构建脚本

| 脚本 | 用途 |
|---|---|
| `build_harmony.bat` | Windows 上交叉编译 Harmony（使用 DevEco SDK Ninja/cmake） |
| `build_hos.sh` | macOS 上交叉编译 Harmony |
| `build_android.bat` | Windows 上交叉编译 Android（arm64 + armv7） |
| `build_ios.sh` | 构建 iOS 用 Xcode 工程 |
| `build_mos.sh` | 构建 macOS |
| `ioscopy.sh` | iOS 产物拷贝辅助 |
| `HJMediaOBSExport.bat` | OBS 集成导出脚本 |

### CMake 关键选项

- `HJ_ENABLE_TNN`：启用 TNN 推理支持（Windows 默认 ON，其它 OFF）
- `HJ_ENABLE_RENDER_PRIO`：启用 Render Prio 支持（默认 OFF）
- `DISABLE_HJPLAYER`：禁用播放器模块
- `DISABLE_HJPUSER`：禁用推流器模块
- `DISABLE_HJRENDER`：禁用渲染入口模块
- `DISABLE_HJINFERENCE`：禁用推理入口模块（会同时禁用 `detect` 和 `inferenceRender`）

平台宏定义：`WIN32_LIB`、`ANDROID_LIB`、`IOS_LIB`、`MACOS_LIB`、`LINUX_LIB`、`Harmony_LIB`。

## 代码约定与核心宏

### 命名空间

```cpp
#define NS_HJ_BEGIN  namespace HJ {
#define NS_HJ_END    }
```

所有类、函数、枚举都在 `HJ::` 命名空间内。源文件首尾分别用 `NS_HJ_BEGIN` / `NS_HJ_END`。

### 锁与 RAII

```cpp
HJ_AUTO_LOCK(m_mutex)      // std::lock_guard
HJ_AUTOU_LOCK(m_mutex)     // std::unique_lock
HJ_AUTOS_LOCK(m_mutex)     // std::shared_lock
```

**HJOnceToken** — 作用域守卫，常用于 `proRun()` 确保退出时清理 busy 状态：
```cpp
int proRun() {
    HJOnceToken token(nullptr, [&] { setIsBusy(false); });  // 退出时自动执行
    // ... 处理逻辑 ...
}
```

### MessageBus 系统（v1.0.7 新增）

Graph 和 Plugin 之间通过类型安全的消息总线通信，替代传统的观察者模式。`src/utils/HJMessageBus.h` 定义了三种总线：

| 总线 | 用途 | 返回值 | 注册/调用宏 |
|---|---|---|---|
| `HJMessageBus` | 通用同步消息调用 | `HJResult<Ret>` | `HJ_BUS_REGISTER` / `HJ_BUS_CALL` |
| `HJQueryBus` | 查询/应答（Graph 暴露给 Plugin 的只读接口） | `HJResult<Ret>`（非 void） | `HJ_QUERY_REGISTER` / `HJ_QUERY_CALL` |
| `HJEventBus` | 发布/订阅事件（一对多广播） | void | `HJ_EVENT_REGISTER` / `HJ_EVENT_SUBSCRIBE` / `HJ_EVENT_PUBLISH` |

**消息声明宏**（通常集中在 `HJGraphInfo.h`）：
```cpp
HJ_QUERY_DECLARE(bool, QUERY_HAS_AUDIO);                        // 查询消息
HJ_QUERY_DECLARE(bool, QUERY_CAN_DELIVER_TO_OUTPUTS, size_t, const HJMediaFrame::Ptr&);
HJ_EVENT_DECLARE(EVENT_PLUGIN_NOTIFY, HJPluginNotifyType, size_t);  // 事件消息
HJ_EVENT_DECLARE(EVENT_GRAPH_EOF);                                  // 无参数事件
HJ_MSG_DECLARE(int, MSG_CUSTOM, const std::string&, int);         // 通用消息
```

**使用模式**：
- Graph 持有 `HJQueryBus::Ptr` 和 `HJEventBus::Ptr`（见 `HJGraph` 基类的 `queryBus()` / `eventBus()`）
- Plugin 通过 `HJ_BUS_CALL(graph->queryBus(), QUERY_HAS_AUDIO)` 查询 Graph 状态
- Plugin 通过 `HJ_EVENT_PUBLISH(graph->eventBus(), EVENT_PLUGIN_NOTIFY, type, id)` 上报事件
- `HJMessageBus` 区分 static handler（编译期注册，不可取消）和 dynamic handler（运行期注册，可取消）
- `HJEventBus` 的 subscribe 返回 `shared_ptr<void>` token，用于取消订阅；listener 返回 `HJErrAlreadyDone` 会自动取消

**与旧通知系统的关系**：旧的 `HJNoticeCenter` / `HJNotification` 模式仍存在于部分代码中。新代码应优先使用 MessageBus。

### 日志宏

| 宏 | 级别 |
|---|---|
| `HJLogt(msg, ...)` | Trace |
| `HJLogd(msg, ...)` | Debug |
| `HJLogi(msg, ...)` | Info |
| `HJLogw(msg, ...)` | Warning |
| `HJLoge(msg, ...)` | Error |
| `HJLogf(msg, ...)` | Fatal |

带 `HJF` 前缀的版本支持 fmt 风格格式化：`HJFLogi("init end, res:{}", res)`，`HJFLoge("error:{}", err)`。

### 其他常用宏

| 宏 | 用途 |
|---|---|
| `HJ_TYPE_NAME(type)` | `typeid(type).name()` — 用作类名字符串 |
| `HJ_ABS(a)` / `HJ_MAX(a,b)` / `HJ_MIN(a,b)` / `HJ_CLIP(x,min,max)` | 数学辅助 |
| `HJ_SEC_TO_MS(sec)` / `HJ_MS_TO_SEC(ms)` | 时间单位转换 |
| `HJ_NOTS_VALUE` | 无效时间戳哨兵值 (0x8000000000000000) |
| `HJ_UNUSED(x)` | 消除未使用参数警告 |
| `IF_FALSE_BREAK(COND, RET)` | 条件为 false 则设置 ret 并 break（常用于 do-while 初始化） |
| `IF_FAIL_BREAK(COND, RET)` | 条件 != HJ_OK 则设置 ret 并 break |
| `HJ_DEFINE_CREATE(ClassName)` | 生成 `ClassName::Ptr` 工厂方法 |

### HJMediaFrame 帧类型与管道流通

所有通过 Graph/Plugin 管道流通的数据单元都是 `HJMediaFrame`（`src/media/HJMediaFrame.h`），含视频 `HJAVFrame`（包装 `AVFrame`）和音频 `HJAVPacket`（包装 `AVPacket`）。

**帧类型标志**（`HJFrameType`，用位掩码区分）：

| 标志 | 含义 |
|---|---|
| `HJFRAME_NORMAL` | 普通数据帧 |
| `HJFRAME_KEY` | 关键帧（I 帧 / IDR） |
| `HJFRAME_EOF` | 流结束标记（沿 DAG 向下游传播） |
| `HJFRAME_NULL` | 空帧（占位，不含数据） |
| `HJFRAME_CLEAR` | 清空信号（通知下游清空缓冲） |
| `HJFRAME_FLUSH` | Flush 信号（如 Seek 时的冲刷标记） |

**帧缓冲容量常量**（定义在 `HJMediaFrame.h`）：
- 视频解码：`HJ_VDEC_STOREAGE_CAPACITY` = 15
- 音频解码：`HJ_ADEC_STOREAGE_CAPACITY` = 50
- 视频编码：`HJ_VENC_STOREAGE_CAPACITY` = 3
- 视频渲染：`HJ_VREND_STOREAGE_CAPACITY` = 3
- 音频渲染：`HJ_AREND_STOREAGE_CAPACITY` = 10

## 测试 / lint 状态

- 没有统一的顶层 lint 或 CTest 工作流。
- `externals/windows/gtest` 中存在 gtest 预编译库，但仓库没有全局测试运行器。
- 部分模块有 `test/` 子目录：`src/datafuse/test`、`src/db/test`、`src/localserver/test`、`src/plugins/test`、`src/utils/test`。
- 部分入口模块有 `verify/` 子目录（如 `src/entry/player/hsys/verify`）。
- 如果你新增或修改测试，请在对应模块的 CMake target 或 README 中写清运行命令。

## 架构（整体视角）

顶层 CMake（`CMakeLists.txt`）按以下顺序组织 HJMedia：

### 1. 外部依赖（`third_party/*`）

**编解码 / 媒体处理：**
`librtmp`、`fdk-aac`、`zlib`、`lz4`、`sonic`（音频变速）、`soundtouch`（音频变调）、`miniaudio`（跨平台音频 I/O）

**数据 / 工具：**
`yyjson`（JSON 解析）、`spdlog`（日志）、`sqlite`、`sqlite_orm`、`cpp-httplib`（HTTP 客户端/服务器）、`minicoro`（协程）、`stb`（单头文件图像库）、`refl-cpp`（静态反射）

**桌面 UI（Windows / macOS）：**
`glfw`、`imgui`、`implot`、`imgui-node-editor`、`ImFileDialog`、`ImGuiFileDialog`

**预编译平台 externals（`externals/`）：**
按平台分目录存放预编译的第三方库：`ffmpeg`、`libyuv`、`mbedtls`、`openssl`、`x264`、`x265`、`ncnn`、`libsrt`、`opencv`、`sqlite`、`gtest`、`libcurl`、`tnn`、`mindspore`

### 2. 框架模块（`src/*`）

| 模块 | 说明 |
|---|---|
| `src/utils` | 基础工具层：线程封装（`HJThread`）、平台工具（`asys`/`hsys`/`osys`/`base`）、文档 |
| `src/stat` | 统计子系统 |
| `src/comp` | **组件层**：可组合媒体处理组件（下方展开） |
| `src/datafuse` | 数据融合模块 |
| `src/convert` | 格式转换工具（非 macOS/iOS 平台编译） |
| `src/media` | **媒体层**：采集（capture）、编解码（codec）、通信（com）、数据源（datasource）、解复用（demuxer）、I/O、封装（muxer）、网络（net）、渲染（render）、SEI |
| `src/links` | 链接/传输层 |
| `src/core` | **核心运行时**：上下文（HJContext）、环境（HJEnvironment）、图基类（HJGraphBase/HJGraphDec）、节点抽象（HJMediaNode 及 A/V 编解码/渲染/解复用/封装节点）、播放器基类（HJPlayerBase/HJMediaPlayer）、缩略图提取器（HJThumbnailExtractor）、时间轴处理（HJTimelineHandler） |
| `src/db` | **数据库层**：基于 sqlite_orm 的媒体数据库管理（HJMediaDBManager）、Block 状态存储（HJDBBlockStatus）、Blob 适配器 |
| `src/localserver` | **本地 HTTP 服务**：缓存管理（HJCacheManager/HJCacheFile）、块文件管理（HJBlockManager/HJBlockFile）、HTTP 下载器（HJDownloader）、HTTP 服务器（HJHTTPServer）、本地服务器入口（HJLocalServer） |
| `src/plugins` | **插件层**：平台特定的插件实现（`hsys`=Harmony、`asys`=Android、`isys`=iOS、`wsys`=Windows）、文档（`doc/`） |
| `src/graphs` | **图编排层**：HJGraphPusher（推流图）、HJGraphLivePlayer（直播播放图）、HJGraphVodPlayer（点播播放图）、HJGraphMusicPlayer（音乐播放图）、HJGraphAudioMixer（混音图） |
| `src/gui` | **桌面 GUI 框架**：基于 ImGui 的应用框架（HJApplication、HJWindow、HJView、HJImage、HJFileDialog） — 仅 Windows/macOS |
| `src/platform` | 平台抽象：共享内存（sharedmemory）— 仅 Windows |
| `src/detect` | **AI 检测层**：人脸检测等，多后端支持：`hsys/mindspore`（华为 MindSpore）、`ncnn`（腾讯 ncnn）、`tnn`（腾讯 TNN）、`osys/coreml`（Apple CoreML）、`osys/vision`（Apple Vision）、`osys/VTFrameProcessor`（VideoToolbox 帧处理） |
| `src/entry` | **产品入口层**（下方展开） |

#### `src/comp` 组件层子结构

| 子模块 | 说明 |
|---|---|
| `comp/graphic` | OpenGL 图形组件：Shader 管理（HJOGBaseShader、HJOGPointShader、HJOGShaderProgram）、FBO 控制、PBO 读取、EGL Surface、拷贝 Shader Strip 等 |
| `comp/prio` | **Prio 渲染管线**：基于 FBO 的 GPU 后处理组件链（SourceBridge、Faceu 萌颜、Blur 模糊、Gray 灰度、GiftSeq 礼物序列、SplitScreen 分屏、SourceSeries 源序列）、图编排（HJPrioGraph/HJPrioGraphProc） |
| `comp/rte` | **Runtime Engine 渲染管线**：实时绘制组件（HJRteComDraw、超分滤波 SRFilter、降噪 DenoiseFilter）、Faceu 输入桥、Image/ImageSeq 输入、图编排（HJRteGraph/HJRteGraphProc/HJRteGraphSetupInfo） |
| `comp/utils` | 组件工具：人脸点位管理（HJFacePointMgr/HJFacePointMadeup）、Faceu 信息（HJFaceuInfo）、图片序列信息（HJImgSeqInfo）、FBO 池 |
| `comp/include` | 公共头文件 |

#### `src/entry` 入口层子结构

| 子模块 | 说明 | 控制选项 |
|---|---|---|
| `entry/pusher` | 推流器入口（`hsys` + `bridge` + `verify`） | `DISABLE_HJPUSER` |
| `entry/player` | 播放器入口（`asys`/`hsys`/`isys` + `bridge` + `verify`） | `DISABLE_HJPLAYER` |
| `entry/render` | 渲染入口（`asys`/`faceu`） | `DISABLE_HJRENDER` |
| `entry/inference` | AI 推理入口（`asys`/`utils`） | `DISABLE_HJINFERENCE` |
| `entry/inferenceRender` | Harmony 推理+渲染整合（`hsys`/`verify`） | `DISABLE_HJINFERENCE` + `HarmonyOS` |
| `entry/hsys` | Harmony 通用入口：HJEntryBaseRender、NAPI 导出封装（HJNativeExportCommon）、线程安全函数包装器 | — |
| `entry/utils` | 入口工具（`asys`） | — |

### 3. 平台 / 演示层（`examples/` 和 `demo/`）

| 路径 | 内容 |
|---|---|
| `examples/harmony` | Harmony 演示：hjpusher HAR 包、hjplayer HAR 包、hjmediautils 共享工具、hvigor 构建 |
| `examples/android` | Android 演示 / SDK 封装 |
| `examples/ios` | iOS 演示（HJMediaDemo、HJMediaUI、HJPlayerDemo） |
| `examples/windows` | Windows 演示 / 工具：HJMainUI、HJMediaUI、HJMediaTest、HJMediaGraphicUI、HJOpencv、XMediaTools、XMediaTest |
| `examples/resource` | 共用资源：audio、faceu、image、imgseq、model |
| `demo/faceu` | 萌颜 SDK 集成演示（android / harmony / ios / pc 四个平台） |

### 图驱动的流水线模型

媒体流由组件（Node/Plugin）组装成图（Graph），分别用于不同场景：

- **Pusher 路径**：采集 → 人脸检测/萌颜前处理(可选) → 编码 → 封装/交织 → RTMP 输出
- **LivePlayer 路径**：RTMP/HTTP-FLV/M3U8 输入 → 解复用 → 解码 → 萌颜后处理(可选) → 渲染输出
- **VodPlayer 路径**：点播源 → 解复用 → 解码 → 渲染，支持暂停/恢复/Seek/变速
- **MusicPlayer 路径**：音频源 → 解复用 → 解码 → 重采样 → 音频渲染，支持 repeat/seek/EOF
- **AudioMixer 路径**：多路 PCM 输入 → 混音 → PCM 输出

组装逻辑分布在 `src/core`（节点基础抽象）、`src/graphs`（图结构定义）和 `src/comp`（具体组件实现）中。

### 节点体系核心概念（src/core）

理解 `src/core` 的四个关键概念（详见 `docs/architecture/HJCore_Architecture.md`）：

**1. Node 拓扑与帧缓冲**：`connect(A, B, capacity)` 建立 DAG 边时，**帧缓冲创建在消费者 B 侧**（`m_inStorages` 中为生产者 A 分配一个 `HJMediaStorage`）。即消费者管理缓冲区。节点通过 `weak_ptr` 持有邻接节点，避免循环引用。

**2. 推拉双驱动模型**：`HJMediaNode` 通过 `m_driveType` 控制数据流动方向：
- `HJNODE_DRIVE_NEXT` — `deliver()` 后异步调度自身继续生产
- `HJNODE_DRIVE_PRE` — `pop()` 后唤醒前驱节点继续生产
- 解码/编码节点默认双向驱动，渲染节点仅 `DRIVE_PRE`（纯拉模型）

**3. 反压与容量估算**：`proRun()` 中通过 `nextNode->isFull()` 检查实现反压。容量按时间窗口估算：视频 `capacity ≈ 帧率 × 时间容量(默认2s)`，音频 `capacity ≈ 采样率 × 时间容量 / 每帧采样数`。音频缓冲在多后继场景下自动弹性扩容 1.2 倍。

**4. 任务调度模型**：任务封装为 `HJTaskCancelable`（`src/utils/HJChore.h`），通过 `HJExecutor`（`src/utils/HJExecutor.h`）调度执行。每个 Executor 拥有一个专用线程。关键 API：

- `executor->asyncTask(task)` — 异步投递任务到队列
- `executor->async(runnable, delayMS)` — 创建并投递一次性延迟任务
- `executor->asyncFixed(runnable, rate)` — 创建固定频率的周期性任务（如 30fps 渲染）
- `executor->sync(runnable)` — 同步执行并等待完成（用于 `done()`/`seek()` 等需保证返回时状态已更新的操作）

节点通过 `m_isBusy` 标记防止重入。`asyncSelf(delayTimeMS)` 支持延迟调度（用于 A/V 同步等场景）。每个 Executor 可通过 `HJExecutorLoadCounter` 查询 CPU 使用率。

### 插件系统

插件层（`src/plugins`）实现可复用的媒体处理单元，被图层的 Graph 组装。按平台分为：
- `hsys`：HarmonyOS（音频采集 OH、音频渲染 OH、视频解码 OH、视频编码 OH）
- `asys`：Android（音频 AAudio 渲染）
- `isys`：iOS（音频渲染）
- `wsys`：Windows（WASAPI 音频渲染）

每个插件遵循统一的 lifecycle（init → start → process → stop → release）和 I/O 布线模型。详细文档在 `src/plugins/doc/README.md` 及同目录下各 `.md` 文件。

## Harmony 示例 / API 背景

- `examples/harmony/API.md` v1.0.0 记录 HJPusher / HJPlayer 的 TypeScript API（`@hj-live/hjpusher` / `@hj-live/hjplayer`）。
- HJPusher API：`contextInit`、`createPusher`、`destroyPusher`、`openPreview`、`closePreview`、`setWindow`、`openPusher`、`closePusher`、`setMute`、`openRecorder`、`closeRecorder`、`openPngSeq`。
- HJPlayer API：`contextInit`、`preloadUrl`、`createPlayer`、`destroyPlayer`、`setWindow`、`openPlayer`（含 SEI 回调）、`closePlayer`、`exitPlayer`、`setMute`。
- `examples/harmony/hjmediautils` 提供 pusher/player 包共用的 Harmony 工具。
- HAR 包通过示例工程内的 Build 配置生成（见 `examples/harmony/hjpusher/HJPusher_build.jpg` 和 `HJPlayer_build.jpg` 截图）。

## 文档导航

| 路径 | 内容 |
|---|---|
| `README.md` | 项目主页文档（特性矩阵、快速入门、编译说明） |
| `CHANGELOG.md` | 版本更新日志（v1.0.0 ~ v1.0.7） |
| `docs/` | 设计/架构文档 |
| `docs/architecture/` | **HJCore_Architecture.md**（核心运行时完整架构）、HJGraphMusicPlayer 架构、音频上下文指南、iOS Face SR Rect |
| `docs/plans/` | 实现计划（iOS player 框架、audio session、music player 等） |
| `docs/product/` | 产品需求文档 |
| `docs/delivery/` | 交付文档 |
| `docs/HJAudioCompositeManager.md` | 音频合成管理器文档 |
| `docs/Readme_AudioMixer.md` | 混音器文档 |
| `docs/Readme_MusicPlayer.md` | 音乐播放器文档入口 |
| `docs/sei_handling_design.md` | SEI 处理设计 |
| `docs/hjplugin_audio_mixer_strategy.md` | 音频混音策略 |
| `src/plugins/doc/README.md` | 插件文档索引（含按任务类型的推荐阅读路径） |
| `src/utils/HJThread/doc/` | 线程模块文档 |
| `study/` | 4 周学习计划大纲和详细计划 |
| `studyNote/` | 学习笔记 |

## 实用导航提示

对于大多数功能或 bug 工作：

1. **确定产品层入口**：`src/entry/pusher`、`src/entry/player`、`src/entry/render`、`src/entry/inference`。
2. **看对应的 Graph**：`src/graphs/HJGraphPusher`、`HJGraphLivePlayer`、`HJGraphVodPlayer`、`HJGraphMusicPlayer`、`HJGraphAudioMixer`。
3. **深入 Graph 引用的 Plugin / Component**：在 `src/plugins` 和 `src/comp` 中定位具体处理单元。
4. **追踪运行时核心**：`src/core` 中的 Node 基类和 Graph 基类。
5. **平台适配**：在 `CMakeLists.txt` 的平台分支和 `examples/<platform>` 中验证集成。
6. **涉及数据库/缓存**：查看 `src/db`、`src/localserver`。
7. **涉及 AI / 人脸检测**：查看 `src/detect`。

### 修改前阅读文档的建议流程

对于插件层修改，建议先读 `src/plugins/doc/README.md` 判断涉及哪些插件链，再按推荐阅读路径建立上下文。对于音乐播放器修改，建议先读 `docs/Readme_MusicPlayer.md` → `docs/architecture/HJGraphMusicPlayer.md` → `docs/architecture/HJGraphMusicPlayer_AudioContextGuide.md`。

### 常见陷阱

- **线程模型**：Plugin 和 Graph 运行在不同线程上，注意帧队列（HJMediaFrameDeque）的线程安全语义。每个 Node 的 `proRun()` 可能在调度器线程上被异步调用，`m_isBusy` + `m_mutex` 防止重入。
- **connect 的方向**：`connect(A, B, capacity)` 在 B 侧为 A 创建 InStorage——不是 A 持有输出缓冲。忘记这一点会导致对缓冲生命周期的错误理解。
- **DriveType 影响调度**：Node 的 `m_driveType` 决定收到帧/消费帧后的调度行为。渲染节点设为仅 `DRIVE_PRE`，若错误设为默认值会导致不必要的 CPU 唤醒。
- **EOF / Flush 传播**：EOF 帧沿 DAG 向下游传播；当所有后继都 EOF 后，前驱才标记自己 EOF。`done()` 调用必须同步等待（`scheduler->sync()`）以确保所有异步任务完成后再释放资源。
- **Seek 的 preFlush**：Seek 前必须同时暂停音频和视频渲染（`setPreFlush(true)`），避免旧帧在 seek 期间被渲染。
- **生命周期**：Plugin 的 init → start → process → stop → release 状态机由 Graph 层驱动，不要在未 ready 时调用 process。
- **平台宏**：代码中大量使用 `#ifdef` 平台宏（`WIN32_LIB`、`Harmony_LIB` 等），修改跨平台逻辑时需考虑所有平台。
- **HJ_ 前缀**：所有宏、类型、函数都以 `HJ` 为前缀，不要引入无前缀的公共符号。
- **两套通知系统并存**：旧代码使用 `HJNoticeCenter` / `HJNotification`（观察者模式），新代码（v1.0.7+）使用 `HJMessageBus` / `HJQueryBus` / `HJEventBus`（类型安全消息总线）。开发新功能时优先使用 MessageBus 系列；修改旧模块时注意它可能仍依赖 HJNoticeCenter。
- **HJSyncObject 生命周期**：`init()` 和 `done()` 内部持有生产者锁（`SYNC_PROD_LOCK`），不要在锁内调用可能等待同一对象锁的操作。`beforeDone()` 不在锁内且可能被重复调用——实现时要幂等。`afterInit()` 在锁内调用，是线程安全的。
