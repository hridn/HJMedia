# 第 3 周学习笔记：推流链路、编码、封装和 RTMP

对应计划：`study/week3-pusher-codec-rtmp-practice.md`

## 本周目标

- 理解 HJMedia 推流链路
- 掌握 PCM、AAC、H.264/H.265、RTMP 的基础概念
- 理解 mux、时间戳交织、弱网重连和丢帧
- 能讲清直播推流端为什么更重视实时性

## 本周关键问题

- Pusher 和 Player 的链路有什么对称关系？
- PCM 如何变成 AAC？
- 视频帧如何变成 H.264/H.265 码流？
- mux 为什么要处理时间戳和音视频交织？
- 弱网下为什么不能无限缓存？

## Day 15：Pusher Graph 总览

### 今日阅读

- [ ] `src/graphs/HJGraphPusher.h`
- [ ] `src/graphs/HJGraphPusher.cpp`
- [ ] `src/entry/pusher`
- [ ] `examples/harmony/API.md`

### Pusher 链路图

```text
capture -> process -> encode -> mux/interleave -> RTMP
```

### Pusher / Player 对比

| 维度 | Pusher | Player |
|---|---|---|
| 数据方向 |  |  |
| 核心目标 |  |  |
| 实时性要求 |  |  |
| 队列风险 |  |  |
| EOF 语义 |  |  |

## Day 16：PCM、采样率、AAC 帧实践

### 今日阅读

- [ ] `src/media` 中 audio / capture / codec 相关实现
- [ ] fdk-aac 使用位置
- [ ] miniaudio 使用位置

### PCM 参数换算

```text
采样率：
声道数：
位深：
每秒字节数：
每 10ms 字节数：
每 20ms 字节数：
```

### AAC 拼帧伪代码

```cpp

```

### 今日总结


## Day 17：视频编码基础实践

### 今日阅读

- [ ] `src/media` 中 video capture / codec 相关实现
- [ ] Harmony / Android / iOS 平台编码适配代码
- [ ] `src/plugins/hsys`
- [ ] `src/plugins/asys`
- [ ] `src/plugins/isys`

### 概念表

| 概念 | 我的理解 | 在编码链路中的作用 |
|---|---|---|
| SPS |  |  |
| PPS |  |  |
| VPS |  |  |
| IDR |  |  |
| PTS |  |  |
| DTS |  |  |

### 关键帧处理伪代码

```cpp

```

## Day 18：Mux、RTMP 和时间戳交织实践

### 今日阅读

- [ ] `docs/README_HJOHMuxer.md`
- [ ] `src/media/muxer`
- [ ] `src/media/net`
- [ ] `third_party/librtmp` 使用位置

### 音视频交织伪代码

```cpp

```

### 时间戳模拟

| 类型 | 间隔 | 示例时间戳 |
|---|---|---|
| Audio | 20ms |  |
| Video | 33ms |  |

### RTMP 失败策略

```text
发送失败：
重试条件：
丢帧条件：
断开条件：
恢复条件：
```

## Day 19：弱网和队列堆积实践

### 今日实践

```text
network backpressure demo 文件：
模拟的问题：
生产速度：
网络发送速度：
队列最大容量：
是否丢帧：
观察结果：
```

### 策略对比

| 策略 | 优点 | 缺点 | 适合场景 |
|---|---|---|---|
| 阻塞编码 |  |  |  |
| 丢低优先级帧 |  |  |  |
| 降低码率 |  |  |  |
| 断线重连 |  |  |  |

## Day 20：渲染 / 美颜 / AI 检测链路概览

### 今日阅读

- [ ] `src/comp/prio`
- [ ] `src/comp/rte`
- [ ] `src/detect`
- [ ] `src/entry/render`
- [ ] `src/entry/inference`

### 插入位置图

```text

```

### 今日总结


## Day 21：本周复盘

### 20 个面试问答

1. 
2. 
3. 
4. 
5. 
6. 
7. 
8. 
9. 
10. 
11. 
12. 
13. 
14. 
15. 
16. 
17. 
18. 
19. 
20. 

### 5 分钟直播推流系统介绍稿


### 弱网推流内存上涨定位案例

```text
现象：
可疑模块：
源码入口：
日志点：
可能原因：
修复思路：
风险：
```
