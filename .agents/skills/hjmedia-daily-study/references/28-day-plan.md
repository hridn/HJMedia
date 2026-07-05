# HJMedia 28 天每日学习计划

目标：用 4 周把花椒 HJMedia 学成一个能支撑 C++ 音视频开发求职的项目案例。

每日闭环：阅读 -> 画链路/状态 -> 做实践 -> 写笔记 -> 验证 -> 面试复述。

## Week 1：项目结构与 MusicPlayer

| Day | 主题 | 阅读 | 实践 | 产出 | 验收 |
|---|---|---|---|---|---|
| 1 | 项目全局地图 | `README.md`、`CHANGELOG.md`、`CMakeLists.txt`、`CMakePresets.json` | 画 `entry -> graphs -> plugins/core/media/comp -> third_party/externals` 分层图 | `studyNote/01-project-map.md`，可选 `studyDemo/day01_project_map.cpp` | 2 分钟说明 HJMedia 和 Graph/Plugin 组织方式 |
| 2 | 智能指针和生命周期 | `src/utils/HJObject.h`，搜索 `HJ_DECLARE_PUWTR`、`Ptr`、`Wtr`、`sharedFrom` | 写 shared/weak 防循环引用 demo 和 RAII demo | `studyDemo/day02_smart_ptr_demo.cpp`、`studyDemo/day02_raii_demo.cpp` | 解释 shared_ptr、weak_ptr、RAII 解决的问题 |
| 3 | MusicPlayer 文档 | `docs/Readme_MusicPlayer.md`、MusicPlayer architecture docs | 画 `demuxer -> decoder -> resampler -> render` 和 open/pause/seek/EOF 控制链 | `studyNote/03-music-player-architecture.md`，可选 `studyDemo/day03_music_player_pipeline.cpp` | 解释 demuxer EOF 和 final EOF 区别 |
| 4 | HJGraphMusicPlayer 源码 | `src/graphs/HJGraphMusicPlayer.h/.cpp`、`HJGraph.h/.cpp` | 标注成员职责，追 `internalInit/openURL/pause/resume/seek` | `studyNote/04-hjgraph-music-player-source.md` | 从源码讲清 Graph 如何组装音频链 |
| 5 | 队列和生产消费 | `src/plugins/doc/HJPlugin.md`、`HJMediaFrameDeque.md` | 写 bounded frame queue demo | `studyDemo/day05_bounded_frame_queue.cpp` | 解释队列、反压、异步消费 |
| 6 | 音频插件链 | `HJTimeline.md`、Demuxer/AudioDecoder/Resampler/Render docs | 写插件职责、输入输出、失败表和 frame 流转伪代码 | `studyNote/06-audio-plugin-chain.md`，可选 `studyDemo/day06_audio_plugin_chain.cpp` | 讲清 demux/decode/resample/render 区别 |
| 7 | 第一周复盘 | Week 1 产物 | 10 个问答、3 分钟 MusicPlayer 稿、3 个未懂问题 | `studyNote/week1-review.md` | 讲清全局结构、MusicPlayer、智能指针、RAII、队列 |

## Week 2：线程模型、插件系统和播放器扩展

| Day | 主题 | 阅读 | 实践 | 产出 | 验收 |
|---|---|---|---|---|---|
| 8 | HJThread 和 task queue | `src/utils/HJThread/doc/README.md`、LooperThread/Looper/Handler docs | 写 task queue + delayed task demo，解释 seek 投递 graph handler | `studyNote/08-hjthread-model.md`、`studyDemo/day08_task_queue_handler.cpp` | 讲清 LooperThread、Looper、Handler |
| 9 | teardown 风险 | HJMessageQueue/HJMessage docs、`HJGraphMusicPlayer.cpp` seek/release | 模拟 close 后 delayed task，使用 weak_ptr 改造 | `studyNote/09-thread-teardown-risk.md`、`studyDemo/day09_weak_task_teardown.cpp` | 回答如何避免旧回调和野指针 |
| 10 | Plugin 生命周期和日志 | `HJPlugin.md`、`HJPluginCodec.md`、`HJMediaFrameDeque.md` | 画生命周期图，为 deliver/receive/flush/runTask 设计日志点 | `studyNote/10-plugin-lifecycle-log-design.md`，可选 `studyDemo/day10_plugin_lifecycle_logging.cpp` | 用日志定位插件链路卡点 |
| 11 | Player Graph 对比 | LivePlayer/VodPlayer/MusicPlayer graph 源码 | 对比 seek、低延迟、追帧、EOF、共同/差异 API | `studyNote/11-player-graphs-compare.md`，可选 `studyDemo/day11_player_graph_compare.cpp` | 解释直播和点播核心差异 |
| 12 | 视频丢帧策略 | Video decoder/render/dropping docs | 模拟生产快于消费；写不丢、丢非关键帧、按时间戳追帧策略 | `studyNote/12-video-dropping-practice.md`、`studyDemo/day12_video_drop_policy.cpp` | 回答直播为何实时性优先 |
| 13 | seek / flush / EOF 定位 | `HJMediaPlayer::seek`、`HJNodeDemuxer::seek`、render flush、`HJMediaNode::flush` | 设计 seek 后旧帧排查；实现 broken/fixed demo；补中文注释 | `studyNote/13-seek-flush-eof-debug.md`、`studyDemo/day13_seek_flush_eof_debug.cpp` | 把 seek 问题讲成完整定位案例 |
| 14 | 第二周复盘 | Week 2 产物 | 15 个线程/插件/播放器问答，5 分钟异步调度稿 | `studyNote/week2-review.md`，可选 `studyDemo/day14_async_review.cpp` | 讲清 Handler、Plugin、seek/flush/EOF/teardown 风险 |

## Week 3：Pusher、编解码、RTMP 与弱网

| Day | 主题 | 阅读 | 实践 | 产出 | 验收 |
|---|---|---|---|---|---|
| 15 | Pusher Graph | `HJGraphPusher.h/.cpp`、`src/entry/pusher`、Harmony API | 画采集 -> 处理 -> 编码 -> mux/interleave -> RTMP | `studyNote/15-pusher-graph.md`，可选 `studyDemo/day15_pusher_graph.cpp` | 讲清推流端基本架构 |
| 16 | PCM / AAC | `src/media` audio/capture/codec，fdk-aac、miniaudio 使用点 | 计算 PCM 字节数，写 1024 samples AAC 输入帧伪代码 | `studyNote/16-audio-capture-aac.md`，可选 `studyDemo/day16_pcm_aac_frame_calc.cpp` | 回答 PCM 和 AAC 帧大小问题 |
| 17 | 视频编码基础 | video capture/codec，平台编码插件 | 画采集到编码路径，解释 SPS/PPS/VPS/IDR/PTS/DTS | `studyNote/17-video-capture-codec.md`，可选 `studyDemo/day17_video_codec_headers.cpp` | 回答 H.264/H.265、关键帧、硬编接口 |
| 18 | Mux / RTMP / 时间戳 | `src/media/muxer`、`src/media/net`、`third_party/librtmp` | 写音视频 packet 按 timestamp 交织输出，设计发送失败策略 | `studyNote/18-rtmp-muxer-timestamp.md`，可选 `studyDemo/day18_av_interleave.cpp` | 回答 RTMP、交织、时间戳、弱网问题 |
| 19 | 弱网队列堆积 | 网络和队列相关实现 | 模拟编码持续生产、网络随机变慢；比较阻塞/丢帧/降码率 | `studyNote/19-weak-network-queue-practice.md`，可选 `studyDemo/day19_network_backpressure.cpp` | 解释推流不能无限缓存 |
| 20 | 渲染 / 美颜 / AI | `src/comp/prio`、`src/comp/rte`、`src/detect`、render/inference entry | 画 GPU 后处理和 AI 检测插入主链路位置 | `studyNote/20-render-inference-overview.md`，可选 `studyDemo/day20_render_inference_overview.cpp` | 说明 GPU/AI 如何插入音视频链路 |
| 21 | 第三周复盘 | Week 3 产物 | 20 个推流/编码/RTMP 问答，5 分钟推流讲稿，弱网案例 | `studyNote/week3-review.md`，可选 `studyDemo/day21_pusher_review.cpp` | 讲清 Pusher、编码、RTMP、弱网实时性冲突 |

## Week 4：求职表达、问题定位与面试

| Day | 主题 | 实践 | 产出 | 验收 |
|---|---|---|---|---|
| 22 | 选择主项目点 | 选择 MusicPlayer/Player/Pusher/Plugin，写 STAR，区分阅读分析和实践模拟 | `studyNote/22-resume-project-point.md`，可选 `studyDemo/day22_star_project_point.cpp` | 项目描述可信，不夸大 |
| 23 | 简历项目描述 | 写简短版、普通版、技术强化版，包含链路、技术点、难点、收益 | `studyNote/23-resume-description.md`，可选 `studyDemo/day23_resume_description.cpp` | 能经得起追问 |
| 24 | 50 个面试问答 | 覆盖 C++、音频、视频、播放器、推流、工程，关联真实模块或 demo | `studyNote/24-interview-qa.md`，可选 `studyDemo/day24_interview_question_bank.cpp` | 每个回答落到模块、链路、风险或验证 |
| 25 | 问题定位案例 1-3 | seek 后旧帧、close 后崩溃、直播卡顿延迟上涨 | `studyNote/25-debugging-playbook-part1.md`，可选 `studyDemo/day25_debug_playbook_part1.cpp` | 把定位讲成工程故事 |
| 26 | 案例 4-5 和源码讲解 | 音频 EOF/UI 进度、弱网内存上涨；做 10 分钟源码走读 | `studyNote/26-debugging-playbook-part2.md`、`26-source-walkthrough.md` | 能做源码走读 |
| 27 | 模拟面试 | 3 分钟介绍、10 分钟深入、自问自答 15 个追问 | `studyNote/27-mock-interview.md`，可选 `studyDemo/day27_mock_interview.cpp` | 自然说明 C++ 音视频能力 |
| 28 | 求职材料打包 | 整理项目描述、介绍稿、源码讲解、问答、定位案例、demo 列表 | `studyNote/28-job-ready-package.md`，可选 `studyDemo/day28_job_ready_package.cpp` | 用 HJMedia 支撑 C++ 音视频岗位面试 |

## 每日笔记模板

```text
日期：
今日阅读：
今日实践：
实践结果：
关键类 / 函数：
我理解的数据流：
我理解的控制流：
线程 / 锁 / 生命周期风险：
可用于面试的一句话：
还没懂的问题：
```