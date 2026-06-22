# Day 8：HJThread 基础和 task queue 实践

对应计划：`study/week2-thread-plugin-player-practice.md`

## 今日目标

- 读懂 `HJLooperThread`、`HJLooper`、`HJHandler`、`HJMessageQueue`、`HJMessage` 的分工。
- 用 `studyDemo/day08_task_queue_handler.cpp` 验证：主线程 post 任务，工作线程按队列串行执行。
- 观察 delayed task 的执行顺序。
- 用 demo 解释为什么播放器里的 `seek()` 应该投递到 graph handler，而不是在调用线程直接改状态。

## 阅读记录

- [x] `src/utils/HJThread/doc/README.md`
- [x] `src/utils/HJThread/doc/HJLooperThread.md`
- [x] `src/utils/HJThread/doc/HJLooper.md`
- [x] `src/utils/HJThread/doc/HJHandler.md`
- [x] `src/utils/HJThread/doc/HJMessageQueue.md`
- [x] `src/utils/HJThread/doc/HJMessage.md`

## 核心关系

```text
caller thread
  -> HJHandler::post/postDelayed/removeMessages/runWithScissors
    -> HJMessageQueue: 按 when 排序，阻塞等待，按 what/obj 移除
      -> HJLooper::loop: 取出到期 message，检查 weak target，分发
        -> HJLooperThread: 拥有真实 OS 线程和 looper 生命周期
```

最短理解：

- `HJLooperThread` 是线程和 looper 的生命周期 owner。
- `HJLooper` 是绑定在线程上的消息循环。
- `HJHandler` 是跨线程投递任务的入口。
- `HJMessageQueue` 是按执行时间排序的队列。
- `HJMessage` 是队列节点，携带 `what`、`when`、`callback`、`obj`、`target`。

## HJThread 关键语义

### 1. 一个 looper thread 对应一个 looper

`HJLooperThread::run()` 在线程函数中调用：

```text
HJLooper::prepare()
HJLooper::loop()
```

`HJLooper::prepare()` 只允许在 `HJLooperThread` 创建的线程中调用，并且一个线程只能 prepare 一次。

### 2. Handler 只负责投递，不负责拥有线程

`HJHandler` 构造时绑定一个 `HJLooper`，之后通过：

- `post(...)` 投递立即任务。
- `postDelayed(...)` 投递延迟任务。
- `removeMessages(what, obj)` 取消匹配的队列任务。
- `runWithScissors(...)` 做同步跨线程调用。

线程生命周期由 `HJLooperThread` 管，不由 `HJHandler` 管。

### 3. delayed task 不是新线程

延迟任务只是 `HJMessage.when` 是未来时间。任务仍然在同一个 looper thread 上串行执行。

这点很重要：delay 只影响“什么时候进入执行”，不改变“在哪个线程执行”和“是否串行”。

### 4. target 是 weak

`HJHandler::enqueueMessage(...)` 会把 message 的 target 设置为 handler 的 weak 引用。`HJLooper` 分发时如果 target 已经过期，message 会被跳过。

这能降低 teardown 后旧消息访问已销毁 handler 的风险，但不能替代业务层的 close/done 状态检查。

### 5. `asyncAndClear` 是 latest-only 模式

`HJLooperThread::Handler::asyncAndClear(task, id, delay)` 的语义是：

```text
removeMessages(id)
postDelayed(task, id, delay)
```

适合 seek、reset、刷新 UI、延迟重试这类“只关心最新请求”的操作。

## 今日 Demo

文件：`studyDemo/day08_task_queue_handler.cpp`

这个 demo 用标准 C++17 模拟 HJThread 的核心行为：

- `DemoHandler` 对应 `HJLooperThread + HJLooper + HJMessageQueue` 的简化组合。
- `post()` 对应 `HJHandler::post()`。
- `postDelayed()` 对应 `HJHandler::postDelayed()`。
- `postLatest()` 对应 `HJLooperThread::Handler::asyncAndClear()`。
- `remove(id)` 对应 `HJHandler::removeMessages(what)`。

编译运行：

```powershell
cd D:\PROJECT\temp\HJMedia
cmake -S studyDemo -B studyDemo/output
cmake --build studyDemo/output --target day08_task_queue_handler
.\studyDemo\output\Debug\day08_task_queue_handler.exe
```

如果使用单配置生成器，产物可能在：

```powershell
.\studyDemo\output\day08_task_queue_handler.exe
```

预期观察：

```text
main thread begin
graph-handler started
open graph
decode one frame
delayed preload after 80ms
seek to 30s, only latest seek runs
render frame after seek
graph-handler stopped
main thread end
```

重点不是具体毫秒数，而是三个现象：

1. 所有任务都在同一个 worker thread 上执行。
2. 延迟任务按到期时间执行，不是按 post 顺序绝对执行。
3. 同一个 `kSeekMsg` 连续 `postLatest()` 后，只保留最后一次 seek。

## 为什么 seek 要投递到 graph handler

`seek()` 不是一个单纯的变量赋值。它会影响多个模块：

- demuxer 要跳到新时间。
- decoder 要 flush 旧包和旧帧。
- resampler 可能要清空内部缓存。
- render 要暂停旧帧输出，避免 seek 后还播放旧画面。
- timeline 要重置播放基准。
- EOF、pause、resume、close 可能同时发生。

如果调用线程直接执行 seek，会出现两个问题：

1. 并发修改 graph/plugin 状态：graph 线程可能正在 `runTask()` 或消费帧，调用线程同时清队列或改状态，会产生竞态。
2. 操作顺序不可控：连续 seek、close、flush、render 回调可能交错执行，很难保证“清旧帧 -> 定位 -> 恢复播放”的顺序。

投递到 graph handler 后，所有 graph 状态变更被串行化。如果 seek 使用 latest-only 的 message id，还可以把过期 seek 请求清掉：

```text
seek(10s) 被移除
seek(20s) 被移除
seek(30s) 执行
```

这就是 `asyncAndClear()` 这类 API 对播放器控制流的价值。

## 面试表达

问：`LooperThread`、`Looper`、`Handler` 是什么关系？

答：`LooperThread` 拥有真实线程和生命周期；线程启动后创建线程本地的 `Looper`；`Looper` 持有 `MessageQueue` 并循环取消息；`Handler` 绑定到某个 `Looper`，调用方通过 `Handler` 把任务或消息投递到这个 looper 的队列里。最终任务在 `LooperThread` 的线程上串行执行。

问：为什么 delayed task 仍然是串行的？

答：delay 只是改变 message 的 `when`，消息仍然进入同一个 `MessageQueue`。`Looper` 只在消息到期后取出并分发，所以任务的执行线程没有变化，依然由同一个 looper thread 串行执行。

问：为什么 seek 不建议在调用线程直接执行？

答：seek 会同时影响 demuxer、decoder、render、timeline 和 frame queue。直接跨线程修改这些状态会破坏 graph 内部顺序，还可能和正在执行的 `runTask()`、close、EOF 交错。投递到 graph handler 后，seek 和其他 graph 操作在同一线程串行执行，顺序更清楚，也方便用 `asyncAndClear` 丢弃过期 seek。

## 今日总结

HJThread 的核心不是“开一个线程”，而是“给一个线程建立串行消息入口”。`HJLooperThread` 管线程，`HJLooper` 管循环，`HJMessageQueue` 管时间顺序，`HJHandler` 管投递和取消。播放器、插件、graph 这类状态复杂的模块，应该尽量通过 handler 把控制操作收敛到同一个执行上下文里，减少跨线程直接改状态。
