# Day 14：第 2 周复盘 — 线程模型、插件系统和播放器扩展

对应计划：`study/week2-thread-plugin-player-practice.md`

## 今日目标

- 整理 15 个线程 / 插件 / 播放器面试问答
- 写一份 5 分钟异步调度模型介绍稿
- 总结本周最重要的 5 个收获

---

## 15 个面试问答

### 1. HJLooperThread、HJLooper、HJHandler、HJMessageQueue 的关系是什么？

`HJLooperThread` 启动一条 `std::thread`；这条线程内部调用 `HJLooper::prepare()` 创建一个 `thread_local` 的 `HJLooper`；`HJLooper` 持有 `HJMessageQueue`，并在 `loop()` 中不断取消息分发；`HJHandler` 是外部投递任务的入口，它把 lambda 包装成 `HJMessage` 放入队列。完整链路是：Handler 投递 → MessageQueue 按 `when` 排序 → Looper 循环取出到期消息 → dispatchMessage 执行 callback。

### 2. Plugin 的 postTask() 为什么用 asyncAndClear 而不是 async？

因为 `deliver()`、`flush()`、`onOutputUpdated()` 都可能频繁触发 `postTask()`。如果每次都投递一个 `runTask()` 消息，消息队列会堆积大量重复的调度请求。`asyncAndClear` 先清除同 id 的旧消息再投递新消息，保证同一个 plugin 的待执行调度请求最多保留一个，降低无意义唤醒和队列堆积。

### 3. 消息队列去重后，runTask() 怎么保证帧不会丢失？

消息队列存的是"该干活了"的调度信号，不是帧本身。帧在 plugin 自己的输入队列 (`mediaFrames`) 里。一次 `runTask()` 内部会循环 `receive()` 消费所有可用帧，不会只处理一帧。如果处理不完（还有数据），`runTask` 返回 `HJ_OK`，`postTask` 会继续投递下一轮自调度。

### 4. Plugin 为什么要把 deliver() 和 runTask() 拆到不同线程？

拆到不同线程后：上游 deliver 只做入队操作，立即返回，不阻塞调用栈；下游 runTask 在绑定的 looper 线程上串行处理，不需要加锁保护内部状态；同时天然支持延迟调度（等下游腾空间）、取消旧任务（asyncAndClear）和跳过早销毁的回调（weak_ptr）。

### 5. 为什么 seek 不能直接在调用线程执行？

seek 会修改 demuxer、decoder、resampler、render、timeline、EOF 状态等跨模块状态。如果在调用线程直接执行，可能与 looper 线程上正在执行的 `runTask()`、`flush()`、`close()` 并发，破坏顺序一致性。通过 handler 投递到 graph looper 线程后，seek 和普通数据流在同一个线程上串行，保证"先排在前、后排在后的"顺序。

### 6. 连续 seek 时如何保证只有最后一次生效？

使用 `asyncAndClear(messageId)`：每次 seek 投递前先清掉队列里同 id 的旧 seek 请求，再投递新请求。这样前几个 seek 还在排队就被移除，最终只执行最后一次。配合 `weak_ptr` 捕获业务对象，防止旧 seek 越过对象生命周期。

### 7. close / done / internalRelease 有什么区别？

- `close()`：面向业务的停止入口，让上层认为实例不可继续工作，但资源不一定立即释放。
- `done()`：`HJSyncObject` 的生命周期终结入口，设置 `HJSTATUS_Done` 后调用 `internalRelease()`。
- `internalRelease()`：子类真正释放成员资源的实现，例如 plugin、thread、handler、timeline。外部不应直接调用。

### 8. 旧 delayed task 越过对象生命周期怎么办？

两种措施配合：
1. Handler 的消息本身使用 weak target：Handler 析构后，MessageQueue 中未执行的消息会自动跳过。
2. Lambda 内捕获 `Wtr`（weak_ptr）而不是裸指针或 `this`：任务执行时先 `lock()`，对象已释放就跳过。示例：`auto wDemuxer = SHARED_FROM_THIS; handler->async([wDemuxer] { if (auto d = wDemuxer.lock()) d->seek(...); })`。

### 9. 直播播放器和点播播放器的核心差异是什么？

直播优先低延迟，所以 demuxer 后接 `HJPluginAVDropping`，允许丢弃过期非关键帧，按时间戳追帧，不提供 seek 能力。点播优先完整性和可控制的时间线，所以强调 seek、pause、timestamp、response to repeated playback，以及 render 侧最终 EOF 的准确上报。直播的 EOF 倾向于 reset demuxer 继续等待，不轻易结束会话；点播的 EOF 代表文件的正常播放完成。

### 10. 如何定位插件链路卡住的位置？

沿 `deliver()` → `runTask()` → `receive()` → `deliverToOutputs()` 的路径加日志，每段打印：plugin name、queue size before/after、status、thread id、frame pts。如果某段 queue size 持续增长但下游不再消费，卡点就在那里。具体来说：
- deliver 正常但下游 runTask 不执行：查 handler 是否 alive、状态是否 pause、message id 是否被错误清掉。
- runTask 执行但 receive 返回空或者 WOULD_BLOCK：没数据到达，问题在上游。
- runTask 正常处理但 deliverToOutputs 失败：下游队列满，反压。

### 11. render preFlush 的用途是什么？风险是什么？

seek 开始前 `setPreFlush(true)` 让 render 暂停消费旧队列，防止 flush 到达前 render 继续播放旧帧。风险是如果 flush 因某种原因丢失或失败，render 会一直停在 preFlush，导致播放卡死。需要确保 flush 一定能到达 render，或者设计超时/fallback 机制。

### 12. 什么场景下 postTask 的 weak_ptr 捕获是必要的？

所有跨线程异步任务都应该用 weak_ptr。具体场景：
- `postTask` 调度 `runTask`：插件 close 后 looper 线程上可能还有未执行的 `runTask` 消息。
- seek 调度：旧 seek 被执行时 demuxer 可能已被释放。
- `onOutputUpdated` 通知上游：上游插件可能已经 close。

### 13. generation 过滤在 seek 中解决什么问题？

防止旧 seek 轮次残留的帧和 EOF 控制帧误伤新播放轮次。每次 seek 递增 generation，render 和 EOF 处理只接受当前 generation 的帧。这样即使某个旧帧因为异步乱序或 flush 遗漏在队列里，也会被 generation gate 丢弃。风险是 generation 更新点不一致时，可能误杀合法的新帧。

### 14. 为什么 LivePlayer 有 dropping 而 VodPlayer 没有？

产品目标不同。直播追求接近实时，下行链路的任何一环卡顿（弱网、解码慢、渲染慢）都需要通过丢弃过期帧把播放端拉回实时。点播追求内容完整播放，队列满时更适合通过反压让上游等待，而不是丢帧。MusicPlayer 也没有 dropping，因为纯音频场景下丢帧会造成可感知的音频缺失。

### 15. 描述一次从交付帧到最终渲染的完整调用链路。

```
上游 deliver(frame)
  → 下游输入队列入队 (mediaFrames.deliver)
  → onInputUpdated()
  → postTask()
    → Handler::asyncAndClear(lambda, runTaskId)
      → HJMessageQueue 入队 (按 when 排序)
        → HJLooper loop() 取出消息
          → Handler::dispatchMessage(callback)
            → wPlugin.lock()
              → plugin->runTask(&delay)
                → receive() 取帧
                → 处理 (decode/resample/render)
                → deliverToOutputs() 转发给下一级
                → 通知上游 onOutputUpdated()
                  → 上游 postTask() 继续生产
```

---

## 5 分钟异步调度模型介绍稿

> **标题：HJMedia 的异步串行调度模型**
>
> （面向对 HJMedia 不熟悉、有 C++ 基础的技术同事，语速正常约 5 分钟）

大家好，今天用 5 分钟介绍 HJMedia 的异步调度模型。这是一个跨平台 C++ 多媒体框架，它的核心挑战是：推流、播放、渲染涉及大量跨线程操作，如何保证数据串行处理的同时又不阻塞调用方？

HJMedia 的方案可以概括为三层：**线程层、消息层、业务层**。

### 第一层：线程层

`HJLooperThread` 封装了一条 `std::thread`。启动后线程进入消息循环，不断从 `HJMessageQueue` 里取消息执行。它自己不产生业务，只提供一个"串行执行入口"。外部通过 `createHandler()` 拿到一个 `Handler` 对象，之后所有投递到该 handler 的任务都会在这个线程上串行执行。

### 第二层：消息层

消息投递有三个关键设计：

1. **延迟调度**：消息按到期时间排序，没到期的消息让线程阻塞等待；有新消息时 `nativeWake()` 唤醒。这支撑了反压场景——下游队列满时设置一个 delay，到期后再尝试。

2. **latest-only 去重**：`asyncAndClear` 先删除同 id 的旧消息再入队。这对连续 seek 特别有用——用户点了三次 seek，前两次在队列里就被移除，最终只执行最后一次。

3. **weak_ptr 生命周期保护**：lambda 捕获 `Wtr` 而不是裸指针。执行时先 `lock()`，对象已释放就跳过。这解决了"旧 delayed task 越过对象生命周期"的问题。

### 第三层：业务层

`HJPlugin` 是消息模型的直接使用者。它初始化时拿到一个 handler，之后所有业务入口都通过 `postTask()` 调度 `runTask()` 在 handler 线程上执行。

关键链路是：上游 `deliver(frame)` 只是把帧放入下游输入队列 + 触发 `postTask()`，立即返回。下游 `runTask()` 在 looper 线程上循环 `receive()` 消费队列帧，处理完后通过 `deliverToOutputs()` 转发给下一级，并通知上游腾出了空间。

三个关键的协作机制：

- **反压**：`runTask` 检测到下游队列满时返回 `WOULD_BLOCK`，停止自调度，直到下游消费后通知。
- **自调度**：`runTask` 返回 `HJ_OK` 时继续投递下一轮，不需要上游再次触发。
- **控制帧传播**：flush、EOF 同样通过输入队列传播，保证控制帧和普通帧的相对顺序正确。

### 结合一个实际场景

seek(5000) 在 HJMedia 中的完整路径：

1. 通过 `asyncAndClear` 投递到 graph handler，保证连续 seek 只保留最新。
2. graph handler 线程上执行 seek 逻辑：先让 render `setPreFlush(true)` 暂停消费。
3. 调用 demuxer seek，成功后向下游传播 flush，清空 decoder/resampler/render 的旧队列和旧 EOF。
4. 重置 timeline 和 generation，开始新一轮播放。
5. demuxer 在新位置产生帧，沿 dropping → decoder → render 流动。

如果某一步没有 atomic 语义（比如 flush 没到达 render，或者旧 EOF 没被清），就会出现旧帧被播放或提前 EOF 的问题。

### 一句话总结

HJMedia 的异步调度模型用 `HJLooperThread` 提供串行执行底座，用 `asyncAndClear` 和 `weak_ptr` 解决重复调度和生命周期问题，用 plugin 的 `postTask` + `runTask` 自调度把"收到帧"和"处理帧"解耦到不同线程。

---

## 本周最重要的 5 个收获

1. **线程是底座，消息是核心** — HJMedia 没有在 plugin 内部加锁保护状态，而是通过把状态访问串行化到同一个 looper 线程上解决并发问题。理解 Handler/MessageQueue 的设计比理解 std::thread API 更重要。

2. **postTask 不是 runTask** — `deliver()` 只是入队 + 触发调度，`runTask()` 异步在 looper 线程上执行。这种解耦让插件链路支持反压、延迟、去重和生命周期保护。

3. **asyncAndClear 的自调度机制** — 消息去重只去重调度信号，不丢帧。一次 `runTask` 可以循环消费所有可用帧，并通过返回 `HJ_OK` 自调度下一轮。

4. **seek 不是单点操作** — 它是 preFlush + demuxer 定位 + 全链路 flush + EOF reset + timeline reset + generation gate 的组合。任何一个环节遗漏都会导致旧帧播放或错误 EOF。

5. **产品目标决定技术策略** — 直播和点播播放器的差异来自产品目标：直播保实时性所以允许丢帧和追帧，点播保完整性所以强调反压和 seek。LivePlayer 有 dropping，VodPlayer 没有——这不是技术能力问题，是产品选择问题。
