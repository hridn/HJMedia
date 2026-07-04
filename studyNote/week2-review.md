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

消息队列存的是“该干活了”的调度信号，不是帧本身。帧在 plugin 自己的输入队列 `mediaFrames` 里，`asyncAndClear` 去掉的是重复的 `runTask` 调度请求，不会把已经入队的帧删掉。实际执行时，`runTask()` 会按各插件自己的策略去 `receive()` 和处理输入；如果当前插件认为还有必要继续跑下一轮，就返回 `HJ_OK`，由 `postTask()` 再投递一次自调度。

### 4. Plugin 为什么要把 deliver() 和 runTask() 拆到不同线程？

更准确地说，是把“上游交付帧”和“插件实际处理帧”解耦。`deliver()` 往往只负责把帧送进下游输入队列并触发调度，尽快返回，避免阻塞上游调用栈；`runTask()` 则在 plugin 绑定的 looper / scheduler 线程上执行，负责真正的 `receive()`、处理和向下游转发。这样做的收益是：上游线程变轻、插件处理顺序更稳定，也更容易实现延迟调度、latest-only 调度和生命周期保护。  

但这里不能简单理解成“完全不需要锁”。HJMedia 里输入队列本身仍然是线程安全结构，很多状态访问也仍然通过同步对象或原子变量保护；更准确的理解是“跨线程入口变薄，核心处理尽量集中到 owner 线程上完成”。

### 5. 为什么 seek 不能直接在调用线程执行？

seek 不能直接在调用线程执行，因为它不是一次简单的状态赋值，而是一次跨多个异步组件的状态切换。按当前源码，`HJMediaPlayer::seek()` 会先在 player 自己的 scheduler 上排队，先给 audio/video render 设置 `preFlush`，再进入 graph / demuxer 的 seek 路径；真正的 `source->seek()` 是在 demuxer 自己的 scheduler 里执行的，后续 decoder、render、timeline 的旧状态清理主要通过 `flush` 继续向下传播。  

如果在调用线程里直接做这些操作，就会绕开对象各自的线程归属，与正在执行的 `runTask()`、`asyncSelf()`、`flush()`、`close()` 并发交错，导致旧帧、旧 EOF、缓存残留或释放竞态。核心不是"把所有逻辑都放到一个 graph looper 线程"，而是"每个组件的状态变更都应在它自己的 owner scheduler / handler 上完成"，这样才能保证 seek 这次状态切换和已有异步任务的顺序关系是可控的。

### 6. 连续 seek 时如何保证只有最后一次生效？

在 graph 这条旧路径里，连续 seek 通过 `asyncAndClear(messageId)` 保证 latest-only：每次新 seek 入队前先删除同 id 的旧 seek 请求，因此最终只执行最后一次。配合 `weak_ptr` 捕获业务对象，可以避免旧 seek 越过对象生命周期。  

如果看 `HJMediaPlayer` 这条路径，思想相同，但实现更接近 scheduler 层的 `asyncRemoveBefores(name)`，本质上也是“新 seek 覆盖旧 seek”。

### 7. close / done / internalRelease 有什么区别？

- `close()`：在概念上可以理解为业务侧的停止入口，但在本仓库当前不少 graph 实现里，它并没有承担完整的停止和释放逻辑，有些地方基本只是做状态检查后返回 `HJ_OK`。
- `done()`：`HJSyncObject` 的生命周期终结入口。它会把对象状态切到 `HJSTATUS_Done`，然后调用 `internalRelease()`。
- `internalRelease()`：子类真正释放成员资源的实现，例如 plugin、thread、handler、timeline、shared memory 等。外部不应直接调用。

所以如果按“当前源码事实”来讲，真正可靠的生命周期终结动作是 `done()`，真正的资源释放落点是 `internalRelease()`；`close()` 更像一个业务语义接口，而不是统一可靠的释放入口。

### 8. 旧 delayed task 越过对象生命周期怎么办？

两种措施配合：
1. Handler 的消息 target 本身是 weak reference。这样 handler 不存在后，消息在 looper 取出或队列扫描时会因为 target 失效而被丢弃。
2. Lambda 内捕获 `Wtr`（weak_ptr）而不是裸指针或 `this`。任务执行时先 `lock()`，对象已释放就直接跳过。示例：`auto wDemuxer = SHARED_FROM_THIS; handler->async([wDemuxer] { if (auto d = wDemuxer.lock()) d->seek(...); })`。

### 9. 直播播放器和点播播放器的核心差异是什么？

直播优先低延迟，所以 demuxer 后接 `HJPluginAVDropping`，允许丢弃过期非关键帧，按时间戳追帧，不提供 seek 能力。点播优先完整性和可控制的时间线，所以强调 seek、pause、timestamp、response to repeated playback，以及 render 侧最终 EOF 的准确上报。直播的 EOF 倾向于 reset demuxer 继续等待，不轻易结束会话；点播的 EOF 代表文件的正常播放完成。

### 10. 如何定位插件链路卡住的位置？

沿 `deliver()` → `runTask()` → `receive()` → `deliverToOutputs()` 的路径加日志，每段打印：plugin name、queue size before/after、status、thread id、frame pts。如果某段 queue size 持续增长但下游不再消费，卡点就在那里。具体来说：
- deliver 正常但下游 runTask 不执行：查 handler 是否 alive、状态是否 pause、message id 是否被错误清掉。
- runTask 执行但 receive 返回空或者 WOULD_BLOCK：没数据到达，问题在上游。
- runTask 正常处理但 deliverToOutputs 失败：优先怀疑下游反压、对象失效、状态异常或 EOF / flush 语义问题，不要只盯着“队列满”。

### 11. render preFlush 的用途是什么？风险是什么？

seek 开始前 `setPreFlush(true)` 让 render 暂停消费旧队列，防止 flush 到达前 render 继续播放旧帧。对 audio render 来说，这一步还可能同步触发底层 render 的暂停，不只是打一个标志位。风险是如果 flush 因某种原因丢失或失败，render 会一直停在 preFlush，导致播放卡死。需要确保 flush 一定能到达 render，或者设计超时 / fallback 机制。

### 12. 什么场景下 postTask 的 weak_ptr 捕获是必要的？

更准确地说，所有“可能越过对象生命周期”的跨线程异步任务都应优先使用 weak_ptr。典型场景：
- `postTask` 调度 `runTask`：插件 close 后 looper 线程上可能还有未执行的 `runTask` 消息。
- seek 调度：旧 seek 被执行时 demuxer 可能已被释放。
- `onOutputUpdated` 通知上游：上游插件可能已经 close。

### 13. generation 过滤在 seek 中解决什么问题？

它解决的是“旧 seek 轮次残留帧或旧 EOF 污染新播放轮次”的问题：每次 seek 递增 generation，render 和 EOF 处理只接受当前 generation 的帧，这样即使某个旧帧因为异步乱序或 flush 遗漏留在队列里，也会被 gate 掉。  

但要注意，这更适合作为一种修复思路或设计手段，而不是当前仓库 seek 路径里到处都已经统一实现的既有机制。当前源码里更明确存在的是 `preFlush`、demuxer seek、下游 flush、EOF reset 和 latest-only seek；如果要引入 generation gate，还要额外保证 generation 更新点一致，否则可能误杀合法新帧。

### 14. 为什么 LivePlayer 有 dropping 而 VodPlayer 没有？

产品目标不同。当前实现里，LivePlayer 追求接近实时，下行链路的任何一环卡顿（弱网、解码慢、渲染慢）都倾向于通过 dropping 把播放端拉回实时；VodPlayer 追求内容完整播放，队列满时更适合通过反压让上游等待，而不是主动丢帧。MusicPlayer 也没有这类 dropping，因为纯音频场景下丢帧会造成可感知的音频缺失。  

这里要强调的是：这是当前仓库实现和当前产品目标的选择，不是所有直播播放器和点播播放器都必然如此。

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
