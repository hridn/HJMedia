# 鸿蒙端侧 AI 部署指南

> 基于本工作区已拉取的 CANN Kit、MindSpore Lite Kit 与 Neural Network Runtime（NNRt）示例代码整理。本文面向希望在 HarmonyOS/OpenHarmony 设备上部署视觉模型、接入 NPU，或为芯片实现 NNRt 接入的开发者。

## 1. 结论与选型

应用侧通常不需要直接改动 NNRt。优先按模型形态和优化目标选择入口：

| 需求 | 推荐入口 | 典型模型 | 依据仓库 |
| --- | --- | --- | --- |
| 尽快完成跨设备、本地推理 | MindSpore Lite Kit | `.ms` / MindIR | `MindSporeLiteCDemo` |
| 已有麒麟 NPU 离线模型，重点追求性能、功耗和维测 | CANN Kit | `.om` | `CANNKit-SampleCode-Clientdemo-cpp`、`CANNKit-Codelab-Clientdemo-cpp` |
| 让 MindSpore Lite 将可支持算子下沉至系统 AI 加速设备，并保留 CPU 回退 | MindSpore Lite + NNRt | MindIR | `third_party_mindspore` |
| 为 OpenHarmony 新增 NPU/DSP 等芯片后端 | NNRt HDI | NNRt 图/张量描述 | `ai_neural_network_runtime` |
| 已有 TFLite 框架，需研究接入 NNRt | TFLite NNRt Delegate | `.tflite` | `ai_neural_network_runtime/example/deep_learning_framework/tflite` |

应用层路径：

```text
ArkTS UI → N-API → C/C++ 推理封装
                       ├─ MindSpore Lite → CPU / GPU / NNRt 加速设备
                       └─ CANN Kit → NNRt → 麒麟 NPU（CANN 后端）
```

NNRt 处于 AI 框架和加速芯片之间。它向上提供 Native 运行时 API，向下通过 HDI 连接芯片驱动；MindSpore Lite 的 NNRt 支持在源码中由 `MSLITE_ENABLE_NNRT` / `SUPPORT_NNRT` 编译开关启用。

## 2. 已分析仓库与参考价值

| 本地目录 | 主要内容 | 建议阅读点 |
| --- | --- | --- |
| `projects/CANNKit-SampleCode-Clientdemo-cpp` | SqueezeNet 图片分类、`.om` 模型、CANN Native API | `entry/src/main/cpp/HIAIModelManager.cpp` |
| `projects/CANNKit-Codelab-Clientdemo-cpp` | CANN 模型推理 Codelab | 同上，适合按工程步骤学习 |
| `projects/MindSporeLiteCDemo` | MobileNetV2 图片分类、`.ms` 模型、N-API | `.../entry/src/main/cpp/mslite_napi.cpp` |
| `projects/third_party_mindspore` | MindSpore Lite 的 NNRt Delegate、设备和内存实现 | `mindspore-src/source/mindspore/lite/src/litert` |
| `projects/ai_neural_network_runtime` | NNRt 框架、TFLite Delegate、HDI CPU 设备样例 | `example/deep_learning_framework`、`example/drivers` |
| `projects/applications_objectDetection` | 历史物体识别工程 | README 内容不完整，只适合作为补充线索 |

> NPU Profiling Codelab 的展示名能检索到，但其公开 Git 地址在克隆时返回 404；本文改以 CANN 图片分类样例中已经存在的 Profiling 调用说明为准。

## 3. CANN Kit：`.om` 离线模型直接部署

### 3.1 适用场景

- 目标为麒麟平台，已有 CANN 生成的 `.om` 离线模型。
- 需要明确指定 NPU，或需要 Profiling/Dump 等调优能力。
- 希望控制执行设备次序、带宽模式和模型执行选项。

### 3.2 样例实际工程结构

`CANNKit-SampleCode-Clientdemo-cpp` 的核心划分如下：

```text
entry/src/main/
├─ ets/pages/Index.ets                 # ArkTS 页面、图像预处理与结果展示
├─ cpp/Classification.cpp               # N-API 导出：加载、输入、执行、取结果、卸载
├─ cpp/HIAIModelManager.cpp             # CANN/NNCore 模型生命周期
└─ resources/rawfile/
   ├─ hiai.om                           # 离线模型
   └─ labels_caffe.txt                  # 分类标签
```

Native 构建需链接 `hiai_foundation` 与 `libneural_network_core.so`：

```cmake
FIND_LIBRARY(cann-lib hiai_foundation)
target_link_libraries(entry PUBLIC
  libace_napi.z.so libhilog_ndk.z.so librawfile.z.so
  ${cann-lib} libneural_network_core.so)
```

### 3.3 部署流程

1. 将 `.om` 与标签文件放在 `resources/rawfile/`；运行时可通过 `OH_ResourceManager_OpenRawFile` 读入内存。
2. 枚举 `OH_NNDevice_GetAllDevicesID`，用 `OH_NNDevice_GetName` 找到样例使用的 `HIAI_F` 设备。
3. 用 `OH_NNCompilation_ConstructWithOfflineModelBuffer` 从内存模型构造编译对象。
4. 调用 `OH_NNCompilation_SetDevice` 绑定设备；再用 `HMS_HiAIOptions_SetModelDeviceOrder` 指定 `HIAI_EXECUTE_DEVICE_NPU`。
5. `OH_NNCompilation_Build` 后创建 `OH_NNExecutor`。
6. 基于 executor 创建输入/输出 tensor 描述与 `NN_Tensor`，填入预处理后的数据。
7. 调用 `OH_NNExecutor_RunSync`，读取输出 tensor，做 Softmax/Top-K 后处理。
8. 在 Ability 销毁或页面退出时释放 tensor、executor 和模型状态。

关键骨架：

```cpp
auto compilation = OH_NNCompilation_ConstructWithOfflineModelBuffer(data, size);
OH_NNCompilation_SetDevice(compilation, deviceId);
HMS_HiAIOptions_SetModelDeviceOrder(compilation, &npu, 1);
OH_NNCompilation_Build(compilation);
auto executor = OH_NNExecutor_Construct(compilation);
OH_NNExecutor_RunSync(executor, inputs, inputCount, outputs, outputCount);
```

### 3.4 CANN 调优注意事项

- 样例会调用 `HMS_HiAICompatibility_CheckFromBuffer`，部署前应据此验证 `.om` 与目标设备兼容性。
- 样例包含 `HMS_HiAIOptions_SetOmOptions(..., HIAI_OM_TYPE_PROFILING, outPath)`，可启用模型维测；输出目录必须是应用可写目录。
- 若业务不需要维测，避免默认开启 Profiling，它会带来额外 I/O 与性能扰动。
- 输入的数据类型、布局、尺寸必须和离线模型一致。样例将 ArkTS 的 `Uint8Array` 转为 `float` 后写入 tensor；你的模型若要求归一化、RGB/BGR 或 NCHW，应在这一层明确实现。

## 4. MindSpore Lite Kit：MindIR 应用侧部署

### 4.1 适用场景

- 需要统一的本地推理接口，先以 CPU 为基线，再逐步接入 GPU/NNRt。
- 模型可转换为 `.ms` / MindIR。
- 希望把 ArkTS UI 与推理执行清晰地隔离在 N-API 两侧。

### 4.2 样例落地方式

`MindSporeLiteCDemo` 将 `mobilenetv2.ms` 放在 rawfile 中，ArkTS 负责相册选图、裁剪和像素预处理，N-API 的 `runDemo()` 负责模型生命周期。其 CMake 直接链接：

```cmake
target_link_libraries(entry PUBLIC mindspore_lite_ndk)
target_link_libraries(entry PUBLIC hilog_ndk.z rawfile.z ace_napi.z)
```

Native API 流程为：

```cpp
auto context = OH_AI_ContextCreate();
auto cpu = OH_AI_DeviceInfoCreate(OH_AI_DEVICETYPE_CPU);
OH_AI_DeviceInfoSetEnableFP16(cpu, true);
OH_AI_ContextAddDeviceInfo(context, cpu);

auto model = OH_AI_ModelCreate();
OH_AI_ModelBuild(model, buffer, size, OH_AI_MODELTYPE_MINDIR, context);
auto inputs = OH_AI_ModelGetInputs(model);
// 填充 inputs.handle_list[0]
auto outputs = OH_AI_ModelGetOutputs(model);
OH_AI_ModelPredict(model, inputs, &outputs, nullptr, nullptr);
OH_AI_ModelDestroy(&model);
```

### 4.3 应用实现建议

1. 不要在每次点击推理时重建模型。官方教学样例为清晰展示 API，在 `runDemo()` 中构建和销毁；生产应用应将 model/context 缓存到 Native 单例或管理器，按模型版本或内存压力释放。
2. 图像预处理保持在 Native 或共享 buffer 中，避免 ArkTS 数组逐元素传递大张量。样例的 `napi_get_element` 循环适合演示，不适合高帧率相机流。
3. 对每个输入检查 `OH_AI_TensorGetDataType`、元素数、数据大小和布局；输出同样要先核对 tensor 名称与 shape。
4. CPU 基线正确后再启用 FP16、NNRt 或异构配置，便于区分模型精度问题与硬件后端问题。

## 5. MindSpore Lite + NNRt：统一模型图与异构执行

### 5.1 代码证据

`third_party_mindspore` 中：

- `mindspore/lite/CMakeLists.txt` 定义 `MSLITE_ENABLE_NNRT`，启用后编译 `SUPPORT_NNRT`。
- `src/litert/c_api/context_c.cc` 提供 `OH_AI_GetAllNNRTDeviceDescs`、`OH_AI_CreateNNRTDeviceInfoByName/ByType` 等设备发现和创建接口。
- `src/litert/lite_session.cc` 创建 `NNRTDelegate`；调度器按可支持算子将子图下沉至 NNRt。
- `src/tensor.cc` 和 `NNRTAllocator` 包含 NNRt tensor 内存管理路径，说明该组合不仅是 API 转发，还涉及设备内存生命周期。

MindSpore Lite 基准工具展示的上下文配置流程：先枚举 NNRt 设备，选择目标设备 ID，创建 `OH_AI_DEVICETYPE_NNRT`，再将该设备和 CPU 设备加入同一 context。CPU 的存在为不被硬件支持的算子提供回退条件。

### 5.2 推荐接入流程

1. 将模型转换为 MindIR，并先以 MindSpore Lite CPU 成功运行。
2. 确认 SDK/运行库编译时启用 NNRt 支持；自编译场景检查 `MSLITE_ENABLE_NNRT`。
3. 枚举 NNRt 设备并记录名称、ID、类型；不要假设设备名称固定。
4. 创建 NNRt device info，设置设备 ID、性能模式、优先级和 FP16 等参数，再加入 context。
5. 同时加入 CPU device info，构建模型并做正确性比对。
6. 分别采集 CPU-only、NNRt-only/优先、异构三组数据：首帧时延、稳态时延、内存、功耗、精度。

概念代码：

```cpp
size_t count = 0;
auto descs = OH_AI_GetAllNNRTDeviceDescs(&count);
auto nnrt = OH_AI_DeviceInfoCreate(OH_AI_DEVICETYPE_NNRT);
OH_AI_DeviceInfoSetDeviceId(nnrt, selectedId);
OH_AI_ContextAddDeviceInfo(context, nnrt);

auto cpu = OH_AI_DeviceInfoCreate(OH_AI_DEVICETYPE_CPU);
OH_AI_ContextAddDeviceInfo(context, cpu);
OH_AI_ModelBuild(model, buffer, size, OH_AI_MODELTYPE_MINDIR, context);
```

### 5.3 异构设计原则

- 以 `CPU + NNRt` 作为兼容性优先的默认方案；实际下沉比例取决于设备和算子支持集。
- NNRt 的模型图与 MindSpore Lite 同属 MindIR 路线，避免将整个模型往返转换为另一个框架图；这是该组合相对于通用 Delegate 路线的主要优势。
- 预处理、后处理可能仍在 CPU。只有连续、足够大的可下沉子图通常才能抵消跨设备和数据移动开销。
- 必须做机型/系统版本白名单与 CPU 回退，避免在缺少相应 NNRt 设备时启动失败。

## 6. NNRt：框架接入与芯片接入

### 6.1 框架开发者

`ai_neural_network_runtime/example/deep_learning_framework/tflite` 提供 TFLite Delegate 示例。`label_classify.cpp` 暴露 `--use_nnrt` 参数；Delegate 将 TFLite 可加速子图转换为 `OH_NNModel`，然后通过以下生命周期执行：

```text
OH_NNModel_Construct
  → AddTensor / AddOperation / SpecifyInputsAndOutputs / Finish
  → OH_NNCompilation_Construct / SetDevice / Build
  → OH_NNExecutor_Construct / SetInput / SetOutput / Run
```

这适合已有推理框架需要适配 OpenHarmony 的情况，但需要处理算子映射、量化参数、动态 shape 和内存所有权。对于新应用，优先采用 MindSpore Lite 或 CANN Kit，成本通常更低。

### 6.2 芯片/系统开发者

`ai_neural_network_runtime/example/drivers/nnrt/v2_0` 是 HDI CPU 服务样例。它包含：

- HDI 服务和驱动的实现框架；
- 图、节点、tensor 与量化参数校验；
- `PreparedModelService` 的编译、输入输出绑定、执行；
- 将 NNRt 图转换并交给 MindSpore Lite `Model::Build(..., kMindIR, context)` 的 CPU 参考实现。

落地新芯片后端时应实现对应的 HDI 服务、模型编译/缓存、buffer 共享和算子支持查询，并在产品的 HDF、进程账号、SELinux、组件构建配置中注册。此工作属于系统镜像和驱动集成，普通 HAP 应用不应尝试完成。

## 7. 推荐的端到端交付流程

1. **模型验收**：锁定输入 shape、布局、数据类型、量化方式、输出语义；保存一组 golden input/output。
2. **CPU 基线**：用 MindSpore Lite Native API 跑通 MindIR 模型，完成正确性测试。
3. **硬件路线决策**：已有 `.om` 时直接验证 CANN；需要跨芯片能力时验证 MindSpore Lite + NNRt。
4. **工程封装**：ArkTS 只负责交互和状态；N-API 负责资源加载、模型会话、buffer 与错误码；C++ 负责推理与后处理。
5. **资源管理**：模型常驻、按需加载；避免每帧创建 tensor/上下文；后台/内存压力时卸载大模型。
6. **验证矩阵**：至少覆盖冷启动、连续推理、后台前台切换、异常模型文件、NPU 不可用回退、不同输入尺寸。
7. **性能调优**：先使用 CANN Profiling/Dump 或 NNRt/MindSpore 日志定位，再优化模型、数据布局和线程/设备配置；不要只看单次推理时间。

## 8. 常见风险清单

- **模型格式混用**：CANN 样例的 `.om` 不能直接按 MindSpore Lite 的 `OH_AI_MODELTYPE_MINDIR` 加载；`.ms`/MindIR 也不能直接视为 CANN 离线模型。
- **设备名称硬编码**：CANN 样例寻找 `HIAI_F`，MindSpore Lite 基准中对 `NPU_` 前缀有筛选逻辑；生产代码应枚举并根据能力选择，不能假设所有设备相同。
- **隐性预处理错误**：RGB/BGR、NHWC/NCHW、归一化、量化 scale/zero-point 不匹配会比 API 错误更常见。
- **把同步推理放 UI 线程**：`OH_NNExecutor_RunSync` 和 `OH_AI_ModelPredict` 都可能耗时，必须在任务线程执行，并通过异步 N-API 回到 ArkTS。
- **没有 CPU 回退**：NNRt/NPU 的算子覆盖和系统版本可能变化，需提供可观测的回退策略。
- **示例版本差异**：仓库中部分示例面向 API 11/12，CANN 新版说明涉及 API 18；以目标设备安装的 SDK 头文件、系统能力和 DevEco 版本为最终准则。

## 9. 下一步建议

若要新建一个可维护的鸿蒙端侧视觉 AI 应用，建议以 `MindSporeLiteCDemo` 的 ArkTS + N-API 分层为骨架，先实现 MindIR CPU 基线；若目标确定是麒麟 NPU 且已有 `.om`，替换 Native 推理管理器为 `HIAIModelManager` 风格的 CANN 实现；若需要设备可移植性，则把 NNRT device info 作为可选配置，始终保留 CPU 后端。
