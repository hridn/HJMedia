# HJPlugin

插件基础抽象，负责管理输入/输出连接、任务调度（可选线程）和状态同步。派生类主要实现 `runTask`。

## 关键 API

| 方法 | 说明 | 参数 | 返回值 | 线程说明 |
| ---- | ---- | ---- | ---- | -------- |
| `addInputPlugin(Ptr plugin, HJMediaType type, int trackId)` | 注册上游插件并创建输入队列 | 上游插件、媒体类型、轨道 ID | `HJ_OK`/错误码 | 持有 `m_inputSync` 写锁 |
| `addOutputPlugin(Ptr plugin, HJMediaType type, int trackId)` | 注册下游输出插件 | 下游插件、媒体类型、轨道 ID | `HJ_OK`/错误码 | 持有 `m_outputSync` 写锁 |
| `deliver(size_t srcKeyHash, HJMediaFrame::Ptr& frame)` | 将一帧推入输入队列并触发调度 | 来源哈希、帧 | `HJ_OK`/错误码 | 线程安全 |
| `flush(size_t srcKeyHash)` | 清空输入队列并通知下游 flush | 来源哈希 | `HJ_OK`/错误码 | 线程安全；包含异步 handler 工作 |
| `receive(size_t srcKeyHash, int64_t* size)` | 从输入队列弹出一帧 | 来源哈希、可选的剩余大小输出 | 帧或 `nullptr` | 线程安全 |
| `preview(size_t srcKeyHash)` | 查看队头帧但不弹出 | 来源哈希 | 帧或 `nullptr` | 线程安全 |
| `runTask(int64_t* delay)=0` | 执行派生类实现的调度任务 | 输出下一次延迟时间 | `HJ_OK`/错误码 | 在 handler 线程上运行（如果存在） |
| `deliverToOutputs(HJMediaFrame::Ptr& frame)` | 将帧转发给所有输出插件 | 帧 | void | 通常在 `runTask` 内调用 |
| `setPause(bool pause)` | 切换暂停状态并调用 `onPauseStateChanged` | pause | void | 线程安全 |

> 注意：`Input::eventFlags` 控制 `deliver/flush/receive` 过程中 `reportFrameDequeInfo` 上报哪些统计信息（音频时长、视频帧数、关键帧数等）。当前不会上报队列大小标志。

## 生命周期和线程安全

- `init(param)`：在 `m_sync` 锁下调用。成功后状态变为 `Inited`，并执行 `afterInit`。常见参数包括：`thread`（共享 `HJLooperThread`）、`createThread`（没有共享线程时自动创建）、`pluginListener`（事件回调）。
- 调度：如果 `m_handler` 存在，`postTask` 会调度 `runTask`，并根据返回的 `delay` 重新调度，确保任务在 handler 线程上串行执行。
- 锁：输入/输出 map 分别由 `m_inputSync`/`m_outputSync` 保护；handler 指针由 `m_handlerSync` 保护。派生类必须自行保护自己的资源。
- 关闭：`done()` 将状态设置为 `HJSTATUS_Done`，并调用 `internalRelease` 释放 handler/线程；基类析构函数会调用 `done()`。析构函数还会断开输入/输出连接，并释放弱引用的 graph。

## 使用示例

```cpp
auto plugin = HJCreates<MyPlugin>(); // MyPlugin 派生自 HJPlugin
auto param = HJKeyStorage::create();
(*param)["createThread"] = true;
(*param)["pluginListener"] = myListener;
plugin->init(param);

// 连接上游/下游
plugin->addInputPlugin(demuxer, HJMEDIA_TYPE_AUDIO, 0);
plugin->addOutputPlugin(audioRender, HJMEDIA_TYPE_AUDIO, 0);

// 控制
plugin->setPause(true);
plugin->setPause(false);

plugin->done();
```
