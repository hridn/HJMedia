# Day 12：视频解码、渲染和丢帧策略

对应计划：`study/week2-thread-plugin-player-practice.md`

## 今日目标

- 读懂视频解码插件、视频渲染插件和 `HJPluginAVDropping` 在直播播放链路里的分工。
- 用队列堆积模拟解释：生产速度大于消费速度时，为什么直播播放不能只靠“完整播放每一帧”。
- 写出三种策略的伪代码：不丢帧、丢非关键帧、按时间戳追帧。
- 总结三种策略对延迟、流畅度、画面完整性的影响，并转成面试表达。

## 阅读清单

- [x] `src/plugins/doc/HJPluginVideoFFDecoder.md`
- [x] `src/plugins/doc/HJPluginVideoOHDecoder.md`
- [x] `src/plugins/doc/HJPluginVideoRender.md`
- [x] `src/plugins/doc/HJPluginAVDropping.md`
- [x] `src/plugins/HJPluginAVDropping.cpp`

## 一句话理解

点播播放器通常优先保证内容完整，队列满了就让上游反压等待；直播播放器更重视实时性，如果下游渲染跟不上，继续完整播放旧帧只会让用户看到越来越晚的画面，所以要通过 dropping、反压和按时间戳追帧把播放端拉回实时位置。

## 视频链路中的角色

```text
demuxer
  -> HJPluginAVDropping
    -> HJPluginVideoFFDecoder / HJPluginVideoOHDecoder
      -> HJPluginVideoRender
```

| 插件 | 主要职责 | 卡顿时关注点 |
|---|---|---|
| `HJPluginAVDropping` | 管理 A/V 缓冲，必要时丢弃过期帧，优先保留关键视频帧和控制帧 | audio duration、video key frame、是否触发 `tryDropFrames()` |
| `HJPluginVideoFFDecoder` | FFmpeg 软件视频解码，处理 flush、EOF、codec drain | 输入队列是否持续增长、codec 是否产出、是否能 deliver 到 render |
| `HJPluginVideoOHDecoder` | HarmonyOS 硬件视频解码，通过 codec 回调和渲染 bridge 协作 | codec 回调是否只触发调度、bridge 是否有效、flush 后 codec 是否重建 |
| `HJPluginVideoRender` | 消费解码后视频帧，按 timeline 输出到渲染桥或共享内存 | render 队列长度、首帧/EOF 回调、线程是否阻塞 |

`HJPluginAVDropping::runTask()` 的关键分支是：先从输入队列取帧；如果是 clear frame 就执行 `runFlush()`；普通帧会通过 `QUERY_CAN_DELIVER_TO_OUTPUTS_ID` 查询下游是否能接收；如果下游不能接收，就把当前帧 `store()` 回队列，并在音频累计时长超过阈值时调用 `tryDropFrames()`。

## 队列堆积模型

第 12 天 demo 用一个简化视频队列模拟直播场景：

```text
producer: 每 33ms 产生一帧视频
consumer: 大约每 100ms 消费两帧中的一帧
queue capacity: 5
key frame: 每 6 帧一个
```

这个模型故意让生产速度大于消费速度，用来观察三种结果：

1. 不丢帧：队列满后只能阻塞上游，画面完整但延迟继续保留在队列里。
2. 丢非关键帧：队列满时优先删除非关键帧，减少 backlog，同时尽量不破坏后续解码参考点。
3. 按时间戳追帧：用 render clock 判断帧是否过期，过期的非关键帧直接丢掉，优先恢复实时性。

## 三种策略伪代码

### 1. 不丢帧

```cpp
if (renderQueue.full()) {
    return HJ_WOULD_BLOCK;        // 让上游 demuxer/decoder 等待
}

renderQueue.push(frame);
```

适合点播或离线处理。优点是内容完整；缺点是直播卡顿时会保留旧画面，用户看到的时间点越来越落后。

### 2. 丢非关键帧

```cpp
if (renderQueue.full()) {
    auto dropped = renderQueue.dropFirstNonKeyFrame();
    if (!dropped && !incomingFrame.isKeyFrame()) {
        drop(incomingFrame);
        return HJ_OK;
    }
}

renderQueue.push(incomingFrame);
```

适合直播视频链路的轻量追帧。关键点是优先保留 key frame、EOF、flush 这类不能随便丢的帧；非关键帧对画面连续性有影响，但比无限堆积更可接受。

### 3. 按时间戳追帧

```cpp
while (!renderQueue.empty()) {
    auto& frame = renderQueue.front();
    if (frame.isKeyFrame()) {
        break;
    }
    if (frame.pts + toleranceMs >= renderClock.now()) {
        break;
    }
    renderQueue.pop_front();      // 丢掉已经明显过期的非关键帧
}

if (incomingFrame.pts + toleranceMs < renderClock.now() && !incomingFrame.isKeyFrame()) {
    drop(incomingFrame);
} else {
    renderQueue.push(incomingFrame);
}
```

适合低延迟优先的直播播放。优点是能快速追上实时位置；缺点是画面可能跳跃，如果阈值过激会出现明显不连续。

## 策略取舍

| 策略 | 延迟 | 流畅度 | 画面完整性 | 适用场景 |
|---|---|---|---|---|
| 不丢帧 | 最容易升高 | 短期平滑，长期可能越播越慢 | 最好 | 点播、录制回放、离线处理 |
| 丢非关键帧 | 可控 | 可能轻微跳动 | 中等，关键帧保留 | 直播轻微堆积、下游偶发慢 |
| 按时间戳追帧 | 最容易恢复 | 可能明显跳帧 | 最弱 | 直播严重卡顿、弱网追实时 |

直播播放器通常优先实时性，因为旧帧完整播放完并不等于用户体验更好。对于直播，用户更关心“现在发生了什么”；对于点播，用户更关心“这段内容是否完整看完”。

## 日志观察点

| 位置 | 建议字段 | 目的 |
|---|---|---|
| dropping `deliver()` | frame pts、media type、key frame、audio duration、queue size | 判断是否开始堆积 |
| dropping `tryDropFrames()` | before/after queue size、dropped count、min/max audio duration | 判断是否触发追帧 |
| decoder `runTask()` | input size、ret、output count、can deliver result | 判断卡在解码还是下游接收能力 |
| render `deliver()` / `receive()` | frame pts、queue size、render clock、first frame、EOF | 判断渲染端是否消费过慢 |
| flush / EOF | frame type、route、status、downstream ret | 防止把控制帧当普通帧丢掉 |

## 今日 Demo

文件：`studyDemo/day12_video_drop_policy.cpp`

编译运行：

```powershell
cd D:\PROJECT\temp\HJMedia
cmake -S studyDemo -B studyDemo/output
cmake --build studyDemo/output --target day12_video_drop_policy
.\studyDemo\output\Debug\day12_video_drop_policy.exe
```

如果使用单配置生成器，产物可能在：

```powershell
.\studyDemo\output\day12_video_drop_policy.exe
```

demo 观察点：

- `never-drop` 会在队列满时打印 `backpressure`，表示上游被迫等待。
- `drop-non-key-when-full` 会优先打印 `drop-queued-non-key`，说明它在保留 key frame 的前提下释放队列空间。
- `catch-up-by-timestamp` 会根据 `clockMs` 打印 `drop-expired` 或 `drop-incoming-expired`，说明它不是只看队列容量，而是看帧是否已经落后播放时钟。
- 最后的 `summary` 可以对比三种策略的 produced、rendered、dropped、blocked、maxBacklog。

## 面试复述

问：为什么直播播放中实时性通常优先于完整性？

答：直播的目标是尽量接近实时。如果下游处理不过来还坚持播放每一帧，队列里的旧帧会越积越多，用户看到的画面会持续落后真实直播时间。HJMedia 的 LivePlayer 在 demuxer 后引入 `HJPluginAVDropping`，并通过音频累计时长、视频关键帧和下游 can-deliver 查询决定是否继续投递或丢掉过期帧。这样牺牲部分画面完整性，换取播放端延迟可控。

问：丢帧为什么要尽量保留关键帧？

答：关键帧通常是后续帧解码的重要参考点。随便丢关键帧可能让后面的非关键帧无法正确解码或出现长时间花屏，所以追帧时一般优先丢非关键帧，并保留 key frame、flush、EOF 这类关键语义帧。

问：如何判断卡顿发生在 decoder 还是 render？

答：先看 decoder 是否持续 `receive()` 输入并产出输出；如果 decoder 有输出但 `canDeliverToOutputs()` 失败或 render 队列长期超过阈值，问题更可能在 render 消费慢。如果 decoder 输入队列持续增长但没有输出，则要查 codec run/getFrame、flush 分支和 decoder 状态。

## 今日总结

第 12 天的重点不是写一个复杂丢帧算法，而是建立“产品目标决定队列策略”的意识。点播更适合反压等待，因为完整性重要；直播更适合在可控边界内丢弃过期非关键帧，因为低延迟重要。排查直播卡顿时，要同时看 dropping、decoder、render 三段：dropping 决定是否允许继续堆积，decoder 决定是否能把压缩帧转成可渲染帧，render 决定最终播放时钟是否追得上。
