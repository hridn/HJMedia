# 第 1 周学习笔记：项目地图、C++ 基础补强和 MusicPlayer 主链路

对应计划：`study/week1-music-player-practice.md`

## 本周目标

- 建立 HJMedia 项目全局地图
- 理解基础 C++ 概念在项目中的使用：智能指针、RAII、锁、队列
- 掌握 MusicPlayer 主链路：`openURL -> demux -> decode -> resample -> render -> timeline`
- 能用面试语言讲清纯音频播放器的基本架构

## 本周关键问题

- HJMedia 是什么？主要模块如何分层？
- Graph、Plugin、Thread、Timeline 分别负责什么？
- `shared_ptr` / `weak_ptr` / RAII 在项目中解决什么问题？
- demux、decode、resample、render 分别做什么？
- demuxer EOF 和最终播放 EOF 有什么区别？

## Day 1：项目全局地图

### 今日阅读

- [ ] `README.md`
- [ ] `CHANGELOG.md`
- [ ] `CMakeLists.txt`
- [ ] `CMakePresets.json`
- [ ] `src/graphs/CMakeLists.txt`

### 今日实践

- 项目分层图：

```text
entry
  -> graphs
    -> plugins / core / media / comp
      -> third_party / externals
```

- 顶层目录职责表：

| 目录 | 职责 | 我是否理解 |
|---|---|---|
| `src/graphs` |  |  |
| `src/plugins` |  |  |
| `src/core` |  |  |
| `src/media` |  |  |
| `src/comp` |  |  |
| `src/entry` |  |  |

### 今日总结


### 还没懂的问题


## Day 2：C++ 智能指针和对象生命周期

### 今日阅读

- [ ] `src/utils/HJObject.h`
- [ ] 搜索 `HJ_DECLARE_PUWTR`
- [ ] 搜索 `sharedFrom`

### 今日实践

- `shared_ptr` / `weak_ptr` demo 记录：

```text
demo 文件：
验证的问题：
运行结果：
和 HJMedia 的关系：
```

- RAII demo 记录：

```text
demo 文件：
验证的问题：
运行结果：
和 HJMedia 的关系：
```

### 今日总结


### 面试表达


## Day 3：MusicPlayer 文档导读

### 今日阅读

- [ ] `docs/Readme_MusicPlayer.md`
- [ ] `docs/architecture/HJGraphMusicPlayer_AudioContextGuide.md`
- [ ] `docs/architecture/HJGraphMusicPlayer.md`

### 今日实践

- MusicPlayer 数据链路：

```text

```

- MusicPlayer 控制链路：

```text

```

- `openURL -> 播放结束` 伪代码：

```cpp

```

### 今日总结


### 还没懂的问题


## Day 4：HJGraphMusicPlayer 源码入口

### 今日阅读

- [ ] `src/graphs/HJGraphMusicPlayer.h`
- [ ] `src/graphs/HJGraphMusicPlayer.cpp`
- [ ] `src/graphs/HJGraph.h`
- [ ] `src/graphs/HJGraph.cpp`

### 成员变量职责表

| 成员变量 | 职责 | 线程 / 生命周期风险 |
|---|---|---|
| `m_audioInfo` |  |  |
| `m_audioThread` |  |  |
| `m_renderThread` |  |  |
| `m_timeline` |  |  |
| `m_demuxer` |  |  |
| `m_audioDecoder` |  |  |
| `m_audioResampler` |  |  |
| `m_audioRender` |  |  |
| `m_handler` |  |  |
| `m_thread` |  |  |

### 调用链记录

```text
internalInit:

openURL:

pause:

resume:

seek:
```

## Day 5：队列和生产者消费者实践

### 今日阅读

- [ ] `src/plugins/doc/HJPlugin.md`
- [ ] `src/plugins/doc/HJMediaFrameDeque.md`

### 今日实践

```text
demo 文件：
验证的问题：
是否有容量限制：
满队列时如何处理：
和 HJMedia frame queue 的关系：
```

### 今日总结


## Day 6：音频插件链实践复述

### 今日阅读

- [ ] `src/plugins/doc/HJTimeline.md`
- [ ] `src/plugins/doc/HJPluginDemuxer.md`
- [ ] `src/plugins/doc/HJPluginAudioFFDecoder.md`
- [ ] `src/plugins/doc/HJPluginAudioResampler.md`
- [ ] `src/plugins/doc/HJPluginAudioRender.md`

### 插件职责表

| 插件 | 输入 | 输出 | 职责 | 可能失败点 |
|---|---|---|---|---|
| Demuxer |  |  |  |  |
| Audio Decoder |  |  |  |  |
| Resampler |  |  |  |  |
| Audio Render |  |  |  |  |
| Timeline |  |  |  |  |

## Day 7：本周复盘

### 10 个面试问答

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

### 3 分钟 MusicPlayer 介绍稿


### 本周最重要的 5 个收获

1. 
2. 
3. 
4. 
5. 

### 下周要补的问题

1. 
2. 
3. 
