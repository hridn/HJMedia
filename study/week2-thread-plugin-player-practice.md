# 第 2 周详细计划：线程模型、插件系统和播放器扩展

本周目标：理解 HJMedia 的线程、handler、插件调度和播放器风险点。

本周实践重点：
- 写 task queue / handler 模拟 demo
- 模拟 delayed task 在 close 后触发的问题
- 设计 seek / flush / EOF 的日志排查方案
- 对比 MusicPlayer、LivePlayer、VodPlayer

## Day 8：HJThread 基础和 task queue 实践

阅读：
- `src/utils/HJThread/doc/README.md`
- `src/utils/HJThread/doc/HJLooperThread.md`
- `src/utils/HJThread/doc/HJLooper.md`
- `src/utils/HJThread/doc/HJHandler.md`

实践：
- 写一个 task queue demo：主线程 post 任务，工作线程串行执行
- 加入 delayed task，观察任务执行顺序
- 用这个 demo 解释为什么 `seek()` 需要投递到 graph handler

产出：
- `studyNote/08-hjthread-model.md`
- `studyNote/cpp-practice/task-queue-demo.cpp`

验收：
- 能讲清楚 LooperThread、Looper、Handler 的关系

## Day 9：teardown 风险实践

阅读：
- `src/utils/HJThread/doc/HJMessageQueue.md`
- `src/utils/HJThread/doc/HJMessage.md`
- `src/graphs/HJGraphMusicPlayer.cpp` 中 seek / release 相关逻辑

实践：
- 修改昨天的 task queue demo，模拟对象 close 后 delayed task 仍然执行
- 用 `weak_ptr` 改造 demo，避免旧任务访问已释放对象
- 写一份 HJMedia 中 close / done / internalRelease 的风险清单

产出：
- `studyNote/09-thread-teardown-risk.md`
- `studyNote/cpp-practice/weak-task-demo.cpp`

验收：
- 能回答异步播放器关闭时如何避免旧回调和野指针

## Day 10：Plugin 生命周期和日志点设计

阅读：
- `src/plugins/doc/HJPlugin.md`
- `src/plugins/doc/HJPluginCodec.md`
- `src/plugins/doc/HJMediaFrameDeque.md`

实践：
- 画出 Plugin 生命周期状态图
- 为 `deliver()`、`receive()`、`flush()`、`runTask()` 设计日志点，不一定修改源码
- 写出每个日志点应该打印哪些字段：plugin name、frame pts、queue size、status、thread id

产出：
- `studyNote/10-plugin-lifecycle-log-design.md`

验收：
- 能解释如何通过日志定位插件链路卡住的位置

## Day 11：LivePlayer / VodPlayer / MusicPlayer 对比

阅读：
- `src/graphs/HJGraphLivePlayer.h`
- `src/graphs/HJGraphLivePlayer.cpp`
- `src/graphs/HJGraphVodPlayer.h`
- `src/graphs/HJGraphVodPlayer.cpp`
- `src/graphs/HJGraphMusicPlayer.h`
- `src/graphs/HJGraphMusicPlayer.cpp`

实践：
- 做一张对比表：是否支持 seek、是否关注低延迟、是否需要追帧、EOF 如何处理
- 找出三个 Graph 的共同 API 和差异 API
- 写出直播播放器卡顿后的处理思路

产出：
- `studyNote/11-player-graphs-compare.md`

验收：
- 能解释直播播放器和点播播放器的核心差异

## Day 12：视频解码、渲染和丢帧策略

阅读：
- `src/plugins/doc/HJPluginVideoFFDecoder.md`
- `src/plugins/doc/HJPluginVideoOHDecoder.md`
- `src/plugins/doc/HJPluginVideoRender.md`
- `src/plugins/doc/HJPluginAVDropping.md`

实践：
- 写一个简单队列堆积模拟：生产速度大于消费速度
- 实现三种策略的伪代码：不丢帧、丢非关键帧、按时间戳追帧
- 分析每种策略对延迟、流畅度、画面完整性的影响

产出：
- `studyNote/12-video-dropping-practice.md`
- `studyNote/cpp-practice/drop-policy-demo.cpp`

验收：
- 能回答为什么直播播放中实时性通常优先于完整性

## Day 13：seek / flush / EOF 问题定位演练

实践：
- 设计一个“seek 后旧帧被播放”的排查方案
- 写出可疑模块：demuxer、decoder、resampler、render、timeline、queue
- 写出需要加的日志点和预期现象
- 写出修复时最容易引入的新风险

产出：
- `studyNote/13-seek-flush-eof-debug.md`

验收：
- 能把 seek 问题讲成一个完整的问题定位案例

## Day 14：第二周复盘

实践：
- 写 15 个线程 / 插件 / 播放器面试问答
- 用 5 分钟文字稿介绍 HJMedia 的异步调度模型
- 整理第 1~2 周所有 demo 和笔记

产出：
- `studyNote/week2-review.md`

最终验收：
- 能讲清 Handler 模型
- 能讲清 Plugin 生命周期
- 能讲清 seek、flush、EOF、teardown 的风险
