# 姓名

![个人头像](./resume-avatar.png)

## 个人联系方式

- 手机：
- 邮箱：
- 所在地：
- 求职意向：
- GitHub / 个人主页：

## 教育经历

**深圳大学** ｜ 本科 / 环境工程 ｜ 2019 年 9 月 — 2023 年 6 月

- 主修课程、成绩、奖项或其他相关信息：

## 工作经历

**华为科技有限公司** ｜ 软件开发工程师 ｜ 2024 年 9 月 — 至今
- 华为主题项目，主要负责资源下载模块的相关功能开发与维护、稳定性问题定位与分析。
- 花椒直播鸿蒙版本项目，作为华为鸿蒙技术支持工程师，主要负责开播间，语音房、看播间核心模块的开发、HJMedia自研SDK的鸿蒙适配与接入、稳定性问题分析，性能优化。
- 六间房直播鸿蒙版本项目，作为华为鸿蒙技术支持工程师，独立完成了鸿蒙版本的六间房直播语音房模块的设计与开发，并为其开播功能接入HJMedia鸿蒙版本SDK。
## 项目经历
项目 华为主题 2024年9月 - 2025年5月
技术栈：ArkTS、ArkUI、组件状态管理、UI Ability生命周期管理、RDB关系性数据库、Zip等
负责华为主题资源下载模块的迭代与稳定性治理，核心工作如下：
- 使用 ArkUI 实现下载、暂停、应用等状态管理，并基于 RDB 持久化下载记录；结合 HMS file-download SDK 支持断点续传与状态恢复
- 通过 backgroundTaskManager 与 notificationManager 实现后台下载保活和通知栏进度展示；完成 Lite DRM 解密、Zip 解压及资源落盘流程
- 使用 emitter 解耦下载完成事件与业务模块，结合全链路打点支持质量监控与问题排查
- 使用 DevEco Testing 开展稳定性测试，定位和分析内存泄漏、ArkTS Crash 及 C++ Crash 问题

项目 花椒直播鸿蒙版。 2025年6月 - 2026年1月
技术栈：ArkTS、ArkUI、组件状态管理、UI Ability生命周期管理、XComponent、cameraService、agora等。
负责开播间、看播间和语音房核心模块开发，核心工作如下：
- 使用 ArkUI 与 XComponent 实现开播预览，封装 CameraService 管理相机生命周期，并接入 Agora 实现 RTC 连麦
- 基于 Swiper 滑动触发播放器预加载，优化上下滑看播体验；封装 CDN/RTC 播放组件，以 MainMediaComponent 解耦业务与播放，并支持直播布局动态切换
- 完成语音房开播及视频连麦能力，优化 AgoraService 并复用播放器组件，降低看播间与语音房的重复实现
- 通过 Snapshot 堆快照定位 ArkTS 循环引用和 NativeWindow 未及时回收导致的内存泄漏

项目 HJMedia 自研媒体 SDK  2026年2月 - 2026年4月  
技术栈：C++、CMake、HarmonyOS、NAPI、ArkTS、RTMP、FFmpeg/OpenGL
负责 HJMedia SDK 的 HarmonyOS 适配、业务接入及渲染插件开发，核心工作如下：
- 基于 NAPI 桥接 ArkTS 与 C++ SDK，封装上下文、推流/播放器生命周期、预览窗口绑定及播放控制等媒体能力
- 适配 XComponent/NativeWindow 渲染通路，处理窗口生命周期与 Native 资源释放时序，降低开关播、页面切换中的渲染异常和泄漏风险
- 实现 OpenGL ES 镜像 FBO 插件，通过纹理坐标变换输出镜像视频纹理；实现贴纸插件，完成纹理加载、FBO 合成、透明混合及位置缩放，并以独立插件方式接入媒体图

## 技术栈

- 编程语言：C++、ArkTS；具备 C++ Native 与 ArkTS 应用层协同开发经验
- 框架与库：ArkUI、HarmonyOS NAPI、XComponent、OpenGL ES、FFmpeg、Agora RTC、HMS file-download SDK
- 音视频与图形：了解 RTMP 推流、H.264/H.265 与 AAC 编解码、音视频复用及播放链路；能够使用 OpenGL ES、纹理、Shader 与 FBO 实现镜像、贴纸等实时视频特效
- 工具与平台：HarmonyOS、DevEco Studio、DevEco Testing、CMake、Git；熟悉 UIAbility、CameraService、NativeWindow 等 HarmonyOS 平台能力
- AI 工程化与开发流程：熟练使用 Codex、Cloud Code 等 AI 编码工具；具备 Skill 制作与复用能力，实践 SDD/OpenSpec 驱动需求、设计、开发与验证闭环
- 数据库与通信：RDB 关系型数据库、emitter 事件总线、backgroundTaskManager、notificationManager；了解 Lite DRM 解密与 Zip 解压资源处理流程
