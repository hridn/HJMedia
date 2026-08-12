# ONNX 模型部署到 HarmonyOS 设备流程

本文以现有 ONNX 模型为输入，说明如何将它部署到 HarmonyOS 设备。推荐先走 **MindSpore Lite Kit**：它支持 ONNX 模型转换，应用侧使用统一 Native API 推理，并可选择接入 NNRt 获得硬件加速。性能、功耗要求极高且目标锁定麒麟 NPU 时，再评估 CANN Kit 的 `.om` 路线。

```text
推荐通用路线
ONNX → MindSpore Lite Converter → .ms（MindIR Lite）
     → ArkTS + N-API + MindSpore Lite → CPU
                                      └→ NNRt → NPU/其他 AI 加速设备

性能优先路线（麒麟平台）
ONNX → CANN 转换 → .om → ArkTS + N-API + CANN Kit → NPU
```

## 0. 三个 Kit 的关系，以及 `.ms` / `.om` 为什么同时存在

### 0.1 分层关系：框架、运行时、芯片后端

三者不是互相替代的三个应用 SDK，而是处在不同抽象层：

```text
应用（ArkTS + N-API）
│
├─ MindSpore Lite Kit：通用推理框架
│  ├─ 读取 .ms（MindIR Lite）
│  ├─ 在 CPU/GPU 上执行通用算子
│  └─ 对可支持子图委托给 NNRt
│
├─ CANN Kit：麒麟 NPU 的专用高性能入口/后端能力
│  ├─ 读取 .om（已面向目标 NPU 编译的离线模型）
│  └─ 提供 NPU 选定、AIPP、Profiling、Dump 等硬件相关选项
│
└─ Neural Network Runtime Kit（NNRt）：跨芯片运行时中间层
   ├─ 向上提供模型编译、设备、Tensor、Executor 等统一 Native API
   └─ 向下通过 HDI 对接 NPU/DSP 等芯片后端；麒麟平台由 CANN 支撑
```

- **MindSpore Lite Kit** 是推理框架。它负责模型加载、图调度、通用 CPU/GPU 执行与异构划分；对应用而言是最常用的统一入口。
- **NNRt** 是操作系统的中间运行时，不负责训练，也不等同于某颗 NPU。它把上层框架调用统一成设备、编译、执行和内存接口，并将其转交给下层硬件 HDI 服务。
- **CANN Kit** 是麒麟 AI 硬件的专用计算与优化能力。在 HarmonyOS 端侧，它可作为 NNRt 的麒麟后端，并向应用暴露更贴近 NPU 的模型部署和调优能力。

因此，MindSpore Lite 可以“通过 NNRt 使用 NPU”；CANN 则是麒麟平台上实现/发挥该 NPU 能力的专用路径。应用不必同时使用三者：通用应用常用 MindSpore Lite（可选 NNRt），对固定麒麟机型做深度性能优化时才直接走 CANN Kit。

### 0.2 `.ms`：面向推理框架和异构调度的 MindIR Lite 模型

`.ms` 是 MindSpore Lite 的模型产物，模型语义仍以 MindIR 图为中心，重点是“由推理框架决定如何执行”。它更适合：

- 同一模型运行在 CPU、GPU 和可用 NNRt 设备上；
- 保留不支持算子的 CPU 回退；
- 从 ONNX、TensorFlow Lite、Caffe 等模型统一转换后部署；
- 将应用层与具体芯片型号尽量解耦。

本地 `MindSporeLiteCDemo` 直接证明了这种加载方式：其 CMake 链接 `mindspore_lite_ndk`，在 `mslite_napi.cpp` 中创建 `OH_AI_DEVICETYPE_CPU`，并调用：

```cpp
OH_AI_ModelBuild(model, modelBuffer, modelSize,
                 OH_AI_MODELTYPE_MINDIR, context);
```

样例资源中的 `mobilenetv2.ms` 正是由这个 API 作为 `MINDIR` 类型加载。它没有要求应用预先选择某一颗 NPU。

当启用 NNRt 后，MindSpore Lite 源码通过 `MSLITE_ENABLE_NNRT` 生成 `SUPPORT_NNRT`；`lite_session.cc` 会创建 `NNRTDelegate`，调度器将可支持的子图交给 NNRt。`context_c.cc` 还提供 `OH_AI_DEVICETYPE_NNRT` 和 `OH_AI_GetAllNNRTDeviceDescs`，说明 NNRt 是附加到 MindSpore Lite Context 的设备后端，而不是另一种模型文件格式。

### 0.3 `.om`：面向特定 NPU 的离线编译模型

`.om` 是 CANN 离线模型。它不是通用 IR 的简单换后缀，而是已经根据目标 NPU 的算子支持、内存规划、融合和调度等信息完成离线编译的部署产物。它更适合：

- 目标设备和芯片平台可控，尤其是麒麟 NPU；
- 追求更低时延、功耗或更稳定的性能；
- 需要 CANN 的设备顺序、AIPP、Profiling、Dump 等深度调优能力。

本地 `CANNKit-SampleCode-Clientdemo-cpp` 使用 `resources/rawfile/hiai.om`，而不是 `.ms`；其 CMake 链接 `hiai_foundation` 与 `libneural_network_core.so`。模型加载链路为：

```cpp
auto compilation = OH_NNCompilation_ConstructWithOfflineModelBuffer(data, size);
OH_NNCompilation_SetDevice(compilation, deviceId);
HMS_HiAIOptions_SetModelDeviceOrder(
    compilation, &HIAI_EXECUTE_DEVICE_NPU, 1);
OH_NNCompilation_Build(compilation);
auto executor = OH_NNExecutor_Construct(compilation);
OH_NNExecutor_RunSync(executor, inputs, inputCount, outputs, outputCount);
```

其中 `HMS_HiAIOptions_SetModelDeviceOrder(... NPU ...)` 与 `HMS_HiAIOptions_SetOmOptions` 是 CANN 特有的硬件选项；这正是 `.om` 路线能做深度 NPU 优化和维测的原因。

### 0.4 为什么两种格式不能混用

| 对比项 | `.ms` | `.om` |
| --- | --- | --- |
| 主要消费者 | MindSpore Lite Kit | CANN Kit / NNRt 离线模型执行路径 |
| 抽象层次 | 推理框架 IR 模型 | 目标 NPU 离线编译模型 |
| 加载 API | `OH_AI_ModelBuild(... OH_AI_MODELTYPE_MINDIR ...)` | `OH_NNCompilation_ConstructWithOfflineModelBuffer(...)` |
| 硬件可移植性 | 高：可配置 CPU/GPU/NNRt，视算子支持异构执行 | 低：依赖生成该模型时面向的 CANN/NPU 能力 |
| 优化重心 | 框架通用性、异构调度、CPU 回退 | 特定 NPU 的性能、功耗、AIPP 与维测 |

所以：

- 不能用 `OH_AI_ModelBuild(...OH_AI_MODELTYPE_MINDIR...)` 加载 `.om`；
- 不能把 `.ms` 传给 CANN 离线模型构造接口，期待它自动成为 NPU 编译模型；
- ONNX 是上游交换格式，针对同一个 ONNX 模型，可以选择转换为 `.ms` 或转换为 `.om`，但这是两条不同的部署管线。

### 0.5 如何选择

1. **首次部署或要求多机型兼容**：ONNX → `.ms` → MindSpore Lite CPU 基线 → 可选 NNRt。
2. **模型稳定、目标为麒麟 NPU、性能功耗是核心指标**：ONNX → `.om` → CANN Kit。
3. **芯片/系统厂商接入新加速硬件**：实现 NNRt HDI 后端；这不是普通 App 的工作范围。

## 1. 部署前准备

### 1.1 固化模型契约

在 PC 上先使用 ONNX Runtime 对固定样本完成推理，并保存 golden 数据。需要记录：

- 模型的 ONNX opset 与是否包含自定义算子；
- 输入和输出名称；
- 输入/输出 shape，例如 `images: [1, 3, 224, 224]`；
- 数据类型，例如 `float32`、`uint8` 或 `int8`；
- 数据布局：`NCHW` 或 `NHWC`；
- 图像预处理：RGB/BGR、resize、letterbox、归一化、mean/std；
- 输出后处理：Softmax、Top-K、NMS、mask 解码、token 解码等；
- 至少一组输入和预期输出，用于端侧精度对比。

> ONNX 在桌面端可运行不等于可直接部署。转换前优先处理动态 shape、过高 opset、未支持算子和自定义算子。

### 1.2 环境与工程基线

- 安装与目标 HarmonyOS SDK 相兼容的 DevEco Studio、SDK 和真机调试工具 `hdc`。
- 准备 MindSpore Lite Converter；Converter 版本应不高于端侧 Runtime 的兼容范围，建议转换与 Runtime 使用同一发行版本。
- 使用本工作区的 [MindSporeLiteCDemo](../projects/MindSporeLiteCDemo/code/DocsSample/ApplicationModels/MindSporeLiteCDemo) 作为工程骨架。

## 2. ONNX 转 MindSpore Lite 模型

基本转换命令：

```bash
converter_lite \
  --fmk=ONNX \
  --modelFile=your_model.onnx \
  --outputFile=your_model
```

成功后生成 `your_model.ms`。该文件是 HarmonyOS MindSpore Lite Kit 可加载的 MindIR Lite 模型。

若输入 shape 是动态的，移动端建议先固定为实际业务尺寸：

```bash
converter_lite \
  --fmk=ONNX \
  --modelFile=your_model.onnx \
  --outputFile=your_model \
  --inputShape="images:1,3,224,224"
```

其中 `images` 必须替换为真实输入节点名。输入 shape 必须与应用预处理和后续推理时写入 tensor 的数据完全一致。

转换成功不表示结果正确。立即用同一组 golden 输入运行转换后的模型，对比输出误差和最终业务结果。

## 3. 接入 HarmonyOS 应用

### 3.1 放置模型

将生成的模型加入工程：

```text
entry/src/main/resources/rawfile/your_model.ms
```

原始示例中的 `mobilenetv2.ms` 可以替换为你的模型。模型和标签、类别字典、tokenizer 等配套资源应保持版本绑定。

### 3.2 维持 ArkTS 与 Native 分层

建议沿用示例的职责划分：

```text
ArkTS
  ├─ UI、媒体/相机输入、任务调度、结果展示
  └─ 图像裁剪和轻量状态处理

N-API / C++
  ├─ 加载 rawfile 模型
  ├─ 创建 Context 与 Model
  ├─ 填充输入 Tensor
  ├─ 调用推理
  └─ 解析输出、释放资源
```

生产环境避免将大图像或 tensor 逐元素从 ArkTS 传到 C++。优先传 `ArrayBuffer`、TypedArray 或 Native buffer，减少跨语言复制。

### 3.3 CMake 链接

在 `entry/src/main/cpp/CMakeLists.txt` 中链接 MindSpore Lite NDK：

```cmake
add_library(entry SHARED mslite_napi.cpp)
target_link_libraries(entry PUBLIC
  mindspore_lite_ndk
  hilog_ndk.z
  rawfile.z
  ace_napi.z)
```

### 3.4 Native 推理骨架

下面的代码仅表示生命周期；输入 buffer 的 layout、dtype、大小须按模型契约实现。

```cpp
auto context = OH_AI_ContextCreate();

// 先用 CPU 跑通，建立精度和稳定性基线。
auto cpu = OH_AI_DeviceInfoCreate(OH_AI_DEVICETYPE_CPU);
OH_AI_DeviceInfoSetEnableFP16(cpu, true);
OH_AI_ContextAddDeviceInfo(context, cpu);

auto model = OH_AI_ModelCreate();
auto ret = OH_AI_ModelBuild(
    model, modelBuffer, modelSize,
    OH_AI_MODELTYPE_MINDIR, context);
if (ret != OH_AI_STATUS_SUCCESS) {
    // 记录错误，释放 model/context
}

auto inputs = OH_AI_ModelGetInputs(model);
// 检查 inputs.handle_num、dtype、shape、字节数。
// 将预处理后数据写入 inputs.handle_list[i]。

auto outputs = OH_AI_ModelGetOutputs(model);
ret = OH_AI_ModelPredict(model, inputs, &outputs, nullptr, nullptr);
if (ret == OH_AI_STATUS_SUCCESS) {
    // 按输出 shape/dtype 进行 Top-K、NMS 或其他后处理。
}

OH_AI_ModelDestroy(&model);
```

对应可参考 [mslite_napi.cpp](../projects/MindSporeLiteCDemo/code/DocsSample/ApplicationModels/MindSporeLiteCDemo/entry/src/main/cpp/mslite_napi.cpp)。

## 4. 先验证 CPU 路线

在接入 NNRt/NPU 前，完成以下基线验证：

1. 模型能加载，`OH_AI_ModelBuild` 返回成功。
2. 每个输入 tensor 的元素数、dtype、shape 和数据大小均符合预期。
3. 与 ONNX Runtime golden 输出对比。浮点模型可设置合理绝对/相对误差；检测、分类等模型同时比较最终业务结果。
4. 验证冷启动、连续推理、前后台切换、异常模型文件和内存压力。
5. 推理必须在工作线程运行，不能阻塞 ArkTS UI 线程。

如出现精度错误，排查顺序通常是：输入 layout → 色彩空间 → resize/letterbox → 归一化 → dtype/量化参数 → 输出解析，而不是先怀疑硬件加速。

## 5. 接入 NNRt 异构加速（可选）

CPU 路线正确后，可以为同一个 MindSpore Lite Context 加入 NNRt 设备。`third_party_mindspore` 中已包含相关 API、NNRt Delegate 和设备内存管理实现。

执行步骤：

1. 确认所用 MindSpore Lite Runtime 已启用 NNRt 支持；自编译时检查 `MSLITE_ENABLE_NNRT` / `SUPPORT_NNRT`。
2. 调用 `OH_AI_GetAllNNRTDeviceDescs` 枚举设备，并根据实际名称、ID 和类型选择目标设备。
3. 创建 `OH_AI_DEVICETYPE_NNRT` 的 device info，设置选定 device ID 后加入 context。
4. 保留 CPU device info，作为未下沉算子或无可用加速设备时的回退。
5. 对比 CPU-only、NNRt 优先、异构三种配置的精度、首帧时延、稳态时延、内存和功耗。

概念代码：

```cpp
size_t count = 0;
NNRTDeviceDesc* descs = OH_AI_GetAllNNRTDeviceDescs(&count);
size_t selectedId = /* 根据枚举结果选定 */;
OH_AI_DestroyAllNNRTDeviceDescs(&descs);

auto nnrt = OH_AI_DeviceInfoCreate(OH_AI_DEVICETYPE_NNRT);
OH_AI_DeviceInfoSetDeviceId(nnrt, selectedId);
OH_AI_ContextAddDeviceInfo(context, nnrt);

auto cpu = OH_AI_DeviceInfoCreate(OH_AI_DEVICETYPE_CPU);
OH_AI_ContextAddDeviceInfo(context, cpu);
```

不要硬编码设备名或假设所有机型都有 NPU。NNRt 的实际算子下沉比例由设备、系统版本和模型图共同决定。

## 6. CANN Kit 路线（麒麟 NPU 性能优先）

若目标机型固定为麒麟平台、模型稳定且对性能/功耗有更高要求，可选择：

```text
ONNX → CANN 转换 → your_model.om → CANN Kit 应用
```

应用侧实现可从 [CANNKit-SampleCode-Clientdemo-cpp](../projects/CANNKit-SampleCode-Clientdemo-cpp) 复制结构：

1. 将 `.om` 放入 `resources/rawfile/`；
2. `OH_NNCompilation_ConstructWithOfflineModelBuffer` 加载模型；
3. 枚举并选择 CANN/NPU 设备；
4. `OH_NNCompilation_SetDevice`、`OH_NNCompilation_Build`；
5. 创建 `OH_NNExecutor` 与输入/输出 `NN_Tensor`；
6. 调用 `OH_NNExecutor_RunSync`；
7. 需要时通过 `HMS_HiAIOptions_SetOmOptions` 开启 Profiling/Dump。

该路线不能直接加载 `.ms`，MindSpore Lite 路线也不能把 `.om` 当作 `OH_AI_MODELTYPE_MINDIR` 加载；两者的模型格式和 API 生命周期不同。

## 7. 模型量化与性能优化

优先顺序：

1. 先建立 Float32 CPU 正确性基线。
2. 确认端侧输入尺寸和 batch，尽量使用固定 shape。
3. 在离线转换阶段评估权重量化或训练后量化；量化后必须重新跑 golden 测试。
4. 再启用 FP16、NNRt 或 CANN NPU。
5. 使用真实机型上的端到端延迟评价：预处理 + 数据拷贝 + 推理 + 后处理，而非只看推理 API 时间。

对于相机视频流：复用模型、context、tensor 和输入 buffer；使用生产者/消费者队列限制积压；当推理跟不上帧率时主动丢弃旧帧，优先处理最新帧。

## 8. 交付检查表

- [ ] ONNX Runtime golden 输入输出已保存。
- [ ] ONNX 转 `.ms` 成功，且转换日志已归档。
- [ ] `.ms` 与端侧 Runtime 版本兼容。
- [ ] CPU 后端与 golden 输出/业务结果一致。
- [ ] 输入输出的 shape、layout、dtype 和预后处理已写入代码注释或配置。
- [ ] N-API 不在 UI 线程执行长耗时推理。
- [ ] NNRt/NPU 不可用时可降级到 CPU，并有日志或指标记录。
- [ ] 在目标机型完成冷启动、连续运行、内存、功耗和异常恢复测试。
- [ ] 模型文件、标签文件、tokenizer 和应用版本可追溯。

## 9. 常见故障

| 现象 | 优先检查 |
| --- | --- |
| Converter 失败 | ONNX opset、动态 shape、自定义算子、`Resize` 等算子属性是否受支持；必要时简化/重导出 ONNX 图。 |
| `.ms` 加载失败 | Converter 与 Runtime 版本、模型是否完整写入 rawfile、模型类型是否为 `OH_AI_MODELTYPE_MINDIR`。 |
| 端侧结果错误但不报错 | RGB/BGR、NCHW/NHWC、resize、归一化、float/int8 量化参数、输出索引。 |
| NNRt 不生效 | NNRt 编译开关、设备枚举结果、算子支持、系统版本，以及 CPU 回退是否掩盖了下沉失败。 |
| 首帧很慢或内存上涨 | 是否每次推理都构建/销毁 model，是否重复申请大 buffer，是否在 ArkTS/C++ 间重复复制。 |

## 10. 本地参考

- [MindSpore Lite 应用示例](../projects/MindSporeLiteCDemo/code/DocsSample/ApplicationModels/MindSporeLiteCDemo)
- [MindSpore Lite NNRt 实现](../projects/third_party_mindspore/mindspore-src/source/mindspore/lite)
- [NNRt 框架和 Delegate/HDI 示例](../projects/ai_neural_network_runtime/example)
- [CANN Kit 图像分类示例](../projects/CANNKit-SampleCode-Clientdemo-cpp)
