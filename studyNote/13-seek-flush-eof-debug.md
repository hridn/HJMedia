# Day 13：seek / flush / EOF 问题定位演练

对应计划：`study/week2-thread-plugin-player-practice.md`

## 今日目标

- 把“seek 后旧帧被播放”讲成一个完整的问题定位案例。
- 能说清 demuxer、decoder、resampler、render、timeline、queue 在 seek 过程中的职责。
- 设计关键日志点，判断旧帧来自哪里、flush 是否传到底、EOF 状态是否被错误沿用。
- 提前识别修复 seek 问题时最容易引入的新风险。

## 问题现象

用户在点播或音乐播放中执行 `seek(5000)` 后，短时间内仍听到或看到 seek 前 1000ms 附近的旧帧；更严重时，旧的 EOF 控制帧被 render 消费，导致新位置的数据还没播放就提前上报播放结束。

这个问题不能只怀疑 demuxer。seek 是跨模块状态切换：上游要定位到新时间点，下游要清空旧缓存，render 要暂停旧帧消费，timeline 要重置，EOF / repeat 状态也要进入新一轮播放语义。

## HJMedia 相关入口

| 源码 | 关注点 |
|---|---|
| `src/core/HJMediaPlayer.cc` | `seek()` 中先对 audio/video render 设置 `setPreFlush(true)`，再调用 graph seek |
| `src/core/HJNodeDemuxer.cc` | demuxer 执行 source seek，清理自身状态，然后调用 `HJMediaNode::flush(info)` 向下游传播 |
| `src/core/HJMediaNode.cc` | `flush()` 会 `setEOF(false)`、`clearInOutStorage()`，再递归 flush 后继节点 |
| `src/core/HJNodeVRender.cc` | video render 的 `preFlush` 会阻止 seek 期间继续消费旧帧，flush 后记录 seek info |
| `src/core/HJNodeARender.cc` | audio render flush/reset 后清空旧输出状态并重置 EOF |
| `src/graphs/HJGraphMusicPlayer.cpp` | music player 的 seek 通过 handler `asyncAndClear` 串行化，EOF 分 demuxer EOF 和 final playback EOF |

## 可疑模块

| 模块 | 可疑点 | 预期日志现象 |
|---|---|---|
| demuxer | `seek()` 没执行、失败、被旧 delayed task 覆盖，或 EOF 标志没清 | seek target 和后续输出 pts 不一致；seek 后仍输出旧 stream/generation |
| decoder | codec 内部缓存没 flush，旧压缩帧继续产出旧 raw frame | decoder flush 后仍输出 seek 前 pts |
| resampler | audio FIFO / 重采样缓存没清，残留 PCM 被送到 render | resampler flush 前后 queue size 不为 0 或 flush 后还有旧 pts |
| render | 没有 `setPreFlush(true)`，seek 期间继续消费旧帧 | seek request 之后、flush 完成之前出现 render old pts |
| timeline | seek 后播放时钟没重置 | 新帧 pts 正确，但 A/V sync、当前进度或 EOF 后 timestamp 异常 |
| queue | 消费者侧输入队列没清，控制帧和普通帧顺序错误 | render queue 中旧普通帧或旧 EOF 仍排在新帧前面 |
| EOF 状态 | demuxer EOF 被当成最终播放 EOF，或旧 EOF 未按新 stream/generation 过滤 | seek 后立刻上报 graph EOF，render queue 还有新帧未消费 |

## 日志点设计

| 位置 | 建议字段 | 用途 |
|---|---|---|
| player / graph seek 入口 | seek id、target pts、调用线程、handler msg id、是否 asyncAndClear | 判断连续 seek 是否只保留最新请求，是否发生线程乱序 |
| demuxer seek | target pts、source seek ret、old EOF、new EOF、stream index、first output pts | 判断上游是否真正切到新位置 |
| 每个插件 flush | plugin name、srcKeyHash、queue size before/after、EOF before/after、generation | 判断 flush 是否传到底，旧队列是否清空 |
| decoder / resampler output | input pts、output pts、generation、flush count、codec/fifo buffered size | 判断旧帧是否来自内部缓存 |
| render receive / render | preFlush、frame pts、generation、queue size、timeline base、first frame after seek | 判断旧帧是否最终被消费 |
| EOF 处理 | frame generation、stream index、pending final EOF、matched final EOF、downstream ret | 判断旧 EOF 是否误伤新一轮播放 |

## 排查顺序

1. 先确认 seek 请求是否进入 graph handler，并且连续 seek 是否清掉旧请求。
2. 看 demuxer seek 返回值和 seek 后首帧 pts，确认源头已经切到目标时间附近。
3. 沿链路检查 flush：demuxer、decoder、resampler、render 的队列大小是否从非 0 变为 0。
4. 检查 render 在 seek request 到 flush complete 之间是否仍然消费旧帧。
5. 检查 EOF：旧 EOF 控制帧是否还在队列里，EOF 标志是否在 flush 时被清掉。
6. 如果 pts 正确但 UI/同步异常，再看 timeline 是否重置，以及 stream/generation 是否匹配。

## 修复思路

- seek 前让 render 进入 preFlush，先停止旧帧继续播放。
- demuxer seek 成功后沿下游传播 flush，清空消费者侧输入队列和插件内部缓存。
- flush 过程中重置 EOF 状态，避免旧 EOF 影响新一轮播放。
- 给 seek 引入 seek id、stream index 或 generation，render / EOF 处理只接受当前 generation。
- 连续 seek 使用 latest-only 语义，避免旧 seek delayed task 晚到后覆盖新状态。

## 新风险

| 修复动作 | 新风险 |
|---|---|
| 扩大 flush 范围 | 误清新 seek 后刚产生的新帧，导致首帧丢失或短暂无声 |
| render preFlush | flush 失败或回调丢失时，render 一直停在 preFlush，播放卡死 |
| generation 过滤 | generation 更新点不一致，可能把合法的新帧误判成旧帧 |
| EOF 过滤 | repeat / final EOF 条件变复杂，可能永远不上报真正 EOF |
| latest-only seek | 如果没有明确日志，用户会误以为每次 seek 都完成了 |
| timeline reset | reset 时机过早或过晚，会造成进度跳变、音视频不同步或首帧通知异常 |

## 今日 Demo

文件：`studyDemo/day13_seek_flush_eof_debug.cpp`

编译运行：

```powershell
cd D:\PROJECT\temp\HJMedia
cmake -S studyDemo -B studyDemo/output
cmake --build studyDemo/output --target day13_seek_flush_eof_debug
.\studyDemo\output\Debug\day13_seek_flush_eof_debug.exe
```

单配置生成器下可尝试：

```powershell
.\studyDemo\output\day13_seek_flush_eof_debug.exe
```

demo 里有两个场景：

- `broken-seek`：不设置 preFlush，不 flush 队列，不重置 EOF / timeline，也不做 generation 过滤。输出中会看到 `render-old-frame-after-seek`，随后旧 EOF 让 render 提前停止。
- `fixed-seek`：先 preFlush，再 flush demuxer / decoder / resampler / render，重置 EOF 和 timeline，并启用 generation gate。输出中只会渲染 5000ms 之后的新帧。

## 面试复述

问：如何排查 seek 后旧帧被播放？

答：我会先把问题拆成“请求是否串行化、上游是否 seek 成功、下游是否 flush 干净、render 是否在 flush 前继续消费、EOF/timeline 是否进入新一轮语义”五段。日志上先看 seek id 和 handler 投递，确认连续 seek 没有乱序；再看 demuxer seek 后首帧 pts；然后沿 decoder、resampler、render 打 queue size before/after 和 EOF before/after，确认 flush 传到底。如果 render 在 seek request 后仍消费旧 pts，重点看 `setPreFlush(true)` 和 render queue；如果 seek 后立刻 EOF，则检查旧 EOF 控制帧、stream index 或 generation 是否被错误接受。修复时通常要组合 preFlush、flush、EOF reset、generation 过滤和 latest-only seek，但要防止误清新帧、render 卡在 preFlush、真正 EOF 不上报这些新风险。

## 今日总结

seek bug 的本质是“跨线程、跨插件、跨队列的状态切换没有形成原子语义”。正确定位不能只看某一个模块，而要沿 demuxer -> decoder -> resampler -> render -> timeline -> EOF 状态整条链路找第一个旧状态泄漏点。第 13 天 demo 用最小模型复现了旧帧和旧 EOF 的危害，也展示了为什么 HJMedia 中 seek 需要 handler 串行化、render preFlush、下游 flush 和 EOF 状态重置配合完成。
