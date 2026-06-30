# 第 2 周学习笔记：线程模型、插件系统和播放器扩展

对应计划：`study/week2-thread-plugin-player-practice.md`

## 本周目标

- 理解 HJMedia 的线程、handler、message queue 模型
- 理解 Plugin 生命周期和输入输出队列
- 掌握 seek、flush、EOF、teardown 的常见风险
- 对比 MusicPlayer、LivePlayer、VodPlayer

## 本周关键问题

- LooperThread、Looper、Handler 的关系是什么？
- 为什么 seek 不能简单地在调用线程直接执行？
- close / done / internalRelease 有什么区别？
- Plugin 的 `deliver()`、`receive()`、`flush()`、`runTask()` 如何协作？
- 直播和点播播放器有什么关键差异？

## Day 8：HJThread 基础和 task queue 实践

### 今日阅读

- [x] `src/utils/HJThread/doc/README.md`
- [x] `src/utils/HJThread/doc/HJLooperThread.md`
- [x] `src/utils/HJThread/doc/HJLooper.md`
- [x] `src/utils/HJThread/doc/HJHandler.md`
- [x] `src/utils/HJThread/doc/HJMessageQueue.md`
- [x] `src/utils/HJThread/doc/HJMessage.md`

### 今日实践

```text
task queue demo 文件：studyDemo/day08_task_queue_handler.cpp
独立笔记文件：studyNote/08-hjthread-model.md
验证的问题：主线程 post 任务后，工作线程是否串行执行；delayed task 是否按到期时间执行；连续 seek 是否只保留最后一次
任务如何 post：post() 投递立即任务，postDelayed() 投递延迟任务，postLatest() 模拟 asyncAndClear()
任务在哪个线程执行：所有任务都在 graph-handler worker thread 执行，不在 main thread 直接执行
delayed task 结果：delay 只改变任务到期时间，不改变执行线程；到期后仍在同一 worker thread 串行执行
和 HJMedia seek handler 的关系：seek 会影响 demuxer、decoder、render、timeline、queue，应该投递到 graph handler 串行化；连续 seek 可用同一个 message id 清掉旧请求
```

### 模型图

```text
caller thread
  -> HJHandler::post/postDelayed/removeMessages
    -> HJMessageQueue: 按 when 排序，按 what/obj 移除
      -> HJLooper::loop: 取到期 message，检查 weak target，分发
        -> HJLooperThread: 真实 worker thread 上串行执行 task
```

### 今日总结

HJThread 的重点不是“创建线程”，而是“给一个线程建立串行消息入口”。`HJLooperThread` 管线程生命周期，`HJLooper` 管循环，`HJMessageQueue` 管按时间排序的消息，`HJHandler` 是调用方跨线程投递、取消、同步执行任务的入口。

`seek()` 这种播放器控制操作不应该在调用线程直接改 graph/plugin 状态。它需要和 `runTask()`、flush、close、EOF、render 消费帧等操作保持顺序，所以更适合投递到 graph handler。对于连续 seek，只关心最新目标时间，适合使用类似 `asyncAndClear()` 的 latest-only 模式。

验收答案：`LooperThread` 是线程 owner；`Looper` 是运行在该线程上的消息循环；`Handler` 绑定到 `Looper`，负责把任务放入 `MessageQueue`；`MessageQueue` 按执行时间保存消息；最终任务在 `LooperThread` 的线程上串行执行。


## Day 9：teardown 风险实践

### 今日阅读

- [x] `src/utils/HJThread/doc/HJMessageQueue.md`
- [x] `src/utils/HJThread/doc/HJMessage.md`
- [x] `src/graphs/HJGraphMusicPlayer.cpp` seek / release 相关逻辑
- [x] `src/utils/HJSyncObject.h`

### 今日实践

```text
weak task demo 文件：studyDemo/day09_weak_task_teardown.cpp
独立笔记文件：studyNote/09-thread-teardown-risk.md
模拟的问题：对象 close / release 后，之前投递的 delayed task 仍然到期执行
不用 weak_ptr 的风险：lambda 捕获 raw pointer 或 this 时，旧任务可能继续访问已 close 或已释放的对象
使用 weak_ptr 后的行为：任务执行时先 lock，业务对象已释放就跳过，不访问旧对象
和 HJMedia delayed task 的关系：HJGraphMusicPlayer::seek() 使用 asyncAndClear 清旧 seek，并在 lambda 里捕获 demuxer 的 Wtr
```

### 关键源码理解

`HJMessageQueue` 只负责按 `when` 保存和分发消息。它可以在 quit 时清空队列，也能跳过 target 已失效的消息，但它不知道 lambda 内部捕获的业务对象是否还有效。

`HJGraphMusicPlayer::seek()` 的核心写法是：

```cpp
HJPluginFFDemuxer::Wtr wDemuxer = m_demuxer;
m_handler->asyncAndClear([wDemuxer, i_timestamp] {
    auto demuxer = wDemuxer.lock();
    if (demuxer) {
        demuxer->seek(i_timestamp);
    }
}, m_seekId);
```

这里的重点有两个：

- `asyncAndClear(..., m_seekId)`：连续 seek 时只保留最新请求。
- `Wtr` 捕获：任务真正执行时再确认 demuxer 是否仍存在。

### close / done / internalRelease 对比

| 操作 | 职责 | 是否完整释放 | 风险 |
|---|---|---|---|
| `close()` | 面向业务的停止入口，通常用于停止播放、关闭输入、让上层认为实例不可继续工作 | 不一定。以 `HJGraphMusicPlayer::close()` 当前实现看，它主要做状态检查 | close 后旧 delayed task 仍可能继续 seek、回调或写状态 |
| `done()` | `HJSyncObject` 的生命周期终结入口，设置 `HJSTATUS_Done` 后调用 `internalRelease()` | 是生命周期上的终结入口 | 旧任务若不检查 done 状态，可能访问释放中的对象 |
| `internalRelease()` | 子类真正释放成员资源，例如 plugin、thread、handler、timeline | 是资源释放实现，通常不应由外部直接调用 | release 后成员指针失效，lambda 捕获裸指针或 `this` 风险最大 |

### 今日总结

第 8 天解决“任务在哪个线程串行执行”，第 9 天解决“旧任务是否还能越过对象生命周期”。handler 的 weak target 只能保护 handler 本身，业务 lambda 仍要避免捕获裸指针。播放器 teardown 的基本组合是：固定 message id 清理旧任务、公开接口检查 done 状态、异步任务捕获 `weak_ptr`、释放时先停会访问资源的线程和回调，再释放资源。

## Day 10：Plugin 生命周期和日志点设计

### 今日阅读

- [x] `src/plugins/doc/HJPlugin.md`
- [x] `src/plugins/doc/HJPluginCodec.md`
- [x] `src/plugins/doc/HJMediaFrameDeque.md`
- [x] `src/plugins/HJPlugin.h`
- [x] `src/plugins/HJPlugin.cpp`
- [x] `src/plugins/HJMediaFrameDeque.cpp`

### 生命周期图

```text
NONE -> init -> Inited -> runTask/process -> pause/resume -> flush -> EOF/Ready -> done -> internalRelease
```

### 日志点设计

| 函数 | 应打印字段 | 用于定位什么问题 |
|---|---|---|
| `deliver()` | plugin name、srcKeyHash、src plugin、media type、track id、pts/dts、duration、frame type、queue size before/after、status、thread id | 上游是否真的把帧推到了下游；队列是否持续增长 |
| `receive()` | plugin name、srcKeyHash、pts/dts、frame type、queue size after、status、thread id | `runTask()` 是否在消费输入队列；消费速度是否跟得上 |
| `flush()` | plugin name、srcKeyHash、queue size before/after、是否 store clear frame、status、thread id | seek/切流后旧帧是否被清掉；flush 是否到达目标插件 |
| `runTask()` | plugin name、status、paused、input size、delay、thread id、route、ret | handler 是否调度到了插件；是否因为 pause/status/空队列退出 |
| `deliverToOutputs()` | plugin name、output plugin、media type、track id、pts/dts、frame type、output count、ret | 下游是否收到了帧；某个输出插件是否 deliver 失败 |

### 今日实践

```text
demo 文件：studyDemo/day10_plugin_lifecycle_logging.cpp
独立笔记文件：studyNote/10-plugin-lifecycle-log-design.md
模拟链路：demuxer -> audio-decoder -> audio-render
观察点：deliver、receive、runTask、deliverToOutputs、pause、flush、EOF
关键结论：队列归消费者插件管理，控制帧和状态变化要沿同一条插件链路传播，日志字段至少要能串起 plugin、frame、queue、status、thread 这五类信息
```

### 今日总结

Plugin 的调度不是单次函数调用，而是“投递 -> 入队 -> handler 调度 -> 消费 -> 输出转发 -> 控制帧传播”的完整链路。排查卡住位置时，先看日志是不是断在 `deliver()`、`runTask()`、`receive()`、`deliverToOutputs()` 的某一层，再判断是 handler、状态、队列还是下游插件出了问题。

## Day 11：LivePlayer / VodPlayer / MusicPlayer 对比

### 今日阅读

- [x] `src/graphs/HJGraphLivePlayer.h`
- [x] `src/graphs/HJGraphLivePlayer.cpp`
- [x] `src/graphs/HJGraphVodPlayer.h`
- [x] `src/graphs/HJGraphVodPlayer.cpp`
- [x] `src/graphs/HJGraphMusicPlayer.h`
- [x] `src/graphs/HJGraphMusicPlayer.cpp`

### 对比表

| Graph | 场景 | 是否支持 seek | 是否追求低延迟 | 是否需要追帧 | EOF 语义 |
|---|---|---|---|---|---|
| MusicPlayer | 纯音频播放 | 是 | 否 | 否 | demuxer EOF 后按 repeats 决定 reset 或等待最终 audioRender EOF |
| LivePlayer | 直播音视频流 | 否 | 是 | 是 | demuxer EOF 或 codec/network 错误倾向于 reset demuxer，不轻易结束直播会话 |
| VodPlayer | 点播音视频 | 是 | 否 | 弱于直播 | demuxer EOF 记录最后流，audio/video render 都结束后上报 Graph EOF |

### 今日实践

```text
demo 文件：studyDemo/day11_player_graph_compare.cpp
独立笔记文件：studyNote/11-player-graphs-compare.md
观察点：三个 Graph 的插件链路、共同 API、差异 API、反压阈值、EOF 策略
关键结论：LivePlayer 保实时性，VodPlayer 保可 seek 的完整播放，MusicPlayer 保纯音频 repeat/音轨/最终 EOF 状态一致性
```

### 核心差异

LivePlayer 在 demuxer 后接 `HJPluginAVDropping`，用更激进的音视频缓存阈值控制延迟；VodPlayer 和 MusicPlayer 没有 dropping 插件，主要由 demuxer 输出和下游 decoder/render 队列做反压。

VodPlayer 和 MusicPlayer 都有 `seek()`，并且都通过 handler `asyncAndClear` 投递到 demuxer，避免连续 seek 和 close/release 交错。LivePlayer 没有公开 seek，因为直播流通常没有稳定、可任意定位的时间轴。

MusicPlayer 虽然链路最短，但 repeat 和 EOF 状态最细：demuxer EOF 不直接等于播放结束，还要等 audioRender 消费完最终流后才报告 Graph EOF。

### 今日总结

直播播放器和点播/音乐播放器的差异来自产品目标。直播优先低延迟，所以允许 dropping、追帧、reset；点播和音乐优先完整性和可控时间线，所以强调 seek、pause、timestamp、repeat 和 render 侧最终 EOF。排查卡顿时，LivePlayer 要从 `demuxer -> dropping -> decoder -> render` 找第一个堆积点；点播和音乐则重点看 seek/flush、timeline、render 缓存和 EOF 状态是否一致。

## Day 12：视频解码、渲染和丢帧策略

### 今日阅读

- [ ] `src/plugins/doc/HJPluginVideoFFDecoder.md`
- [ ] `src/plugins/doc/HJPluginVideoOHDecoder.md`
- [ ] `src/plugins/doc/HJPluginVideoRender.md`
- [ ] `src/plugins/doc/HJPluginAVDropping.md`

### 今日实践

```text
drop policy demo 文件：
模拟的问题：
生产速度：
消费速度：
队列变化：
丢帧策略：
观察结果：
```

### 策略对比

| 策略 | 延迟 | 流畅度 | 完整性 | 适合场景 |
|---|---|---|---|---|
| 不丢帧 |  |  |  |  |
| 丢非关键帧 |  |  |  |  |
| 按时间戳追帧 |  |  |  |  |

## Day 13：seek / flush / EOF 问题定位演练

### 问题：seek 后旧帧被播放

```text
现象：
可疑模块：
源码入口：
需要打印的日志：
可能原因：
修复思路：
修复风险：
```

### seek 风险清单

- [ ] demuxer 是否 seek 到新位置
- [ ] decoder 是否 flush
- [ ] resampler 是否清空内部缓存
- [ ] render 是否停止旧帧播放
- [ ] timeline 是否重置
- [ ] 旧异步任务是否仍然在执行

## Day 14：本周复盘

### 15 个面试问答

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

### 5 分钟异步调度模型介绍稿


### 本周最重要的 5 个收获

1. 
2. 
3. 
4. 
5. 
