# 鸿蒙端侧 AI 部署问答

> 用于记录 HarmonyOS 端侧 AI 部署相关的面试问答与复盘，覆盖 MindSpore Lite、NNRt、CANN Kit，以及 ONNX 模型转换与应用集成。

---

## 问题 1：目前主流的 AI 框架有哪些？请对比其优缺点。

这里的“AI 框架”要区分两类：**训练/研究框架**主要解决模型开发；**推理框架/运行时**主要解决模型在手机、边缘设备或服务器上的高效部署。对于 HarmonyOS 端侧部署，第二类更直接相关。

| 框架 | 主要定位与模型格式 | 优点 | 缺点 | HarmonyOS 端侧建议 |
| --- | --- | --- | --- | --- |
| **PyTorch** | 主流训练与研究框架；动态图，Python 生态为主 | 开发体验好；社区、模型和大模型生态强；调试直观 | 原生训练运行时较重；不能把 Python 训练代码直接放到移动端执行 | 常用于训练和导出 ONNX；端侧运行选 ExecuTorch、ONNX Runtime 或转换到 MindSpore Lite |
| **TensorFlow / Keras** | 训练框架；TensorFlow Lite（`.tflite`）用于端侧 | 工具链成熟；移动端与量化、Delegate 生态完善；Keras 上手快 | 端侧模型格式和工具链与 ONNX/MindIR 不完全通用；大型工程配置相对复杂 | 已有 `.tflite` 模型时可评估转换或 NNRt Delegate；新 HarmonyOS 项目通常优先评估 MindSpore Lite |
| **ONNX Runtime** | 跨框架推理运行时，消费 `.onnx`；通过 Execution Provider 对接硬件 | 模型来源广；C/C++ API 成熟；可按 CPU、GPU、NPU 等后端切分子图；跨平台能力强 | 端侧二进制和算子集需要裁剪；不同 Execution Provider 的算子覆盖、性能差异大 | 适合已有 ONNX 资产和多平台需求；HarmonyOS 的官方主路径仍更适合 ONNX 转 `.ms` 后使用 MindSpore Lite |
| **ExecuTorch** | PyTorch Edge 的轻量端侧推理方案，基于 PyTorch 2 Export | 比旧 PyTorch Mobile 更轻；覆盖手机、嵌入式与 MCU；面向 CPU/GPU/NPU/DSP 后端 | 较新，第三方模型和平台集成成熟度不如 ONNX Runtime；需适配 PyTorch Export 图 | 若模型训练链完全基于 PyTorch，值得评估；HarmonyOS 集成需要自行验证 NDK、算子和后端支持 |
| **MindSpore / MindSpore Lite** | 昇思训练框架及轻量推理框架；端侧部署 `.ms` / MindIR | HarmonyOS 有 Kit 与 Native API；支持 ONNX 转换；可先 CPU 跑通，再接 NNRt；适合 ArkTS + N-API 分层 | 生态规模和现成模型数量通常小于 PyTorch；转换器版本与端侧 Runtime 必须兼容 | **HarmonyOS 通用部署首选**：ONNX → `.ms` → CPU 基线 → 可选 NNRt |
| **NNRt** | HarmonyOS 系统级 AI 推理运行时，不是完整训练框架 | 屏蔽 NPU/DSP 等底层差异；可让上层框架把可支持子图下沉到 AI 加速设备 | 只负责已接入的 AI 硬件，不负责通用 CPU 推理；设备、算子和系统版本依赖明显 | 通常作为 MindSpore Lite 的可选硬件后端；必须保留 CPU 回退，避免硬编码设备名 |
| **CANN Kit** | 麒麟 NPU 专用计算与部署能力；使用 `.om` 离线模型 | 可做 NPU 定向编译、AIPP、Profiling、Dump、内存和功耗深度优化；性能上限高 | `.om` 与目标 NPU 强绑定，可移植性低；模型转换、维测和适配成本高 | 目标锁定麒麟 NPU、模型稳定且性能/功耗是核心指标时选择；不与 `.ms` 混用 |
| **NCNN / MNN** | 轻量 C++ 端侧推理引擎，分别偏腾讯与阿里生态 | 包体小、C++ 友好；对移动 CPU/GPU 有较多优化；国内视觉模型部署资料多 | 主要适配 Android/iOS/Linux 等，HarmonyOS 的官方系统级 NPU 路径不如 MindSpore Lite + NNRt 清晰；算子与模型转换需逐项验证 | 可作为纯 Native、自带推理引擎的备选；若需要 HarmonyOS NPU 的官方路径，优先前述方案 |

### 面试式总结

可以按“**训练用什么、模型以什么格式流转、端侧最终跑在哪块硬件**”来回答：PyTorch 和 TensorFlow/Keras 主导训练与模型开发；ONNX 是跨框架交换格式；ONNX Runtime、ExecuTorch、TensorFlow Lite、MindSpore Lite、NCNN/MNN 等负责推理部署；NNRt 和 CANN 则分别承担 HarmonyOS 的系统级硬件抽象与麒麟 NPU 深度优化。

对 HarmonyOS 项目而言：有 ONNX 模型且追求兼容性时，优先走 `ONNX → MindSpore Lite .ms → CPU + 可选 NNRt`；只有在目标确定为麒麟 NPU、且性能和功耗值得投入额外适配成本时，再走 `ONNX → CANN .om → NPU`。

### 资料依据

- [ONNX Runtime：Execution Providers](https://onnxruntime.ai/docs/execution-providers/)
- [ONNX Runtime Mobile](https://onnxruntime.ai/docs/get-started/with-mobile.html)
- [ExecuTorch Overview](https://docs.pytorch.org/executorch/stable/intro-overview.html)
- [MindSpore Lite Kit 简介](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides/mindspore-lite-kit-introduction)

---

## 问题 2：`.ms` 和 `.om` 两种模型格式有什么区别？模型加载、推理时如何体现？

`.ms` 和 `.om` 不是同一模型的两种文件后缀，而是**不同部署层次的产物**：`.ms` 是交给 MindSpore Lite 推理框架调度的 MindIR Lite 模型；`.om` 是已经针对某个 CANN/NPU 能力离线编译后的模型。

| 对比项 | `.ms` | `.om` |
| --- | --- | --- |
| 所属路线 | MindSpore Lite / MindIR Lite | CANN Kit 离线模型 |
| 上游来源 | ONNX、TensorFlow Lite、Caffe、MindSpore 等可转换为 MindIR | 通常由 ONNX 或其他支持格式经 CANN 工具链转换 |
| 执行决策 | 运行时由 MindSpore Lite 决定 CPU、GPU 或 NNRt 子图如何执行 | 关键算子、内存规划、融合与调度已面向目标 NPU 离线确定 |
| 可移植性 | 较高：同一个模型可配置 CPU、GPU、NNRt；不支持的部分可回退 CPU | 较低：与生成时的 CANN 版本、NPU 能力和目标设备兼容性强相关 |
| 优化重心 | 统一推理接口、框架级图调度与异构回退 | 麒麟 NPU 性能、功耗、AIPP、Profiling、Dump 等深度调优 |
| 典型应用 | 多机型部署、先保证正确性再逐步加速 | 目标机型稳定、性能/功耗是关键 KPI 的产品 |

### 1. 加载阶段的区别

`.ms` 由 MindSpore Lite 的 Model API 加载。应用需要创建 Context，向其中加入 CPU 和可选的 NNRt device info，再把模型按 `MINDIR` 类型构建：

```cpp
auto context = OH_AI_ContextCreate();

auto cpu = OH_AI_DeviceInfoCreate(OH_AI_DEVICETYPE_CPU);
OH_AI_ContextAddDeviceInfo(context, cpu);

auto model = OH_AI_ModelCreate();
auto ret = OH_AI_ModelBuild(
    model, modelBuffer, modelSize,
    OH_AI_MODELTYPE_MINDIR, context);
```

此时 MindSpore Lite 读取的是模型图。若 Context 同时配置 NNRt，它会把可支持的连续子图委托给 NNRt，其余算子仍可由 CPU 执行；因此 `.ms` 的“加载”包含了框架运行时的图分析、调度和潜在子图划分。

`.om` 则走 NNRt/CANN 的离线模型加载路径。它不需要以 MindIR 图方式交给 MindSpore Lite 再做框架级调度，而是从二进制 buffer 构造 Compilation，并绑定实际的 NPU 设备后构建 Executor：

```cpp
auto compilation =
    OH_NNCompilation_ConstructWithOfflineModelBuffer(data, size);
OH_NNCompilation_SetDevice(compilation, deviceId);

HMS_HiAIOptions_SetModelDeviceOrder(
    compilation, &HIAI_EXECUTE_DEVICE_NPU, 1);
OH_NNCompilation_Build(compilation);

auto executor = OH_NNExecutor_Construct(compilation);
```

这里的 `Build` 仍可能完成设备校验、资源准备或缓存恢复，但不像 `.ms` 路线那样由通用推理框架对模型进行 CPU/NNRt 异构子图调度。部署前还应以 `HMS_HiAICompatibility_CheckFromBuffer` 检查 `.om` 是否与目标设备兼容。

### 2. 推理阶段的区别

`.ms` 的推理入口是 `OH_AI_ModelPredict`。应用从 Model 获取输入、输出 tensor，填充符合模型契约的 buffer 后，由 MindSpore Lite 统一调度：

```cpp
auto inputs = OH_AI_ModelGetInputs(model);
// 校验并写入输入 tensor。
auto outputs = OH_AI_ModelGetOutputs(model);
ret = OH_AI_ModelPredict(model, inputs, &outputs, nullptr, nullptr);
```

在这一调用内部，可能是 CPU 全图执行，也可能是“NNRt 子图 + CPU 回退”的异构执行。应用使用同一套 Model API，不应假定模型一定完全下沉到 NPU。

`.om` 的推理入口是 `OH_NNExecutor_RunSync`。应用按 executor 的输入输出描述创建 `NN_Tensor`，然后把 tensor 直接交给已绑定 NPU 的 executor：

```cpp
OH_NNExecutor_RunSync(executor, inputs, inputCount, outputs, outputCount);
```

其执行目标更明确：由这个离线模型所面向的 CANN/NPU 路径完成；应用可进一步配置 AIPP、Profiling 或 Dump。代价是无法自然获得 `.ms` 路线那种通用 CPU 子图回退，机型或系统不兼容时必须在应用层准备另一模型/另一运行路径。

### 3. 不能混用的原因

以下写法是错误的：

```cpp
// 错误：.om 不是 MindIR Lite。
OH_AI_ModelBuild(model, omBuffer, omSize,
                 OH_AI_MODELTYPE_MINDIR, context);

// 错误：.ms 不是 CANN 离线模型。
OH_NNCompilation_ConstructWithOfflineModelBuffer(msBuffer, msSize);
```

前者要求输入是 MindIR Lite 图，后者要求输入是已经针对目标硬件离线编译的模型。两条路线应从同一个 ONNX 上游模型分别转换，并各自维护模型版本、输入输出契约及 golden 测试结果。

### 面试式总结

可以概括为：**`.ms` 是“由推理框架在运行时决定怎么跑”的通用 IR 模型，`.om` 是“已经为目标 NPU 决定好怎么跑”的离线模型。** 前者通过 `OH_AI_ModelBuild` / `OH_AI_ModelPredict` 加载推理，可做 CPU + NNRt 异构与回退；后者通过 `OH_NNCompilation_ConstructWithOfflineModelBuffer` / `OH_NNExecutor_RunSync` 加载推理，换来麒麟 NPU 的更深性能和功耗优化，但牺牲了可移植性。

---

## 问题 3：ONNX 等模型转成 `.ms` 时，转换工具做了什么？经 OMG 转成 `.om` 时，OMG 又做了什么？

两者都不是简单“换后缀”。区别在于：MindSpore Lite Converter 的目标是把不同框架的模型**统一成可由 MindSpore Lite 运行时解释和调度的模型图**；CANN 的 OMG 工具的目标是把模型图进一步**编译成某一类目标 NPU 可直接执行的离线部署产物**。

```text
ONNX / TFLite / Caffe
        │
        ├─ MindSpore Lite Converter
        │  解析、转换、图优化、（可选）量化
        ▼
        .ms：MindIR Lite 图模型
        │
        └─ CANN OMG
           解析、算子适配、图优化与融合、目标 NPU 编译、内存/调度规划
        ▼
        .om：面向目标 NPU 的离线模型
```

### 1. ONNX → `.ms`：MindSpore Lite Converter 做什么

典型命令如下：

```bash
converter_lite \
  --fmk=ONNX \
  --modelFile=your_model.onnx \
  --outputFile=your_model \
  --inputShape="images:1,3,224,224"
```

它的核心工作可概括为：

| 阶段 | Converter 的工作 | 对应用的影响 |
| --- | --- | --- |
| 解析源模型 | 读取 ONNX 的计算图、权重、输入输出、opset、属性、数据类型和量化信息 | 自定义算子、过高 opset、动态 shape 或不支持的属性可能在此失败 |
| 统一图表示 | 将 ONNX 算子和张量语义映射为 MindIR / MindSpore Lite 可识别的算子图 | 产物不再是 ONNX 图，而是 `.ms` 可消费的 MindIR Lite 图 |
| 合法性与推导 | 校验算子参数、连接关系和 shape；在可推导时传播 shape、dtype 和常量 | 应在转换前固定移动端实际使用的输入尺寸，减少动态 shape 风险 |
| 图优化 | 常量折叠、无效节点消除、部分算子规范化/融合等框架级优化 | 降低运行时开销，但不等于已对某一颗 NPU 完成最终编译 |
| 可选压缩 | 根据转换参数或模型条件进行量化等压缩处理 | 量化会影响精度，必须用 golden input/output 回归验证 |
| 序列化模型 | 将优化后的图、权重及必要元数据写为 `.ms` | 运行时用 `OH_AI_ModelBuild(..., OH_AI_MODELTYPE_MINDIR, ...)` 加载 |

所以 `.ms` 仍保留的是**具有计算语义的模型图**。运行时的 MindSpore Lite 可以根据 Context 选择 CPU，也可以把设备支持的子图委托给 NNRt；不支持的部分仍可能回退到 CPU。这也是它更适合多设备部署的原因。

### 2. ONNX → `.om`：OMG 工具做什么

OMG 是 CANN 工具链中的模型转换/离线编译工具。它不只是把 ONNX 翻译成另一种 IR，而是结合指定的目标 SoC/NPU 能力，把模型变成可由 CANN/NNRt 离线模型执行路径加载的产物。

| 阶段 | OMG 的工作 | 对应用的影响 |
| --- | --- | --- |
| 解析与前端转换 | 读取 ONNX 图、权重、输入输出及算子属性，并转为 CANN 内部可处理的图表示 | ONNX 图也必须满足目标 CANN 版本和目标 NPU 的算子限制 |
| 目标硬件算子适配 | 根据目标 NPU 支持集选择算子实现；不支持的算子、动态 shape 或自定义算子需要改图、替换算子或另行处理 | `.om` 的可执行性不是“所有 ONNX 都能转”的保证，而取决于目标硬件能力 |
| 图级优化与算子融合 | 消除冗余、常量折叠、布局调整，尽可能融合可连续执行的算子子图 | 减少中间 tensor、访存和调度开销，是 NPU 性能提升的重要来源 |
| 数据布局与精度决策 | 选择更适合 NPU 的 tensor layout、数据类型和计算实现；可配合 FP16、INT8 等能力 | 预处理和输入 tensor 的 dtype/layout 必须严格匹配转换配置 |
| AIPP 配置（可选） | 将裁剪、缩放、色彩/通道转换、类型转换、补边等图像输入处理配置到 AI 输入预处理路径 | 可减少 CPU 预处理与数据搬运，但输入格式、参数必须与相机/业务链路一致 |
| 离线编译与资源规划 | 为目标 NPU 生成硬件相关的执行描述/指令、内存规划、任务切分和调度信息 | 推理时无需像通用图模型那样从零做完整硬件编译，通常首帧加载更快 |
| 封装与校验信息 | 将编译产物和运行所需元数据封装为 `.om` | 应在部署前用兼容性检查确认模型与实际设备、系统/CANN 能力匹配 |

因此，`.om` 可以理解为“**模型图 + 面向指定 NPU 的编译结果和执行计划**”。应用以 `OH_NNCompilation_ConstructWithOfflineModelBuffer` 加载后，仍要 `Build` 来做兼容性确认、设备绑定和资源准备；但重点不再是通用框架对图做 CPU/NNRt 子图调度，而是执行已离线编译、面向目标 NPU 的模型。

### 3. 最关键的差异

| 维度 | ONNX → `.ms` | ONNX → `.om` |
| --- | --- | --- |
| 工具的角色 | **模型格式转换器 + 框架级图优化器** | **目标 NPU 的离线编译器** |
| 主要产物 | 通用 MindIR Lite 计算图 | 硬件相关的 NPU 部署模型 |
| 硬件信息介入时机 | 主要在端侧运行时由 Context/NNRt 决定 | 转换期已根据目标硬件深度参与优化与编译 |
| 不支持算子的后果 | 可在运行时借助 CPU 处理未下沉部分，具体取决于运行时支持 | 通常在离线转换期暴露，需要改模型/算子或改变部署方案 |
| 适合目标 | 可移植性、CPU 基线、渐进式加速 | 固定机型上的低时延、低功耗与可维测性 |

### 面试式总结

可以回答：**MindSpore Lite Converter 解决的是“把 ONNX 的图语义迁移到 MindIR Lite，并让推理框架以后能灵活调度”的问题；OMG 解决的是“把已验证的模型按指定 NPU 的算子、布局、内存和调度要求提前编译好”的问题。** 因此 `.ms` 更像可移植的运行时输入，`.om` 更像目标硬件的部署二进制。无论走哪条路线，都必须保留 golden 输入输出做精度验证；转换成功只代表模型可被工具接受，不代表预处理、量化和业务结果正确。

---

## 问题 4：目前 HarmonyOS 的 MindSpore 框架有多少算子？与 PyTorch、TensorFlow 相比如何？

先给结论：**没有一个严谨的“MindSpore 有 N 个、鸿蒙 NPU 能跑 N 个”的固定答案。** 算子数随版本和统计口径变化；尤其不能把训练框架的 API 数量，当成某一台手机 NPU 能实际执行的算子数量。

### 1. 先统一统计口径

| 口径 | 含义 | 是否能代表“鸿蒙端侧 NPU 可跑” |
| --- | --- | --- |
| MindSpore `mindspore.ops` | 完整训练/推理框架公开的 Python 算子与函数接口；含别名、梯度、稀疏、通信和框架控制类接口 | 不能。它是框架能力，不等于 Lite，也不等于 NPU |
| MindSpore Lite 算子表 | `.ms` 模型在 Lite Runtime 中可由 CPU/GPU/Kirin NPU/Ascend 等后端执行的算子及 dtype 支持矩阵 | 更接近端侧，但每个后端支持集不同 |
| HarmonyOS NNRt / CANN 设备支持集 | 目标系统版本 + 目标 SoC/NPU 驱动真正可编译、下沉的算子及限制 | **这是部署时唯一有决定意义的口径** |

当前 MindSpore master 的 `mindspore.ops` 文档有约 **600 个编号接口项**；但其中包括别名与高阶/训练相关函数，不能将其直接称为“600 个独立硬件算子”。MindSpore Lite 的官方支持表则按 CPU、Kirin NPU、Mali/Adreno GPU、Ascend 等后端逐项列出，规模是**数百个 Lite 算子**，但没有在文档中承诺一个跨版本、跨后端统一的总数。实际 Kirin NPU 可运行的只是这张表中对应 Kirin NPU 列有支持标记、且数据类型/shape/属性都满足要求的子集。

### 2. 与 PyTorch、TensorFlow 的比较

| 框架/层次 | 可公开引用的数量或规模 | 如何解读 | 与 MindSpore 的关系 |
| --- | --- | --- | --- |
| **MindSpore 完整框架** | `mindspore.ops` 当前文档约 600 个编号接口项 | 包含别名、训练、微分、稀疏及框架接口；不是单纯 kernel 数量 | 面向训练与昇腾/CPU/GPU 的框架层，核心常用视觉/NLP算子覆盖较完整 |
| **MindSpore Lite / HarmonyOS 端侧** | 官方以“数百个”支持项矩阵呈现，按后端和数据类型不同 | 不能给一个脱离设备的精确 NPU 总数；应查目标 SDK 与目标设备支持表 | 端侧模型转换成功后，仍需检查实际下沉比例和 CPU 回退 |
| **PyTorch** | 官方 PyTorch 2.13 文档称内置算子（含相关变体）**超过 3500 个** | 变体、overload、训练/分布式/稀疏等都会显著拉高数字 | 总体生态与算子广度明显更大；端侧的 ExecuTorch 可支持集远小于完整 PyTorch，需另查 backend 支持 |
| **TensorFlow** | `tf.raw_ops` 当前官方低层目录列出约 **1400+** 项 | 低层原语计数，包含资源、队列、数据集、训练和兼容接口；不等同于 TFLite 可部署数 | 完整 TensorFlow 的广度通常大于 MindSpore；但 TFLite/NNAPI 实际可下沉仍是子集 |

> 上述数字不能横向相减得出“谁强多少”。例如 PyTorch 的 3500+ 明确包含 operator variants，TensorFlow 的 `raw_ops` 包含大量低层系统算子；MindSpore 文档还会把同一能力以函数、Primitive、别名分别暴露。真正工程风险不是“总数少多少”，而是**你的模型使用的每一个算子，能否被转换器接受，并在目标后端以目标 shape、dtype 和属性执行**。

### 3. 对 HarmonyOS 部署的实际含义

1. 若模型来自 PyTorch/TensorFlow，先导出 ONNX，再用 Converter 生成 `.ms`；Converter 成功只表示转换链路可接受模型，不能保证其会全图下沉至 NPU。
2. 在 MindSpore Lite 的算子支持表中，逐个检查模型关键算子在 **Kirin NPU** 列是否支持相应 dtype；如有必要，固定 dynamic shape，并规避不支持的算子属性。
3. 真机上记录 NNRt/CANN 编译与执行日志，对比 CPU-only 与 NNRt/CANN 配置的首帧、稳态时延、内存、功耗和精度。若模型被切为多个很小的 NPU 子图，频繁数据搬运可能反而变慢。
4. 始终保留 CPU 回退；若选择 `.om` 路线，则在目标机型部署前完成离线模型兼容性检查和全链路验证。

### 面试式回答

可以这样回答：**MindSpore 完整框架当前公开 `ops` 文档大约是 600 个接口项，MindSpore Lite 则是数百个、按 CPU/GPU/Kirin NPU 等后端分别标注的算子支持矩阵；不能把它说成“鸿蒙 NPU 一定支持 600 个算子”。** 对比之下，PyTorch 官方当前统计的内置算子含变体已超过 3500 个，TensorFlow 的 `tf.raw_ops` 目录约 1400 多项，因此二者在完整训练框架的算子广度和生态上更大。可是在 HarmonyOS 端侧选型时，总数不是关键：应以目标模型的算子、dtype、shape 和目标设备的 NNRt/CANN 支持表为准，实机验证下沉比例与 CPU 回退。

### 资料依据

- [MindSpore `mindspore.ops` API（master）](https://www.mindspore.cn/docs/zh-CN/master/api_python/mindspore.ops.html)
- [MindSpore Lite 硬件后端算子支持矩阵（master）](https://www.mindspore.cn/lite/docs/zh-CN/master/reference/operator_list_lite.html)
- [PyTorch Operator Registration（内置算子 3500+，含变体）](https://docs.pytorch.org/docs/stable/accelerator/operators.html)
- [TensorFlow `tf.raw_ops` 目录](https://www.tensorflow.org/api_docs/python/tf/raw_ops)

---

## 问题 5：MindSpore Lite Kit、NNRt Kit、CANN Kit 分别起什么作用？三者关系是什么？

### 面试回答（推荐表述）

我会把它们理解为 HarmonyOS 端侧 AI 链路中不同层次的能力，而不是三个互相替代的 SDK：

```text
ArkTS 业务/UI
    │ N-API
    ▼
MindSpore Lite Kit：推理框架层，可选
    │ 统一模型、图调度、CPU 回退；把可支持子图交给 NNRt
    ▼
NNRt Kit：系统推理运行时层
    │ 统一设备、模型编译、Tensor、Executor、共享内存接口
    ▼
CANN Kit：麒麟 NPU 的专用计算/优化后端与应用入口
    ▼
Kirin NPU / CPU 等硬件
```

**第一，MindSpore Lite Kit 是应用最常使用的轻量推理框架。** 它负责加载 `.ms` / MindIR Lite 模型、管理输入输出 tensor、执行模型和框架级图调度。应用可先使用 CPU 建立正确性基线；配置 NNRt device 后，Lite 会将硬件支持的连续子图交给 NNRt，不能下沉的部分仍由 CPU 执行。因此它的价值是统一接口、模型可移植性和 CPU 回退。

**第二，NNRt Kit 是系统级的 AI 推理运行时。** 它位于推理框架与具体 NPU/DSP 驱动之间，向上提供设备发现、模型构图/编译、tensor、executor 与共享内存等 Native 能力，向下通过 HDI 对接不同芯片后端。它不负责训练，也不承担通用 CPU 推理；它解决的是“上层框架如何用统一方式调用不同 AI 加速硬件”的问题。普通应用一般通过 MindSpore Lite 间接使用它，或在已有离线模型时直接调用其模型编译和 executor API。

**第三，CANN Kit 是麒麟 NPU 的专用计算和优化能力。** 它在 HarmonyOS 中为 Kirin 平台提供 NPU 后端能力，也可以让应用直接走高性能部署路径：加载 CANN 生成的 `.om` 离线模型，绑定 NPU，创建 executor 并执行。它还提供 AIPP 图像预处理、离线编译、Profiling、Dump、设备顺序和内存等硬件相关调优能力。代价是模型与 CANN/NPU 能力绑定更紧，可移植性低于 `.ms` 路线。

### 三者关系与选型

| 维度 | MindSpore Lite Kit | NNRt Kit | CANN Kit |
| --- | --- | --- | --- |
| 所在层次 | 应用推理框架 | 系统 AI 加速运行时 | 麒麟 NPU 专用计算/优化能力 |
| 主要模型/输入 | `.ms` / MindIR Lite | 在线图或硬件离线模型 | `.om` 离线模型 |
| 主要职责 | 模型加载、推理、图调度、CPU/GPU 执行与回退 | 设备、编译、Tensor、Executor、共享内存、HDI 对接 | NPU 离线编译、执行、AIPP、Profiling/Dump、深度性能功耗优化 |
| 是否直接解决 CPU 推理 | 是 | 否 | 可协同 CPU，但重点是 NPU |
| 可移植性 | 高 | 取决于接入的硬件与驱动 | 低，面向 Kirin NPU |
| 应用开发常见程度 | 最高 | 多数情况下由 Lite 间接使用 | 仅在固定 Kirin 平台且需要深度优化时直接使用 |

可用两条典型链路说明它们的配合：

```text
通用、兼容性优先：
ONNX → Converter → .ms → MindSpore Lite → CPU
                                       └→ NNRt → 可用 AI 加速设备

Kirin NPU、性能功耗优先：
ONNX → OMG → .om → CANN Kit / NNRt 离线模型执行路径 → Kirin NPU
```

所以，MindSpore Lite + NNRt 是“通用模型 + 系统硬件加速”的组合：Lite 使用 MindIR，NNRt 负责将可支持子图下沉到加速设备；CANN 则是 Kirin NPU 这一端的专用高性能能力。应用不需要为了“用 NPU”而同时手写三个 Kit：通常从 MindSpore Lite + CPU 开始，需要硬件加速时加入 NNRt；只有目标机型固定、模型稳定且性能/功耗值得投入时，才直接采用 CANN `.om` 路线。

### 常见追问

**NNRt 能替代 MindSpore Lite 吗？** 不能简单这么说。NNRt 可供应用直接构图或加载离线模型，但它不是提供通用模型生态、Converter、CPU 回退和框架图调度体验的完整推理框架。对普通应用，MindSpore Lite 的接入成本更低。

**CANN 能替代 NNRt 吗？** 不能。CANN 是麒麟平台的专用后端/优化能力；NNRt 是跨芯片的系统运行时抽象。在 Kirin 设备上二者是上下层协作关系，而不是并列竞争关系。

**为什么先跑 CPU？** 因为输入 layout、色彩空间、归一化、量化和输出后处理错误比 NPU API 错误更常见。先用 CPU + golden input/output 锁定正确性，才能在 NNRt 或 CANN 加速后定位是模型问题还是硬件后端问题。

---

## 问题 6：`.om` 模型会用到 NNRt Kit 吗？

**会，但要严格区分“使用 CANN Kit”与“调用哪一套 API”。** 在 HarmonyOS 的 CANN `.om` 部署路径中，应用通常同时链接 `hiai_foundation`（CANN Kit）与 NNRt 运行库：前者提供 CANN/NPU 专属配置、兼容性与版本等能力，后者的 `OH_NN*` 接口承担模型、设备、Tensor 和执行器的通用运行时生命周期。

```text
ONNX
  │ CANN OMG：面向目标 Kirin NPU 做离线编译、融合、布局/内存/调度优化
  ▼
.om
  │ NNRt Native API：加载离线模型、选择设备、Build、创建 Executor、绑定 Tensor
  ▼
NNRt
  │ HDI / Kirin CANN 后端
  ▼
Kirin NPU
```

应用代码中可见的关键 **NNRt** 调用是：

```cpp
// 1. 从 .om 二进制数据构造 NNRt Compilation。
auto compilation =
    OH_NNCompilation_ConstructWithOfflineModelBuffer(omData, omSize);

// 2. 选择 NPU 设备；CANN 专属选项在对应的 hiai_foundation API 中配置。
OH_NNCompilation_SetDevice(compilation, deviceId);

// 3. 构建执行对象，创建/绑定输入输出 Tensor，执行同步推理。
OH_NNCompilation_Build(compilation);
auto executor = OH_NNExecutor_Construct(compilation);
OH_NNExecutor_RunSync(executor, inputs, inputCount, outputs, outputCount);
```

这里需要区分两个含义：

| 说法 | 是否正确 | 原因 |
| --- | --- | --- |
| “`.om` 使用 NNRt 的 Compilation / Executor API 执行。” | 正确 | `.om` 是 NNRt 离线模型执行路径的输入之一，应用通过 NNRt 管理设备、tensor 和执行器 |
| “`.om` 需要 MindSpore Lite + NNRt Delegate。” | 不正确 | 这是 `.ms` / MindIR 的通用框架调度路径，不是 `.om` 的典型加载路径 |
| “`.om` 是由 NNRt 在线构图再编译出来的。” | 不完整 | `.om` 的重点是已由 CANN OMG 针对目标 NPU **离线**编译；NNRt 的 `Build` 主要负责设备绑定、兼容性/资源准备等运行期工作 |
| “CANN 与 NNRt 是两套互斥 API，`.om` 只能选一套。” | 不正确 | CANN 的 `hiai_foundation` API 提供 Kirin NPU 专属能力，NNRt 的 `OH_NN*` API 提供统一运行时生命周期；二者在该路径中协作 |

### 面试式总结

可以说：**`.om` 会用到 NNRt，但不是通过 MindSpore Lite 的 NNRt Delegate。CANN OMG 先把模型离线编译成 `.om`；应用使用 NNRt 的 `Compilation → Executor → Tensor → RunSync` 生命周期装载和执行它，同时按需调用 CANN `hiai_foundation` API 完成 Kirin NPU 的专属配置、兼容性检查或维测。**

### 补充澄清：能否“直接用 CANN Kit 部署模型”？

**能，但“直接”不等于绕过 NNRt。** 正确说法是：可以直接采用 **CANN Kit 的 `.om` 部署路线**，不需要经过 MindSpore Lite；不过 CANN Kit 在 HarmonyOS 应用侧复用了 NNRt 的模型加载和执行接口，并通过 CANN/HiAI 扩展 API 暴露 NPU 专属能力。

```text
可以：ONNX → CANN OMG → .om → NNRt API + CANN 扩展 → Kirin NPU
不需要：ONNX → .ms → MindSpore Lite → NNRt Delegate
不建议说：CANN .om 路线完全不使用 NNRt
```

例如，`OH_NNCompilation_ConstructWithOfflineModelBuffer`、`OH_NNExecutor_RunSync` 是 NNRt 的标准生命周期接口；CANN Kit 的 `hiai_foundation` API 则提供其自身版本、兼容性、模型选项/维测等 Kirin NPU 专属能力。两者共同构成“直接使用 CANN Kit 部署 `.om`”的应用实现。

---

## 问题 7：CANN Kit 中的 API 主要起到什么作用？

先纠正一个容易混淆的点：**`OH_NNCompilation_*`、`OH_NNExecutor_*`、`OH_NNDevice_*` 属于 NNRt Kit，不属于 CANN Kit。** CANN Kit 的 C API 以 `hiai_foundation` 模块为准；它不取代 NNRt 的模型执行生命周期，而是提供面向 Kirin NPU 的芯片能力、模型优化、部署增强与维测能力。

因此，CANN Kit API 的价值不是笼统地说“加载和执行模型”，而是让 `.om` 部署在 NPU 上时具备**兼容性判断、NPU 专属执行/模型选项、AIPP 与硬件优化、版本能力查询和维测调优**等能力。

### 1. 按生命周期理解 API 的作用

| CANN Kit 能力 | 代表 API / 文档模块 | 主要作用 |
| --- | --- | --- |
| 版本与能力识别 | `HMS_HiAI_GetVersion` 等 `hiai_foundation` API | 查询设备上 CANN Kit 版本和可用能力；部署时用它做版本匹配与问题定位 |
| 离线模型兼容性 | CANN Compatibility API | 判断 `.om` 是否可在当前 Kirin SoC、系统和 CANN 版本上运行，避免把不兼容模型送入执行阶段 |
| NPU 专属模型选项 | CANN Model Options API | 配置 CANN/NPU 相关执行参数；具体选项需按目标 SDK 的 `hiai_foundation` 头文件确认 |
| AIPP 与硬件优化 | CANN AIPP、零拷贝、异构/深度融合能力 | 在转换或部署环节利用 NPU/SOC 能力完成图像预处理、减少拷贝、优化跨 IP 数据流与算子融合 |
| 维测与诊断 | CANN Profiling、Dump 等模型选项 | 采集模型执行信息、辅助分析性能或精度问题；仅在调试/专项优化时开启，避免额外开销 |
| 单算子与自定义算子 | CANN 单算子、AscendC 能力 | 将特定算子迁移到 NPU，或开发/优化 NPU 算子，适合框架适配和高性能场景 |

### 2. 标准 NNRt API 与 CANN Kit API 的边界

| 类型 | 代表接口 | 解决的问题 |
| --- | --- | --- |
| NNRt Kit | `OH_NNCompilation_*`、`OH_NNExecutor_*`、`OH_NNDevice_*`、`NN_Tensor` | 以统一方式管理设备、模型、tensor 与执行器，屏蔽不同硬件后端的基础差异 |
| CANN Kit | `hiai_foundation`（如版本、兼容性和模型选项 API）、OMG、AIPP、Profiling、Dump、单算子/AscendC | 将模型面向 Kirin NPU 优化与部署，并开放 NPU 的专属能力和诊断能力 |

因此，CANN Kit API 的核心价值是：**让开发者不仅能“跑起来”，还能确认 `.om` 在目标 Kirin NPU 上可运行、利用硬件优化能力减少预处理与搬运成本，并可观测和调优性能/功耗。**

### 3. 典型调用骨架

```cpp
// rawfile 读出 omData / omSize 后：
// 1. 使用 hiai_foundation 的兼容性/模型选项 API 做 CANN 专属检查与配置。
// 2. 再进入 NNRt 的标准离线模型执行生命周期。
auto compilation =
    OH_NNCompilation_ConstructWithOfflineModelBuffer(omData, omSize);
OH_NNCompilation_SetDevice(compilation, selectedDeviceId);

OH_NNCompilation_Build(compilation);
auto executor = OH_NNExecutor_Construct(compilation);

// 创建并填充与模型契约一致的 NN_Tensor。
OH_NNExecutor_RunSync(executor, inputs, inputCount, outputs, outputCount);

// 退出时按反向顺序销毁 tensor、executor、compilation。
```

### 面试式总结

可以回答：**CANN Kit 的 `hiai_foundation` API 主要开放 Kirin NPU 的专属能力，例如版本/能力识别、`.om` 兼容性检查、模型选项和维测；CANN 工具链还覆盖 OMG 离线编译、AIPP、零拷贝、融合和单算子/AscendC 优化。NNRt 的 `OH_NN*` API 则是另一套系统运行时 API，负责 `.om` 的设备、Compilation、Tensor 和 Executor 生命周期。二者配合，完成高性能部署。**

---

## 问题 8：HarmonyOS 端侧 AI 使用自定义算子时，应该调用哪些 API？

先说结论：**没有一个应用运行时的“`CallCustomOp()`”通用 API。** 要先判断自定义算子是已经编进整图 `.om`，还是由第三方框架以“单算子”方式直调 NPU；两种路径所需 API 完全不同。

### 1. 整图 `.om` 路线：应用不单独调用自定义算子

这是普通 App 最常见的路线。自定义算子在模型转换前已完成定义、实现、编译和注册，OMG 在转换 ONNX/模型图时识别该算子，并把它与其他节点一起编进 `.om`。

```text
自定义算子定义 + AscendC Kernel 实现
  → 编译并注册/部署 NPU 算子库
  → OMG 转换模型，CustomOp 入图并生成 .om
  → App 使用 NNRt 执行整个 .om
```

应用侧调用的是 **NNRt** 的整图执行 API，而不是“自定义算子 API”：

```cpp
auto compilation =
    OH_NNCompilation_ConstructWithOfflineModelBuffer(omData, omSize);
OH_NNCompilation_SetDevice(compilation, deviceId);
OH_NNCompilation_Build(compilation);

auto executor = OH_NNExecutor_Construct(compilation);
OH_NNExecutor_RunSync(executor, inputs, inputCount, outputs, outputCount);
```

`OH_NNExecutor_RunSync()` 运行的是整个 `.om` 图；调度器执行到自定义节点时，会调用已经随模型/算子库部署好的 NPU Kernel。**因此不能、也不需要在 ArkTS 或 N-API 中再手工“调用一次自定义算子”。**

### 2. 算子开发期：要使用 CANN AscendC / GE 的定义、推导和 Kernel API

要让 OMG 和 NPU 认识一个新算子，开发期至少需要以下几类接口；这些是 CANN 的算子开发接口，不是 App 的在线推理 API。

| 目标 | 应使用的 CANN 接口/机制 | 作用 |
| --- | --- | --- |
| 定义算子原型 | `REG_OP` 及其原型定义衍生接口 | 声明算子名、输入、输出、属性和类型/格式约束；ONNX 节点名/属性须能映射到它 |
| 实现 shape / dtype / format 推导 | `IMPLEMT_INFERFUNC`、`INFER_FUNC_REG`；按需要使用 `IMPLEMT_INFERFORMAT_FUNC`、`INFER_FORMAT_FUNC_REG` | 在图编译阶段根据输入和属性推导输出 shape、数据类型与格式；推导错误会导致 OMG 编译或运行失败 |
| 参数合法性校验 | `IMPLEMT_VERIFIER`、`VERIFY_FUNC_REG` | 校验输入维度、属性取值、dtype/format 组合等，在编译期尽早报错 |
| 编写 NPU 核函数 | AscendC Kernel 编程 API，例如 `GlobalTensor`、`LocalTensor`、数据搬运、向量/矩阵计算、同步与 Tiling API | 编写真正运行在 AI Core 上的计算逻辑，以及 host/kernel 的 tiling 数据协定 |
| 编译与部署算子 | CANN 算子工程的编译、打包、算子库部署/动态升级流程 | 让 OMG 和目标设备的 CANN 运行环境能够找到该算子实现；模型转换与设备端算子库版本必须匹配 |
| 将算子放入模型图 | ONNX 框架算子适配 + OMG 离线模型转换 | 将 ONNX 中的 custom node 正确映射为上述算子原型，并把 Kernel 纳入 `.om` |

**正确调用顺序是“定义/实现/编译/入图在前，App 推理在后”，而不是在 App 中临时注册一个 C++ 回调。**

### 3. 单算子直调路线：仅适用于推理框架/高性能 Native 场景

如果你在写推理框架、插件或需要把一段计算独立迁移到 NPU，而不是执行完整 `.om`，才使用 CANN Kit 的“单算子应用”通路。此时需要按**目标 HarmonyOS SDK 的 `hiai_foundation` 头文件**使用单算子的“创建算子 → 设置输入/输出与属性 → 编译/创建执行器 → 执行 → 销毁”API。

这条路线的要点是：

1. 单算子 API 属于 **CANN Kit / `hiai_foundation`**，不要拿 `OH_NNExecutor_RunSync()` 当作“调用某个自定义算子”的 API；后者是 NNRt 的整图 executor API。
2. 自定义 Kernel 仍必须先按上一节完成 AscendC 实现、编译和注册；单算子 API 只能调用设备已识别的算子，不能把任意 C++ 函数下发到 NPU。
3. 单算子 API 的具体函数名和可用版本会随 SDK 演进；实现时以本机 DevEco SDK 中 `hiai_foundation` 的头文件和“单算子应用”文档为唯一准则，不应凭网络样例硬编码旧 API 名。

### 4. 最容易犯的错误

| 错误理解 | 正确做法 |
| --- | --- |
| “把 ONNX 里的 Custom 节点转成 `.om` 后，App 再调用一个自定义算子 API。” | `.om` 已包含该节点，App 只执行整图；自定义节点由图调度执行 |
| “`OH_NN*` 是 CANN 自定义算子 API。” | `OH_NN*` 是 NNRt API；CANN 负责自定义算子开发、模型转换、算子库与单算子能力 |
| “只写 AscendC Kernel 就能转换和运行。” | 还必须定义 `REG_OP` 原型、实现推导/校验、完成 ONNX 适配，并把算子库和模型版本部署匹配 |
| “转换成功就说明自定义算子正确。” | 还要验证 shape/dtype/format、tiling、精度、边界输入、设备算子库版本与性能 |

### 面试式回答

可以这样回答：**如果自定义算子已经通过 AscendC 实现、注册并由 OMG 编进 `.om`，应用侧不直接调它，只需用 NNRt 的 `OH_NNCompilation_*`、`OH_NNExecutor_*` 执行整图。自定义算子本身在开发期要用 CANN 的 `REG_OP` 定义原型、`IMPLEMT_INFERFUNC` / `INFER_FUNC_REG` 做 shape 推导、`IMPLEMT_VERIFIER` / `VERIFY_FUNC_REG` 做校验，并用 AscendC Kernel API 实现 NPU 计算和 Tiling。只有做推理框架的单算子迁移时，才调用 CANN `hiai_foundation` 的单算子创建、配置、执行与销毁接口。**

### 资料依据

- [CANN Kit：单算子推理与应用场景](https://developer.huawei.com/consumer/cn/sdk/cann-kit/)
- [CANN Kit：`IMPLEMT_INFERFUNC`](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides/cannkit-implemt-inferfunc)
- [CANN Kit 自定义算子开发目录](https://developer.huawei.com/consumer/cn/doc/hiai-guides/cannkit-hardware-architecture-0000002300239844)

---

## 问题 9：`.om` 和 `.ms` 文件中有自定义算子的实现吗，还是只有算子声明？

更准确的回答是：**不能把模型文件简单理解为“只保存算子声明”，也不能把它理解为“包含了完整的自定义算子源码/算子包”。** 要区分模型图信息、权重、编译结果和算子 Kernel 实现四个层次。

### 1. `.ms`：模型图和权重，不包含自定义算子 Kernel 实现

`.ms` 是 MindSpore Lite 的 MindIR Lite 模型文件，主要包含：

- 网络拓扑、算子类型、输入输出连接关系；
- 算子属性、shape、dtype、format 等模型元数据；
- 权重和常量数据；
- MindSpore Lite 运行时需要的序列化信息。

它不是一个 C/C++ 动态库，也不包含 AscendC Kernel 的可执行实现。模型里的自定义节点最多是被序列化为一个自定义算子类型及其属性；运行时要么由 MindSpore Lite 注册的自定义算子实现来处理，要么由 Delegate/后端将该节点转换为设备可执行子图。

因此，**把一个自定义节点写入 `.ms` 并不等于把它的实现写入 `.ms`**。如果端侧没有对应的自定义算子注册、Delegate 或后端实现，模型仍然无法执行。

### 2. `.om`：包含面向硬件的模型编译结果，但不等于独立携带完整自定义算子包

`.om` 是 CANN OMG 面向目标 NPU 编译生成的离线模型。相对于 `.ms`，它通常还包含：

- 面向目标 NPU 的图优化和融合结果；
- tensor 的硬件布局、内存规划和任务调度信息；
- 可执行模型所需的离线编译结果；
- 算子节点、属性、输入输出和权重等部署信息。

但是，自定义算子开发文档把“算子工程编译”“算子包安装”“算子部署”和“算子入图/图编译”分别列为独立步骤。由此可见，**自定义 AscendC Kernel 的实现通常作为匹配的算子包/算子库部署，不应认为源码或完整算子实现自动封装在 `.om` 文件中。** `.om` 中会有对该算子的编译后图节点、执行信息或引用关系；运行时仍需要设备侧存在与模型/CANN 版本匹配的算子实现。

这可以类比为：

```text
.ms：图描述 + 权重 + Lite 元数据
.om：面向 NPU 的图描述 + 权重 + 离线编译/调度结果
自定义算子包：算子原型、InferShape/Verifier、Tiling、AscendC Kernel、注册信息
```

### 3. 为什么官方流程要单独“算子包安装/部署”

按照 CANN AscendC 自定义算子开发流程，一个自定义算子通常要经历：

1. 定义算子原型：输入、输出、属性和约束；
2. 实现 InferShape、InferFormat、Verifier 等 Host/图侧逻辑；
3. 用 AscendC 实现 Kernel 侧计算，并实现 Host 侧 Tiling；
4. 编译生成算子包/算子库；
5. 安装或部署算子包，使 OMG、图编译器和设备运行时能够发现它；
6. 做 ONNX/GE 等框架适配，把模型中的节点映射到该算子；
7. 重新执行图编译，生成依赖该算子实现的 `.om`；
8. 在设备侧部署 `.om` 与匹配的算子包/运行库，执行整图。

因此，`.om` 不是脱离算子库就必然自包含的“单文件 App”。模型、算子包、CANN/设备版本需要保持兼容；缺少算子包、算子包版本不匹配或部署路径不正确，都可能在 OMG 转换、Compilation Build 或首次执行阶段失败。

### 4. 对应用运行时的影响

如果自定义算子已经成功入图并生成 `.om`，应用侧仍然只调用整图执行接口，例如 NNRt 的 `OH_NNCompilation_*`、`OH_NNExecutor_*`。应用不会逐个调用自定义 Kernel；运行时根据 `.om` 的图和算子信息，找到已部署的自定义算子实现并执行。

如果走 CANN 单算子路线，则应用/推理框架需要通过 CANN Kit 的单算子接口创建、配置和执行算子，但这仍然依赖已经编译、注册并部署到设备上的 Kernel；单算子 API 也不会把 C++ 源码临时编译成 NPU 算子。

### 面试式回答

可以这样回答：**`.ms` 主要保存 MindIR Lite 的图、权重和算子属性，不携带自定义算子的 AscendC Kernel 实现；`.om` 比 `.ms` 多了面向目标 NPU 的离线编译、布局、内存和调度结果，但自定义算子的 Kernel 通常仍以独立算子包/算子库形式编译和部署。模型文件中有算子节点和编译结果，算子包中才有真正的自定义算子实现。生成 `.om` 前要完成算子包编译、安装和入图，运行时要保证 `.om`、算子包、CANN 和设备版本匹配。**

### 资料依据

- [CANN AscendC 算子开发官方目录](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides/cannkit-ascendc-operator-development)
- [CANN Kit 自定义算子开发流程目录](https://developer.huawei.com/consumer/cn/doc/hiai-guides/cannkit-hardware-architecture-0000002300239844)
