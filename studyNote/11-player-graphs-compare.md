# Day 11：LivePlayer / VodPlayer / MusicPlayer 对比

对应计划：`study/week2-thread-plugin-player-practice.md`

## 今日目标

- 读懂 `HJGraphLivePlayer`、`HJGraphVodPlayer`、`HJGraphMusicPlayer` 的主链路差异。
- 明确三类播放器在 seek、低延迟、追帧、EOF、repeat、API 暴露上的取舍。
- 用 `studyDemo/day11_player_graph_compare.cpp` 模拟三个 Graph 的结构和反压策略。

## 阅读清单

- [x] `src/graphs/HJGraphLivePlayer.h`
- [x] `src/graphs/HJGraphLivePlayer.cpp`
- [x] `src/graphs/HJGraphVodPlayer.h`
- [x] `src/graphs/HJGraphVodPlayer.cpp`
- [x] `src/graphs/HJGraphMusicPlayer.h`
- [x] `src/graphs/HJGraphMusicPlayer.cpp`

## 一句话区别

`LivePlayer` 的核心目标是低延迟和持续播放，卡住时更倾向于 reset、追帧、丢掉过期数据；`VodPlayer` 的核心目标是完整播放一个可定位媒体，必须支持 pause、seek、进度和最终 EOF；`MusicPlayer` 是纯音频 Graph，链路更短，但在 repeat、音轨、最终 EOF 状态上比普通点播音频更细。

## Graph 结构对比

| Graph | 场景 | 音频链路 | 视频链路 | 关键插件 |
|---|---|---|---|---|
| LivePlayer | 直播音视频流 | demuxer -> dropping -> audioDecoder -> audioResampler -> speedControl -> audioRender | demuxer -> dropping -> videoDecoder -> videoMainRender | `HJPluginAVDropping`、`HJPluginSpeedControl`、首帧/卡顿统计 |
| VodPlayer | 点播音视频 | demuxer -> audioDecoder -> audioResampler -> audioRender | demuxer -> videoDecoder -> videoRender | timeline、seek handler、repeat EOF |
| MusicPlayer | 纯音频播放 | demuxer -> audioDecoder -> audioResampler -> audioRender | 无 | audio-only timeline、音轨查询、repeat/final EOF 状态 |

LivePlayer 的最大结构差异是 demuxer 后面先接 `HJPluginAVDropping`。这说明直播播放不是简单把所有帧完整送到解码器，而是在 demux 后先做音视频缓存和追帧控制。

VodPlayer 和 MusicPlayer 都是 demuxer 直接把数据送到解码链路，没有显式 dropping 插件。它们更重视“按时间线完整消费”，队列满时主要让上游 demuxer 反压等待，而不是主动丢过期帧。

## API 对比

共同能力：

| API | 含义 |
|---|---|
| `openURL()` / `close()` | 打开或关闭媒体源 |
| `setMute()` / `isMuted()` | 音频静音控制 |
| `setVolume()` / `getVolume()` | 音量控制 |
| `getDuration()` | 从 demuxer 查询媒体时长 |
| `switchAudioTrack()` | 切换音轨 |

差异能力：

| 能力 | LivePlayer | VodPlayer | MusicPlayer |
|---|---|---|---|
| `pause()` / `resume()` | 无公开实现 | 有 | 有 |
| `seek()` | 无 | 有，投递到 handler 后调用 demuxer seek | 有，投递到 handler 后调用 demuxer seek |
| `getCurrentTimestamp()` | 无 | 有，从 timeline 读当前时间 | 有，从 timeline 读当前时间 |
| 低延迟追帧 | 强需求 | 弱需求 | 无视频追帧 |
| `setEnableSEIUUids()` | 有 | 无 | 无 |
| 音轨展示信息 | 无 | 无 | `getAudioTrackIds()` / `getAudioTrackDisplayInfos()` |
| repeat 控制 | 直播语义不适用 | init 参数里有 repeats | `setRepeats()` 可运行时调整 |
| 录制 | 无 | 无 | 临时支持 `openRecorder()` / `closeRecorder()` |

## 反压策略

### LivePlayer

源码里 LivePlayer 的反压阈值更激进：

```text
drops audio output when audioDecoder + audioRender > 300ms
drops video output when videoDecoder + videoRender > 30 frames
videoDecoder can deliver only when videoMainRender < 2 frames
```

这说明直播播放器优先控制播放端延迟。若缓存堆积太多，即使继续解码每一帧也只会越播越慢，所以需要让 dropping 或 decoder 被反压，并通过丢过期帧追上实时位置。

### VodPlayer

VodPlayer 的 demuxer 直接受下游缓存控制：

```text
audioDecoder + audioRender <= 600ms
videoDecoder + videoRender <= 30 frames
videoRender < 2 frames 时唤醒 videoDecoder
```

它仍然要防止无限堆积，但阈值比 LivePlayer 的音频 dropping 更宽松。点播通常可以短暂缓存，不能随便丢掉用户期望看到的内容。

### MusicPlayer

MusicPlayer 只有音频反压：

```text
audioDecoder + audioRender <= 600ms
```

没有视频队列和追帧问题，核心是保证音频 render、timeline、repeat 和 EOF 状态正确。

## EOF 语义

| Graph | EOF 处理 |
|---|---|
| LivePlayer | demuxer EOF 或 codec/network 错误时倾向于 `demuxer->reset(...)`，Graph 不轻易把直播会话视为最终结束 |
| VodPlayer | demuxer EOF 后记录最后 stream index；audioRender/videoRender 对应流都结束后报告 `EVENT_GRAPH_EOF_ID` |
| MusicPlayer | demuxer EOF 后根据 repeats 决定 reset 还是标记 final EOF；最终由 audioRender EOF 确认播放完成并报告 Graph EOF |

这里的关键是：demuxer 读完不等于“用户听到/看到的内容播放完”。下游 decoder、resampler、render 可能还有缓存帧，所以最终 EOF 应该由 render 侧确认。MusicPlayer 为了支持 repeat 和运行时 `setRepeats()`，还额外维护了 `m_pendingDemuxerFinalEof`、`m_audioPlaybackCompleted`、`m_maxTimestamp` 这类播放状态。

## 直播卡顿排查思路

1. 先确认是网络/demuxer 输入不足，还是下游处理不过来。
2. 看 `EVENT_AUDIO_DURATION_ID` 和 `EVENT_VIDEO_FRAMES_ID` 对应的缓存统计。
3. 如果 dropping 的 audio/video 队列高，说明低延迟链路已经在阻止继续堆积。
4. 如果 video decoder 不能 deliver，重点看 render 队列是否长期大于等于 2，以及 render 线程是否卡住。
5. 如果 demuxer 或 codec 报错，LivePlayer 逻辑倾向于 reset demuxer，而不是等待普通 EOF。
6. 日志上按 `demuxer -> dropping -> decoder -> render` 找第一个不再消费或不再转发的节点。

## demo

文件：`studyDemo/day11_player_graph_compare.cpp`

运行：

```bash
cmake -S studyDemo -B studyDemo/output
cmake --build studyDemo/output --target day11_player_graph_compare
.\studyDemo\output\Debug\day11_player_graph_compare.exe
```

如果生成器不是 Visual Studio，执行文件可能在：

```bash
.\studyDemo\output\day11_player_graph_compare.exe
```

demo 观察点：

- 三个 Graph 的音频链路和视频链路是否不同。
- 哪些 API 是共同能力，哪些只属于某一类播放器。
- 同一组 backlog 下，LivePlayer、VodPlayer、MusicPlayer 的 can-deliver 判断如何不同。
- 直播卡顿时应该优先检查哪些计数和插件。

## 面试复述

问：直播播放器和点播播放器的核心差异是什么？

答：直播播放器的核心是低延迟，不能无限缓存，也不能追求每一帧都完整播放。HJGraphLivePlayer 在 demuxer 后引入 `HJPluginAVDropping`，并用音频时长、视频帧数、render 队列数控制能否继续 deliver；发生 EOF 或 codec/demuxer 错误时更倾向于 reset demuxer 保持会话。点播播放器有明确时长和可 seek 的时间线，重点是 pause/resume、seek、进度、repeat 和最终 EOF，不能像直播一样随意丢掉用户要看的内容。

问：为什么 demuxer EOF 不等于 Graph EOF？

答：demuxer EOF 只说明源数据已经读完，下游 decoder、resampler、render 里可能还有未播放帧。用户感知的结束应该等 render 侧把对应流消费完。VodPlayer 要等 audio/video render 对应流结束后再报 Graph EOF；MusicPlayer 也要等 audioRender EOF，并结合 repeat 状态判断是否真的结束。

问：为什么 VodPlayer 和 MusicPlayer 的 seek 要投递到 handler？

答：seek 会影响 demuxer、下游队列、timeline 和 render 状态。投递到 handler 后，连续 seek 可以用同一个 message id 清理旧任务，执行时再通过 weak_ptr 拿 demuxer，避免旧 seek 和 close/release 交错。

## 今日总结

三个播放器不是“同一条链路换个名字”。LivePlayer 的低延迟目标决定了 dropping、较小缓存阈值、首帧/卡顿统计和 reset 策略；VodPlayer 的点播目标决定了 seek、pause、进度和完整 EOF；MusicPlayer 的纯音频目标让链路更短，但 repeat、音轨和最终播放完成状态更关键。排查问题时要先判断当前 Graph 的产品目标，再决定是保完整性、保实时性，还是保音频播放状态一致性。
