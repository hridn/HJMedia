# 第 1 周详细计划：项目地图、C++ 基础补强和 MusicPlayer 主链路

本周目标：先建立项目地图，再用 MusicPlayer 学会一条最短但完整的音频播放链路。

本周实践重点：
- 写 C++ 小 demo 理解智能指针、RAII、锁、队列
- 给 MusicPlayer 画调用链图
- 给关键类写职责表
- 用伪代码复述播放链路

## Day 1：项目全局地图

阅读：
- `README.md`
- `CHANGELOG.md`
- `CMakeLists.txt`
- `CMakePresets.json`
- `src/graphs/CMakeLists.txt`

实践：
- 手动画一张项目分层图：`entry -> graphs -> plugins/core/media/comp -> third_party/externals`
- 用表格列出每个顶层目录的作用
- 找出 5 个你暂时看不懂但高频出现的关键词，例如 Graph、Plugin、Timeline、Frame、Demuxer

产出：
- `studyNote/01-project-map.md`

验收：
- 能用 2 分钟说明 HJMedia 是什么
- 能说明为什么这个项目用 Graph 和 Plugin 组织音视频链路

## Day 2：C++ 智能指针和对象生命周期实践

阅读：
- `src/utils/HJObject.h`
- 搜索并观察 `HJ_DECLARE_PUWTR`、`Ptr`、`Wtr`、`sharedFrom` 的使用位置

实践：
- 在 `studyNote/cpp-practice/` 下写一个小 demo，模拟 `shared_ptr` / `weak_ptr` 防止循环引用
- 写一段 RAII demo：构造时打印 acquire，析构时打印 release
- 用自己的话解释为什么音视频框架里很少直接暴露裸指针

产出：
- `studyNote/02-cpp-smart-pointer-raii.md`
- `studyNote/cpp-practice/smart-pointer-demo.cpp`

验收：
- 能解释 `shared_ptr`、`weak_ptr`、RAII 在 HJMedia 中解决什么问题

## Day 3：MusicPlayer 文档导读

阅读：
- `docs/Readme_MusicPlayer.md`
- `docs/architecture/HJGraphMusicPlayer_AudioContextGuide.md`
- `docs/architecture/HJGraphMusicPlayer.md`

实践：
- 画出 MusicPlayer 数据链路：`demuxer -> decoder -> resampler -> render`
- 画出控制链路：`openURL / pause / resume / seek / repeat / EOF`
- 用伪代码写出 `openURL -> 播放结束` 的过程

产出：
- `studyNote/03-music-player-architecture.md`

验收：
- 能解释 demuxer EOF 和最终播放 EOF 的区别
- 能解释 timeline 为什么更接近 render 侧进度

## Day 4：HJGraphMusicPlayer 源码入口

阅读：
- `src/graphs/HJGraphMusicPlayer.h`
- `src/graphs/HJGraphMusicPlayer.cpp`
- `src/graphs/HJGraph.h`
- `src/graphs/HJGraph.cpp`

实践：
- 给 `HJGraphMusicPlayer` 每个成员变量写职责说明
- 跟踪 `internalInit()`，列出创建了哪些线程、插件和 timeline
- 跟踪 `openURL()`、`pause()`、`resume()`、`seek()` 的调用路径

产出：
- `studyNote/04-hjgraph-music-player-source.md`

验收：
- 能从源码角度讲清楚 Graph 如何组装音频插件链

## Day 5：队列和生产者消费者实践

阅读：
- `src/plugins/doc/HJPlugin.md`
- `src/plugins/doc/HJMediaFrameDeque.md`

实践：
- 写一个最小生产者 / 消费者队列 demo
- 模拟生产者 push frame，消费者 pop frame
- 加入队列容量限制，观察满队列时应该阻塞、丢弃还是返回错误
- 对照 HJMedia，说明 frame queue 为什么是音视频框架核心组件

产出：
- `studyNote/05-frame-queue-practice.md`
- `studyNote/cpp-practice/frame-queue-demo.cpp`

验收：
- 能回答为什么音视频框架需要队列、反压和异步消费

## Day 6：音频插件链实践复述

阅读：
- `src/plugins/doc/HJTimeline.md`
- `src/plugins/doc/HJPluginDemuxer.md`
- `src/plugins/doc/HJPluginAudioFFDecoder.md`
- `src/plugins/doc/HJPluginAudioResampler.md`
- `src/plugins/doc/HJPluginAudioRender.md`

实践：
- 对每个插件写 5 行以内职责说明
- 写一个表格：输入是什么、输出是什么、失败时可能发生什么
- 用伪代码模拟音频 frame 从 demuxer 传到 render

产出：
- `studyNote/06-audio-plugin-chain.md`

验收：
- 能讲清楚 demux、decode、resample、render 的区别

## Day 7：第一周复盘和面试表达

实践：
- 写 10 个第一周面试问答
- 用 3 分钟文字稿介绍 MusicPlayer
- 写出 3 个你还不懂的问题

产出：
- `studyNote/week1-review.md`

最终验收：
- 能讲清 HJMedia 全局结构
- 能讲清 MusicPlayer 链路
- 能用 C++ 基础解释项目中的智能指针、RAII、队列
