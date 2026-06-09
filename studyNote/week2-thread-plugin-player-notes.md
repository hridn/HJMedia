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

- [ ] `src/utils/HJThread/doc/README.md`
- [ ] `src/utils/HJThread/doc/HJLooperThread.md`
- [ ] `src/utils/HJThread/doc/HJLooper.md`
- [ ] `src/utils/HJThread/doc/HJHandler.md`

### 今日实践

```text
task queue demo 文件：
验证的问题：
任务如何 post：
任务在哪个线程执行：
delayed task 结果：
和 HJMedia seek handler 的关系：
```

### 模型图

```text
caller thread -> handler -> message queue -> looper thread -> task
```

### 今日总结


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
