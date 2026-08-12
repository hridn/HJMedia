# HarmonyOS AI 三层能力资料：MindSpore Lite、NNRt 与 CANN Kit

> 资料收集日期：2026-08-12。内容依据华为开发者联盟公开文档整理；功能、版本支持范围会随 HarmonyOS 版本变化，实施前请复核文末官方页面。

## 概览与选型

HarmonyOS 的这三项 AI 能力大致从上至下组成一条推理与硬件加速链路：

```text
应用 / AI 推理框架
        │
        ├── MindSpore Lite Kit：统一、轻量的端侧模型推理框架
        │          │（可直接使用 CPU/GPU，也可配置 NNRt 后端）
        ▼
Neural Network Runtime Kit（NNRt）：跨芯片 AI 推理运行时
        ▼
CANN Kit：麒麟平台 NPU 的异构计算框架、驱动与优化计算库
        ▼
CPU / NPU 等硬件
```

| 需要解决的问题 | 建议优先了解/使用 |
| --- | --- |
| 在应用中快速部署通用端侧模型 | MindSpore Lite Kit |
| 推理框架需要接入不同 AI 加速硬件，或应用要直连 NPU | Neural Network Runtime Kit |
| 针对 Kirin NPU 做离线模型、AIPP、算子或性能功耗深度优化 | CANN Kit |

## 1. MindSpore Lite Kit（昇思推理框架服务）

### 定位与适用场景

MindSpore Lite 是 HarmonyOS 内置的轻量化 AI 引擎，面向多处理器架构提供端到端的模型部署与推理能力。典型应用包括图像分类、目标检测/识别、图像分割、人脸识别和文字识别。

### 主要能力

- 支持 CPU 与 NNRt 专用芯片的高性能推理；能够借助内核算法和汇编级优化降低时延、功耗。
- 支持模型量化压缩，以及 MindSpore、TensorFlow Lite、Caffe、ONNX 模型向 `.ms` 模型转换。
- 通过统一的训练/推理 IR 支持快速部署；可在多种操作系统、嵌入式系统与智能设备上运行。
- 支持 ArkTS API：直接在 UI 代码中加载模型并推理，适合快速验证。
- 支持 Native API：将模型与 Native 调用封装为动态库，再借助 N-API 暴露 ArkTS 接口，适合工程化集成。

### 基本流程

1. 将第三方模型转换为 MindSpore Lite 使用的 `.ms` 格式。
2. 创建推理/训练上下文，选择硬件并设置线程数等参数。
3. 加载模型，准备输入数据，执行推理/训练并读取输出。

### 限制

- 面向 Phone、Tablet、PC/2in1、TV 和 Wearable；Wearable 仅支持 CPU 推理。
- 支持模拟器，但模拟器不支持 NPU 后端。

### 与 NNRt 的连接

MindSpore Lite 的 Native 接口可配置 NNRt 后端。两者共享 MindIR 模型图格式，因此接入 NNRt 时无需构图；同时也可在 CPU/GPU 与 NNRt AI 加速硬件间进行异构推理。

官方资料：[MindSpore Lite Kit 简介](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides/mindspore-lite-kit-introduction) ｜ [推理模型转换](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides/mindspore-lite-converter-guidelines) ｜ [ArkTS 开发方式](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides/mindspore-guidelines-based-js) ｜ [Native 开发方式](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides/mindspore-guidelines-based-native)

## 2. Neural Network Runtime Kit（Neural Network 运行时服务，NNRt）

### 定位与使用对象

NNRt 是连接上层 AI 推理框架和底层加速芯片的跨芯片推理计算运行时。它的 Native 接口主要面向两类开发者：AI 推理框架开发者，以及需要直接使用 AI 加速硬件进行推理的应用开发者。

### 核心模块与能力

- **在线构图**：将推理框架的模型图转换为 NNRt 内部模型图；MindSpore Lite 因 MindIR 格式兼容无需此步骤。
- **模型编译与缓存**：将内部模型图或离线模型编译为硬件相关模型对象；可以保存并复用缓存以显著提升后续加载/编译速度。
- **模型推理**：创建执行器、设置输入输出张量并在 AI 硬件上执行。
- **内存与设备管理**：申请驱动共享内存，实现输入输出零拷贝；查询和选择已接入的 AI 硬件。
- **离线模型推理**：可直接加载特定硬件的离线模型，首次加载/编译通常更快，但只能在对应硬件运行。
- **硬件属性配置**：支持优先级、性能模式、FP16 等通用属性；特定硬件属性通过自定义扩展属性配置。

### 边界与限制

- 仅提供已接入 AI 加速硬件的推理能力，不负责 CPU 等通用硬件推理。
- 当前官方说明列出常用算子 56 个；算子实现位于具体 AI 硬件驱动中。
- 目前仅同步推理；不支持多线程并发构图。编译与执行能否并发取决于底层驱动。
- 强依赖 NPU，只适用于支持 NPU 的设备；不支持模拟器。

官方资料：[NNRt 简介](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides/neural-network-runtime-kit-introduction) ｜ [NNRt 对接 AI 推理框架开发指导](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides/neural-network-runtime-guidelines) ｜ [Native C API：neural_network_runtime.h](https://developer.huawei.com/consumer/cn/doc/harmonyos-references/capi-neural-network-runtime-h)

## 3. CANN Kit（CANN 异构计算框架服务）

### 定位

CANN（Compute Architecture for Neural Networks）是华为面向 AI 的端云一致异构计算架构。HarmonyOS 的 CANN Kit 面向 Kirin 芯片平台，为 AI 模型与算法提供统一接入和运行环境，并协同调度 NPU、CPU 等资源。在这条链路中，CANN Kit 作为麒麟平台后端接入 NNRt。

它构建在底层硬件驱动和优化计算库之上，面向达芬奇架构 NPU 计算核心；与云侧昇腾芯片统一支持 AscendC 自定义算子语言和工具链，适合计算负载重、需要深入优化性能及功耗的场景。

### 主要能力

- **模型优化**：Model Zoo、模型轻量化与量化蒸馏等。
- **模型转换**：使用 OMG 工具将模型离线编译为硬件专用指令与数据布局；可借助 AIPP 完成硬件图像预处理。
- **端侧部署**：模型编译/推理、动态 AIPP、NPU/CPU 异构子图拆分与调度、ION 内存零拷贝、硬件深度融合。
- **单算子调用**：第三方框架在模型加载和推理过程按算子创建执行器并执行计算。
- **AscendC 自定义算子**：基于 C/C++ 规范，提供多层接口抽象、自动并行计算和孪生调试等能力，以支持算子开发、模型调优与部署。

### 关键概念

- **NPU**：专用的深度学习计算芯片。
- **异构计算**：使用 CPU、NPU 等不同计算单元协同完成计算。
- **AIPP**：AI 输入预处理，可完成裁剪、通道交换、色域转换、缩放、类型转换、旋转、补边等；利用硬件预处理改善推理性能。
- **离线模型**：经硬件相关转换得到的模型，通常加载快，但硬件可移植性较低。

### 限制

- 仅适用于带 Kirin NPU 的 Phone、Tablet、PC/2in1、TV；TV 自 5.1.1(19) 起新增支持。
- 不支持模拟器。

官方资料：[CANN Kit 简介](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides/cannkit-introduction) ｜ [模型推理](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides/cannkit-model-inference) ｜ [模型转换参数](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides/cannkit-overall-parameter) ｜ [昇腾 CANN 开发平台](https://www.hiascend.com/software/cann)

## 三者关系的实践结论

1. 通用应用优先从 MindSpore Lite 开始：它负责模型加载、输入输出和推理框架级体验，适合追求开发效率与多设备部署。
2. 需要跨 AI 加速硬件抽象或直接控制硬件推理时使用 NNRt：在线构图可获得跨硬件兼容性；硬件离线模型则以可移植性换取更快的首次加载。
3. 需要为 Kirin NPU 做深度定制时使用 CANN Kit：例如 AIPP、离线模型、AscendC 自定义算子、NPU/CPU 异构调度和零拷贝。
4. MindSpore Lite + NNRt 是更高效的组合之一：二者共享 MindIR，因此少了一次构图转换；CANN Kit 则为 Kirin 平台的 NNRt 后端提供底层计算能力。

## 来源与更新记录

| 页面 | 官方标注更新时间（采集时页面显示） |
| --- | --- |
| MindSpore Lite Kit 简介 | 2026-07-28 11:23 |
| Neural Network Runtime Kit 简介 | 2026-03-12 02:57 |
| CANN Kit 简介 | 页面未显示具体更新时间 |

