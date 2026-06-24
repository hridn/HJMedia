# Day 10：Plugin 生命周期和日志点设计

对应计划：`study/week2-thread-plugin-player-practice.md`

## 今日目标

- 读懂 `HJPlugin` 如何管理输入队列、输出插件、handler 调度和生命周期。
- 理解 `deliver()`、`receive()`、`flush()`、`runTask()`、`deliverToOutputs()` 在插件链路里的协作关系。
- 为插件链路卡住、队列堆积、flush 不生效、EOF 不传播这些问题设计日志点。
- 用 `studyDemo/day10_plugin_lifecycle_logging.cpp` 模拟插件链路日志。

## 阅读记录

- [x] `src/plugins/doc/HJPlugin.md`
- [x] `src/plugins/doc/HJPluginCodec.md`
- [x] `src/plugins/doc/HJMediaFrameDeque.md`
- [x] `src/plugins/HJPlugin.h`
- [x] `src/plugins/HJPlugin.cpp`
- [x] `src/plugins/HJMediaFrameDeque.cpp`

## 核心关系

```text
upstream plugin
  -> downstream.deliver(srcKeyHash, frame)
    -> HJMediaFrameDeque::deliver(frame)
    -> downstream.onInputUpdated()
    -> downstream.postTask()
      -> handler asyncAndClear(runTaskId)
        -> downstream.runTask()
          -> receive(srcKeyHash)
          -> process / flush / eof
          -> deliverToOutputs(frame)
```

最短理解：

- `deliver()` 是上游把帧推入下游输入队列。
- `receive()` 是下游在自己的 `runTask()` 里从输入队列取帧。
- `postTask()` 把 `runTask()` 投递到插件 handler，并用 `asyncAndClear` 避免同一插件重复堆积调度任务。
- `flush()` 清空输入队列，并插入 clear frame 语义，随后触发调度。
- `deliverToOutputs()` 把处理后的帧继续投递给所有下游插件。

## 生命周期状态图

```text
NONE
  -> init()
    -> Inited
      -> postTask()
        -> runTask()
          -> Ready / Running
            -> setPause(true)  -> Paused
            -> setPause(false) -> Ready
            -> flush()         -> Ready + clear frame
            -> EOF frame       -> EOF
            -> error           -> Exception
      -> done()
        -> Done
        -> internalRelease()
```

注意：状态图不是严格的单线状态机。不同插件可能在 `runTask()` 中根据 codec、timeline、队列、EOF 调整为 `Ready`、`Running`、`EOF`、`Stoped` 或 `Exception`。排查时要看具体插件实现。

## 日志点设计

| 函数 | 建议字段 | 用于定位 |
|---|---|---|
| `init()` / `afterInit()` | plugin name、status、thread id、handler 是否存在、createThread、输入/输出数量 | 插件是否完成初始化，是否拥有调度线程 |
| `deliver()` | plugin name、srcKeyHash、src plugin、media type、track id、pts/dts、duration、frame type、queue size before/after、status、thread id | 上游是否真的把帧推到了下游；队列是否持续增长 |
| `receive()` | plugin name、srcKeyHash、pts/dts、frame type、queue size after、status、thread id | `runTask()` 是否在消费输入队列；消费速度是否跟得上 |
| `flush()` | plugin name、srcKeyHash、queue size before/after、是否 store clear frame、status、thread id | seek/切流后旧帧是否被清掉；flush 是否到达目标插件 |
| `runTask()` enter | plugin name、status、paused、input size、delay、thread id、route | handler 是否调度到了插件；是否因为 pause/status/空队列退出 |
| `runTask()` exit | plugin name、ret、next delay、status、processed frame pts、output count、cost ms | 插件是否卡在处理、是否反复返回 delay、是否进入异常 |
| `deliverToOutputs()` | plugin name、output plugin、media type、track id、pts/dts、frame type、output count、ret | 下游是否收到了帧；某个输出插件是否 deliver 失败 |
| `done()` / `internalRelease()` | plugin name、status、queue size、handler/thread 是否释放、thread id | teardown 顺序是否正确；旧任务是否仍可能访问插件 |

字段优先级：先打印能串起链路的字段，再打印性能字段。

```text
第一优先级：plugin name、action、status、thread id、frame pts、frame type、queue size
第二优先级：src/dst plugin、srcKeyHash、track id、media type、ret、delay
第三优先级：duration、sample count、video key frame、cost ms、route
```

## 卡住位置判断

### 1. 有 `deliver()`，没有 `runTask()`

常见原因：

- 插件没有 handler，或者 `init()` 时没有传 `thread/createThread`。
- `postTask()` 没有被触发。
- 插件已经 `Done`，`getInput()` 返回空。
- handler 已退出或被 `internalRelease()` 置空。

排查重点：`init/afterInit/postTask` 日志、handler 是否存在、status 是否已 `Done`。

### 2. 有 `runTask()`，没有 `receive()`

常见原因：

- `srcKeyHash` 不匹配，输入队列找不到。
- 状态小于 `Inited` 或已经 `Done/EOF/Stoped`。
- 插件 pause 后直接跳过。

排查重点：`runTask()` enter 的 status、paused、inputKeyHash，`receiveInputFrame()` 的 route/ret。

### 3. 有 `receive()`，没有 `deliverToOutputs()`

常见原因：

- codec 处理失败，状态进入 `Exception`。
- 输入帧是 flush/eof 控制帧，走了特殊分支。
- 输出暂时不可用，需要下一轮 `runTask()` drain。

排查重点：`processMediaFrame()` ret、`getOutputFrame()` ret、flush/eof 分支日志。

### 4. 有 `deliverToOutputs()`，下游没收到

常见原因：

- 输出插件 weak_ptr 已失效。
- 下游 `deliver()` 返回错误。
- media type / track id 对应关系不一致。

排查重点：output plugin name、output key、deliver ret、下游 `deliver()` 是否出现。

### 5. flush 后仍播放旧帧

常见原因：

- flush 没有传播到所有下游。
- 某个插件只清了输入队列，内部 codec/FIFO/timeline 没清。
- flush 期间旧的 delayed task 仍然执行。

排查重点：每个插件的 `flush()`、`runFlush()`、内部缓存清理、旧任务 message id。

## 今日 Demo

文件：`studyDemo/day10_plugin_lifecycle_logging.cpp`

编译运行：

```powershell
cd D:\PROJECT\temp\HJMedia
cmake -S studyDemo -B studyDemo/output
cmake --build studyDemo/output --target day10_plugin_lifecycle_logging
.\studyDemo\output\Debug\day10_plugin_lifecycle_logging.exe
```

如果使用单配置生成器，产物可能在：

```powershell
.\studyDemo\output\day10_plugin_lifecycle_logging.exe
```

demo 模拟了一条简化链路：

```text
demuxer -> audio-decoder -> audio-render
```

观察点：

1. 正常媒体帧先进入 `audio-decoder.deliver()`，再由 `runTask()` 调用 `receive()` 消费。
2. `audio-decoder.deliverToOutputs()` 会触发 `audio-render.deliver()` 和 `audio-render.runTask()`。
3. pause 后，输入帧仍可入队，但 `runTask()` 会跳过消费。
4. flush 会清空旧帧，插入 flush 控制帧，并向下游传播。
5. EOF 会从 decoder 继续传到 render。

## 面试表达

问：`deliver()` 和 `receive()` 是什么关系？

答：`deliver()` 是上游调用下游插件的入口，它把帧写入下游插件按源插件区分的输入队列，并触发下游 `postTask()`。`receive()` 是下游插件在自己的 `runTask()` 中从输入队列取帧消费。也就是说，队列归消费者插件管理，上游只负责投递。

问：如何通过日志判断插件链路卡在哪里？

答：按链路顺序看日志断点。只有 `deliver()` 没有 `runTask()`，优先查 handler/postTask/status；有 `runTask()` 没有 `receive()`，查 inputKeyHash、pause、Done/EOF 状态；有 `receive()` 没有 `deliverToOutputs()`，查 codec/process/flush/eof 分支；有 `deliverToOutputs()` 但下游没有 `deliver()`，查输出 weak_ptr、media type/track id 和 deliver 返回值。

问：插件日志最少应该打印哪些字段？

答：最少要有 plugin name、action、status、thread id、frame pts、frame type、queue size、src/dst plugin 和 ret。这样可以同时串起“帧在哪里”“队列是否堆积”“在哪个线程执行”“状态是否允许继续处理”这几条线索。

## 今日总结

Plugin 的核心不是单个处理函数，而是输入队列、handler 调度和输出转发组成的一条链路。`deliver()` 证明上游推到了下游，`receive()` 证明下游开始消费，`runTask()` 证明调度线程在工作，`deliverToOutputs()` 证明处理结果继续向后流动，`flush()` 和 EOF 则验证控制帧是否沿链路传播。排查卡顿时不要只看某一个插件，要按这些日志点从上游到下游逐段找第一个断点。
