# 第 3 周详细计划：推流链路、编码、封装和 RTMP

本周目标：理解 HJMedia 的推流链路，并用实践掌握音视频编码、时间戳、弱网策略的基本思路。

本周实践重点：
- 画 Pusher 数据链路
- 做 PCM / AAC 参数换算
- 写时间戳递增和音视频交织伪代码
- 模拟弱网队列堆积和丢帧

## Day 15：Pusher Graph 总览

阅读：
- `src/graphs/HJGraphPusher.h`
- `src/graphs/HJGraphPusher.cpp`
- `src/entry/pusher`
- `examples/harmony/API.md`

实践：
- 画出 Pusher 链路：采集 -> 处理 -> 编码 -> mux/interleave -> RTMP
- 标出每个阶段可能运行在哪类线程
- 对比 Pusher 和 Player：数据方向、实时性目标、EOF 语义

产出：
- `studyNote/15-pusher-graph.md`

验收：
- 能讲清楚直播推流端的基本架构

## Day 16：PCM、采样率、AAC 帧实践

阅读方向：
- `src/media` 中 audio / capture / codec 相关实现
- fdk-aac、miniaudio 的使用位置

实践：
- 写计算练习：采样率 44100 / 48000、双声道、16bit PCM 每秒多少字节
- 写伪代码：把任意采集 chunk 拼成 1024 samples 的 AAC 输入帧
- 总结采样率、声道、sample format、frame samples 的关系

产出：
- `studyNote/16-audio-capture-aac.md`
- `studyNote/cpp-practice/pcm-frame-calc.cpp`

验收：
- 能回答 PCM 和 AAC 编码帧大小相关问题

## Day 17：视频编码基础实践

阅读方向：
- `src/media` 中 video capture / codec 相关实现
- Harmony / Android / iOS 平台编码适配代码
- `src/plugins/hsys`、`src/plugins/asys`、`src/plugins/isys` 中相关插件

实践：
- 画出视频帧从采集到编码的路径
- 写表格解释 SPS、PPS、VPS、IDR、PTS、DTS
- 写伪代码：关键帧前附带 codec header 的处理思路

产出：
- `studyNote/17-video-capture-codec.md`

验收：
- 能回答 H.264/H.265 码流基础、关键帧、硬编接口相关问题

## Day 18：Mux、RTMP 和时间戳交织实践

阅读：
- `docs/README_HJOHMuxer.md`
- `src/media/muxer`
- `src/media/net`
- `third_party/librtmp` 使用位置

实践：
- 写音频 / 视频 packet 按 timestamp 交织输出的伪代码
- 模拟音频 20ms 一帧、视频 33ms 一帧的时间戳递增
- 写出 RTMP 发送失败后的重试 / 丢帧 / 断开策略

产出：
- `studyNote/18-rtmp-muxer-timestamp.md`
- `studyNote/cpp-practice/av-interleave-demo.cpp`

验收：
- 能回答 RTMP 推流、音视频交织、时间戳、弱网处理问题

## Day 19：弱网和队列堆积实践

实践：
- 写一个模拟：编码端持续生产 packet，网络端随机变慢
- 观察队列上涨时内存和延迟的问题
- 设计三种策略：阻塞编码、丢低优先级帧、降低码率
- 对比每种策略的优缺点

产出：
- `studyNote/19-weak-network-queue-practice.md`
- `studyNote/cpp-practice/network-backpressure-demo.cpp`

验收：
- 能解释为什么直播推流不能无限缓存

## Day 20：渲染 / 美颜 / AI 检测链路概览

阅读：
- `src/comp/prio`
- `src/comp/rte`
- `src/detect`
- `src/entry/render`
- `src/entry/inference`

实践：
- 画出 GPU 后处理和 AI 检测插入主链路的位置
- 写出“检测结果如何影响渲染处理”的数据流说明
- 不深入 shader，只掌握架构级位置

产出：
- `studyNote/20-render-inference-overview.md`

验收：
- 能说明 GPU 处理和 AI 检测如何插入音视频链路

## Day 21：第三周复盘

实践：
- 写 20 个推流 / 编码 / RTMP 面试问答
- 用 5 分钟文字稿讲清楚直播推流系统
- 整理一个“弱网推流内存上涨”的问题定位案例

产出：
- `studyNote/week3-review.md`

最终验收：
- 能讲清 Pusher 链路
- 能解释 PCM、AAC、H.264/H.265、RTMP 的核心概念
- 能说明弱网推流如何处理实时性和完整性的冲突
