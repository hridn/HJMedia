# Day 9：线程 teardown 风险实践

对应计划：`study/week2-thread-plugin-player-practice.md`

## 今日目标

- 理解 delayed task 在 close / done / release 之后仍可能到期执行的风险。
- 用 `studyDemo/day09_weak_task_teardown.cpp` 对比 raw pointer 捕获和 `weak_ptr` 捕获。
- 结合 `HJGraphMusicPlayer::seek()` 理解为什么异步任务里应该捕获弱引用。
- 整理 HJMedia 中 close / done / internalRelease 的职责边界。

## 阅读记录

- [x] `src/utils/HJThread/doc/HJMessageQueue.md`
- [x] `src/utils/HJThread/doc/HJMessage.md`
- [x] `src/graphs/HJGraphMusicPlayer.cpp` seek / release 相关逻辑
- [x] `src/utils/HJSyncObject.h`

## 核心结论

teardown 风险的本质不是“多线程一定会崩”，而是“旧任务的执行时机晚于业务对象的有效期”。只要任务被投递到队列里，调用方就要回答三个问题：

1. 对象已经 close 但还没析构时，旧任务是否允许继续改状态？
2. 对象已经 `done()` / `internalRelease()` 后，旧任务是否还持有裸指针或强引用？
3. 如果任务已经过期，应该靠 remove message、状态检查，还是 `weak_ptr` 失效来阻止执行？

## HJMessageQueue / HJMessage 相关点

`HJMessageQueue` 按 `when` 保存消息。delayed task 不会创建新线程，只是让消息在未来某个时间点变成可分发状态。

队列在 quit 时有两种语义：

- `quit(false)`：清空所有还没执行的消息。
- `quit(true)`：只清掉未来消息，保留已经到期的消息。

当前 `HJLooper::quit()` 走的是 `quit(false)`，所以正常退出 looper 会清掉未执行消息。但这不能替代业务对象自己的防护，因为旧任务可能已经被取出、正在执行，或者被投递到另一个仍然存活的 handler 上。

`HJMessage` 的 `target` 是弱引用。handler 已经销毁时，队列可以丢弃失效 target 的消息。但这只保护“消息目标 handler”，不自动保护 lambda 里捕获的业务对象。lambda 如果捕获裸指针，仍然可能访问已经 close 或释放的对象。

## HJGraphMusicPlayer 中的对应源码

`HJGraphMusicPlayer::seek(int64_t i_timestamp)` 的关键逻辑：

```cpp
HJPluginFFDemuxer::Wtr wDemuxer = m_demuxer;
m_handler->asyncAndClear([wDemuxer, i_timestamp] {
    auto demuxer = wDemuxer.lock();
    if (demuxer) {
        demuxer->seek(i_timestamp);
    }
}, m_seekId);
```

这里同时做了两件事：

- `asyncAndClear(..., m_seekId)`：连续 seek 时清掉同 id 的旧请求，只保留最新一次。
- lambda 捕获 `Wtr`：真正执行时再 `lock()`，demuxer 已释放就跳过，不把旧任务变成对象保活或野指针访问。

`internalRelease()` 会释放音频渲染、重采样、解码、demuxer、线程、timeline，并调用 `m_thread->done()`。这意味着 release 后，graph 内部组件指针已经不可再假设有效。任何异步任务如果绕过弱引用和状态检查，都会变成 teardown 风险。

## 今日 Demo

文件：`studyDemo/day09_weak_task_teardown.cpp`

编译运行：

```powershell
cd D:\PROJECT\temp\HJMedia
cmake -S studyDemo -B studyDemo/output
cmake --build studyDemo/output --target day09_weak_task_teardown
.\studyDemo\output\Debug\day09_weak_task_teardown.exe
```

如果使用单配置生成器，产物可能在：

```powershell
.\studyDemo\output\day09_weak_task_teardown.exe
```

demo 分两段：

```text
unsafe raw pointer case
safe weak_ptr case
```

第一段为了避免真的制造未定义行为，没有让对象立刻析构，而是 close 后保留对象到延迟任务执行。观察点是：旧 delayed seek 仍然碰到了已经 close 的 session。

第二段在延迟任务里捕获 `weak_ptr`，然后 close 并释放 `shared_ptr`。任务到期时 `weak_ptr::lock()` 失败，打印 skip stale delayed seek。

## close / done / internalRelease 对比

| 操作 | 职责 | 是否完整释放 | 风险 |
|---|---|---|---|
| `close()` | 面向业务的停止入口，通常用于停止播放、关闭输入、让上层认为实例不可继续工作 | 不一定。以 `HJGraphMusicPlayer::close()` 当前实现看，它主要做状态检查，真正资源释放不在这里完成 | close 后如果旧 delayed task 仍执行，可能继续 seek、回调、写状态 |
| `done()` | `HJSyncObject` 的生命周期终结入口：调用 `beforeDone()`，加锁把状态置为 `HJSTATUS_Done`，再调用 `internalRelease()` | 是生命周期上的终结入口 | `beforeDone()` 注释说明不在锁内，可能被重复调用；旧任务若不检查 done 状态，仍可能访问释放中的对象 |
| `internalRelease()` | 子类真正释放成员资源：置空 plugin、thread、handler、timeline 等 | 是资源释放实现，但通常不应由外部直接调用 | release 后成员指针失效；异步 lambda 捕获裸指针或 `this` 时风险最大 |

## HJMedia teardown 风险清单

- delayed task 是否捕获了 `this`、裸指针、引用，而不是 `weak_ptr`？
- close / done 后是否移除了可识别的消息 id，例如 seek 使用同一个 `m_seekId`？
- lambda 执行时是否再次检查对象是否存在、是否 done、组件是否为空？
- handler quit 是否会清空队列？是否还有其他 handler 或线程持有旧任务？
- `internalRelease()` 是否先停线程再释放线程上会访问的对象？释放顺序是否清楚？
- event bus / listener 回调是否可能在 player 已关闭后回到上层？
- `HJMessage::obj` 回收会调用 `obj->done()`，调用方是否误以为 payload 仍可复用？
- close、seek、pause、resume 是否都进入同一 graph handler 串行执行？

## 面试表达

问：异步播放器关闭时如何避免旧回调和野指针？

答：第一层是生命周期状态，`done()` 后所有公开接口要通过 `CHECK_DONE_STATUS` 直接拒绝；第二层是消息队列清理，对 seek 这类 latest-only 操作用固定 message id，close/done 时清掉未执行消息；第三层是异步 lambda 不捕获裸指针或 `this`，而是捕获 `weak_ptr`，执行时 `lock()` 成功才访问对象；第四层是资源释放顺序，先停止会访问资源的线程和回调，再释放 plugin、handler、listener 等成员。HJMedia 的 `HJGraphMusicPlayer::seek()` 就是一个例子：它用 `asyncAndClear` 清旧 seek，并在 lambda 里捕获 demuxer 的弱引用。

## 今日总结

第 8 天解决的是“控制操作应该投递到同一 handler 串行化”，第 9 天解决的是“投递出去的旧任务不能越过对象生命周期”。handler 的 weak target 可以防止 handler 自己被旧消息强行保活，但业务对象仍然要自己使用 `weak_ptr`、message id 清理和 done 状态检查。对播放器来说，teardown 不是简单把指针置空，而是要保证 close、seek、callback、release 在同一个生命周期规则下收敛。
