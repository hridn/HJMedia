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

- [x] `src/media/capture/hsys/HJACaptureOH.cc`
- [x] `src/media/codec/HJAEncFDKAAC.cc`
- [x] `src/plugins/hsys/HJPluginAudioOHCapturer.cpp`
- [x] `src/plugins/HJPluginFDKAACEncoder.cpp`
- [x] `src/media/render/HJARenderMini.cc`

详细笔记：`studyNote/16-audio-capture-aac.md`
练习 demo：`studyDemo/day16_pcm_aac_frame_calc.cpp`

### PCM 参数换算

```text
采样率：48000
声道数：2
位深：16bit / 2 bytes
每秒字节数：48000 * 2 * 2 = 192000
每 10ms 字节数：1920
每 20ms 字节数：3840
AAC-LC 1024 samples/channel 输入字节数：1024 * 2 * 2 = 4096
FDK-AAC numInSamples：4096 / 2 = 2048
```

### AAC 拼帧伪代码

```cpp
bufferedSamplesPerChannel += captureChunk.samplesPerChannel;

while (bufferedSamplesPerChannel >= 1024) {
    inputBytes = 1024 * channels * bytesPerSample;
    fdkaacNumInSamples = inputBytes / bytesPerSample;
    encodeOneAacFrame(inputBytes, fdkaacNumInSamples, pts);
    bufferedSamplesPerChannel -= 1024;
    pts += 1024 * 1000.0 / sampleRate;
}
```

### 今日总结

PCM 是编码前的原始样本，大小由采样率、声道数和 sample format 决定；AAC packet 是编码后的压缩结果，大小不等于固定的 PCM 输入字节数。HJMedia 的 Harmony 采集路径用 `HJACaptureOH::OnReadData` 产出 S16 PCM AVFrame，`HJAEncFDKAAC::run` 再把输入字节数换成 FDK-AAC 的 `numInSamples`。

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
