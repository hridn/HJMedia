# MusicPlayer 文档

## 目的
本文档是 `HJGraphMusicPlayer` 文档集的入口。

它面向三类读者：

1. LLM / 编码代理  
目的：在修改 graph、plugin 或 wrapper 代码之前，先建立稳定上下文。

2. 维护者  
目的：在一段时间没有接触该模块后，快速恢复对架构、线程模型、音频链路和生命周期语义的理解。

3. 其他读者  
目的：理解 music player 做什么、依赖什么，以及下一步应该阅读哪里。

## 这个模块是什么
`HJGraphMusicPlayer` 是 `hjmedia` 中的纯音频播放器 graph。

它的主要职责是：
- 组装音频播放链路
- 暴露播放控制 API
- 维护播放 timeline 的访问能力
- 在 graph 层协调 repeat、seek 和最终 EOF 行为

核心实现：
- [HJGraphMusicPlayer.h](/f:/Source/hjmedia/src/graphs/HJGraphMusicPlayer.h)
- [HJGraphMusicPlayer.cpp](/f:/Source/hjmedia/src/graphs/HJGraphMusicPlayer.cpp)

详细架构文档：
- [HJGraphMusicPlayer.md](/f:/Source/hjmedia/docs/architecture/HJGraphMusicPlayer.md)

## 核心音频链路
主播放链路是：

`demuxer -> audio decoder -> audio resampler -> audio render`

关键模块文档：
- [HJPluginDemuxer.md](/f:/Source/hjmedia/src/plugins/doc/HJPluginDemuxer.md)
- [HJPluginAudioFFDecoder.md](/f:/Source/hjmedia/src/plugins/doc/HJPluginAudioFFDecoder.md)
- [HJPluginAudioResampler.md](/f:/Source/hjmedia/src/plugins/doc/HJPluginAudioResampler.md)
- [HJPluginAudioRender.md](/f:/Source/hjmedia/src/plugins/doc/HJPluginAudioRender.md)
- [HJTimeline.md](/f:/Source/hjmedia/src/plugins/doc/HJTimeline.md)

## 线程前置知识
这个播放器高度依赖 `HJThread` 子系统。

在处理 seek、teardown、handler 投递、延迟任务，或 render/control 线程协作之前，先阅读这些文档：
- [HJThread README](/f:/Source/hjmedia/src/utils/HJThread/doc/README.md)
- [HJLooperThread.md](/f:/Source/hjmedia/src/utils/HJThread/doc/HJLooperThread.md)
- [HJLooper.md](/f:/Source/hjmedia/src/utils/HJThread/doc/HJLooper.md)
- [HJHandler.md](/f:/Source/hjmedia/src/utils/HJThread/doc/HJHandler.md)
- [HJMessageQueue.md](/f:/Source/hjmedia/src/utils/HJThread/doc/HJMessageQueue.md)
- [HJMessage.md](/f:/Source/hjmedia/src/utils/HJThread/doc/HJMessage.md)

## 推荐阅读顺序
如果这是第一次阅读 music player，建议按以下顺序：

1. [MusicPlayer Audio Context Guide](/f:/Source/hjmedia/docs/architecture/HJGraphMusicPlayer_AudioContextGuide.md)
2. [HJGraphMusicPlayer.md](/f:/Source/hjmedia/docs/architecture/HJGraphMusicPlayer.md)
3. [HJThread README](/f:/Source/hjmedia/src/utils/HJThread/doc/README.md)
4. [HJTimeline.md](/f:/Source/hjmedia/src/plugins/doc/HJTimeline.md)
5. [HJPluginDemuxer.md](/f:/Source/hjmedia/src/plugins/doc/HJPluginDemuxer.md)
6. [HJPluginAudioFFDecoder.md](/f:/Source/hjmedia/src/plugins/doc/HJPluginAudioFFDecoder.md)
7. [HJPluginAudioResampler.md](/f:/Source/hjmedia/src/plugins/doc/HJPluginAudioResampler.md)
8. [HJPluginAudioRender.md](/f:/Source/hjmedia/src/plugins/doc/HJPluginAudioRender.md)
9. [HJGraphMusicPlayer.cpp](/f:/Source/hjmedia/src/graphs/HJGraphMusicPlayer.cpp)

## Wrapper 层
相关 wrapper / bridge 代码：
- [HJMusicPlayerJni.cpp](/f:/Source/hjmedia/src/entry/player/asys/HJMusicPlayerJni.cpp)
- [HJMusicPlayer.h](/f:/Source/hjmedia/src/entry/player/isys/HJMusicPlayer.h)
- [HJMusicPlayer.mm](/f:/Source/hjmedia/src/entry/player/isys/HJMusicPlayer.mm)

## 适合 Codex 的任务
适合优先处理的任务：
- 总结线程所有权和控制流
- 追踪 `openURL -> demux -> decode -> resample -> render`
- 审计 seek / repeat / EOF 语义
- 复查 pause/resume/close/done 等生命周期边界
- 生成回归检查清单或设计说明

避免一上来就做：
- 修改最终 EOF 语义
- 修改 timeline 所有权
- 修改 render 回调生命周期
- 在没有先审计 wrappers 和 graph teardown 的情况下，重新定义 `close()` / stop 语义
