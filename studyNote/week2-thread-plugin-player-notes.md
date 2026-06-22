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

- [ ] `src/utils/HJThread/doc/HJMessageQueue.md`
- [ ] `src/utils/HJThread/doc/HJMessage.md`
- [ ] `src/graphs/HJGraphMusicPlayer.cpp` seek / release 相关逻辑

### 今日实践

```text
weak task demo 文件：
模拟的问题：
不用 weak_ptr 的风险：
使用 weak_ptr 后的行为：
和 HJMedia delayed task 的关系：
```

### close / done / internalRelease 对比

| 操作 | 职责 | 是否完整释放 | 风险 |
|---|---|---|---|
| `close()` |  |  |  |
| `done()` |  |  |  |
| `internalRelease()` |  |  |  |

## Day 10：Plugin 生命周期和日志点设计

### 今日阅读

- [ ] `src/plugins/doc/HJPlugin.md`
- [ ] `src/plugins/doc/HJPluginCodec.md`
- [ ] `src/plugins/doc/HJMediaFrameDeque.md`

### 生命周期图

```text
init -> runTask/process -> pause/resume -> flush -> done -> internalRelease
```

### 日志点设计

| 函数 | 应打印字段 | 用于定位什么问题 |
|---|---|---|
| `deliver()` |  |  |
| `receive()` |  |  |
| `flush()` |  |  |
| `runTask()` |  |  |
| `deliverToOutputs()` |  |  |

## Day 11：LivePlayer / VodPlayer / MusicPlayer 对比

### 今日阅读

- [ ] `src/graphs/HJGraphLivePlayer.h`
- [ ] `src/graphs/HJGraphLivePlayer.cpp`
- [ ] `src/graphs/HJGraphVodPlayer.h`
- [ ] `src/graphs/HJGraphVodPlayer.cpp`
- [ ] `src/graphs/HJGraphMusicPlayer.h`
- [ ] `src/graphs/HJGraphMusicPlayer.cpp`

### 对比表

| Graph | 场景 | 是否支持 seek | 是否追求低延迟 | 是否需要追帧 | EOF 语义 |
|---|---|---|---|---|---|
| MusicPlayer |  |  |  |  |  |
| LivePlayer |  |  |  |  |  |
| VodPlayer |  |  |  |  |  |

### 今日总结


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
