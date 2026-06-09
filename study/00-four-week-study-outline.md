# HJMedia 四周学习计划大纲：面向 C++ 音视频开发求职

目标：用 4 周时间把 HJMedia 学成一个能支撑 C++ 音视频开发求职的项目案例。

你是 C++ 初学者，所以学习方式不能只读文档和源码。每天都按这个闭环执行：

```text
阅读 -> 画链路 -> 做实践 -> 写笔记 -> 用面试语言复述
```

## 最终产出

- HJMedia 项目架构图
- MusicPlayer 播放链路源码导读
- Player / Pusher 主链路对比笔记
- 线程、插件、队列、seek、EOF 专题笔记
- 5 个实践型问题定位案例
- 一份简历项目描述
- 一套 C++ 音视频面试问答

## 实践分级

### Level 1：只读实践

适合第 1 周前半段。

- 画模块图
- 追函数调用链
- 给关键函数写伪代码
- 标注成员变量职责
- 写“如果我是作者，为什么这么设计”

### Level 2：轻量代码实践

适合第 1 周后半段和第 2 周。

- 增加日志点
- 增加状态打印
- 写最小 C++ demo 验证智能指针、锁、队列、线程
- 写伪 frame queue 模拟生产者 / 消费者
- 在不改业务语义的情况下整理一段调用链说明

### Level 3：问题定位实践

适合第 2 周后半段到第 4 周。

- 模拟 seek 后旧帧问题
- 模拟 close 后旧任务回调问题
- 模拟队列堆积和丢帧策略
- 模拟 EOF 状态机
- 写日志排查方案和修复风险分析

## 四周安排

### 第 1 周：项目地图、C++ 基础补强和 MusicPlayer 主链路

重点：
- 项目结构
- CMake 和目录组织
- C++ 智能指针、RAII、锁、队列基础
- MusicPlayer 的 `demux -> decode -> resample -> render`

实践：
- 写 C++ 小 demo 验证 `shared_ptr` / `weak_ptr`
- 写简单生产者 / 消费者队列
- 给 MusicPlayer 画调用链图
- 给 `HJGraphMusicPlayer` 成员变量写职责表

详细计划：
- `study/week1-music-player-practice.md`

### 第 2 周：线程模型、插件系统和播放器扩展

重点：
- `HJThread` / `HJHandler` / `HJMessageQueue`
- Plugin 生命周期
- flush、seek、EOF、teardown 风险
- LivePlayer / VodPlayer / MusicPlayer 对比

实践：
- 写 handler / task queue 模拟 demo
- 设计 seek 日志点
- 模拟 delayed task 在 close 后触发的问题
- 追踪一个 frame 从插件输入到输出的路径

详细计划：
- `study/week2-thread-plugin-player-practice.md`

### 第 3 周：推流链路、编码、封装和 RTMP

重点：
- Pusher Graph
- PCM、AAC、H.264/H.265
- mux、时间戳、RTMP、弱网策略
- 推流实时性和丢帧

实践：
- 写 PCM 参数换算练习
- 写时间戳递增和交织伪代码
- 模拟弱网队列堆积和丢帧策略
- 画 Pusher 数据链路和控制链路

详细计划：
- `study/week3-pusher-codec-rtmp-practice.md`

### 第 4 周：项目实战表达、问题定位和求职材料

重点：
- 简历项目描述
- 面试问答
- 源码讲解
- 问题定位案例

实践：
- 做 5 个问题定位 playbook
- 做 10 分钟源码讲解稿
- 做 3 分钟项目介绍录音或文字稿
- 整理可写进简历的项目经历

详细计划：
- `study/week4-interview-job-ready-practice.md`

## 每日笔记模板

```text
日期：
今日阅读：
今日实践：
实践结果：
关键类 / 函数：
我理解的数据流：
我理解的控制流：
线程 / 锁 / 生命周期风险：
可用于面试的一句话：
还没懂的问题：
```

## 学习原则

- 不只读源码，每天必须有实践。
- 不追求一次读懂全部，优先掌握主链路。
- 每个实践都要能回答：我验证了什么？结果是什么？和项目源码有什么关系？
- 不盲目改复杂业务逻辑，初期优先做日志、模拟、伪代码和小 demo。
- 第 4 周必须把学习结果转化为简历和面试表达。
