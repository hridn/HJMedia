# HJMedia OpenGL API 项目实践学习计划

目标：围绕 HJMedia 实际使用的 OpenGL ES / EGL 调用建立学习路径，不单独背 API，而是从 `src/comp/graphic`、`src/comp/prio`、`src/comp/rte` 的渲染链路中理解 API 为什么出现、在哪个线程/Surface 上执行、输出给哪个下游组件。

## 阅读范围

- `src/comp/graphic/hsys/HJOGEGLCore.cpp`
- `src/comp/graphic/hsys/HJOGRenderEnv.cpp`
- `src/comp/graphic/HJOGEGLSurface.cpp`
- `src/comp/graphic/HJOGCommon.cpp`
- `src/comp/graphic/HJOGShaderProgram.cpp`
- `src/comp/graphic/HJOGCopyShaderStrip.cpp`
- `src/comp/graphic/HJOGFBOCtrl.cpp`
- `src/comp/graphic/HJPBORead.cpp`
- `src/comp/utils/HJFBOCtrlPool.cpp`
- `src/comp/prio/HJPrioComFBOBase.cpp`
- `src/comp/prio/HJPrioComSourceSeries.cpp`
- `src/comp/rte/HJRteComDraw.cpp`
- `src/comp/rte/HJRteComDrawSRFilter.cpp`
- `src/comp/rte/HJRteComDrawDenoiseFilter.cpp`

## API 分组

| 分组 | HJMedia 中的 API | 项目角色 |
|---|---|---|
| EGL 上下文和 Surface | `eglGetDisplay`, `eglInitialize`, `eglChooseConfig`, `eglCreateContext`, `eglCreateWindowSurface`, `eglCreatePbufferSurface`, `eglMakeCurrent`, `eglSwapBuffers`, `eglDestroySurface`, `eglTerminate` | 在 Harmony 渲染线程中建立 GL 上下文，管理窗口 Surface 和 1x1 离屏 Surface |
| 纹理输入 | `glGenTextures`, `glBindTexture`, `glTexParameteri`, `glTexImage2D`, `glDeleteTextures`, `GL_TEXTURE_2D`, `GL_TEXTURE_EXTERNAL_OES` | 普通 RGBA / FBO 输出使用 2D 纹理，Harmony `OH_NativeImage` 外部视频帧使用 OES 纹理 |
| Shader 程序 | `glCreateShader`, `glShaderSource`, `glCompileShader`, `glCreateProgram`, `glAttachShader`, `glLinkProgram`, `glUseProgram`, `glGetUniformLocation`, `glUniform*` | `HJOGShaderProgram` 封装编译、链接和 uniform 设置，业务 shader 处理 copy/gray/blur/SR/denoise |
| 顶点和绘制 | `glGenBuffers`, `glBufferData`, `glVertexAttribPointer`, `glEnableVertexAttribArray`, `glDrawArrays`, `GL_TRIANGLE_STRIP`, `GL_POINTS` | 视频本质是矩形纹理拷贝；人脸点位调试使用点绘制 |
| FBO 离屏链 | `glGenFramebuffers`, `glBindFramebuffer`, `glFramebufferTexture2D`, `glCheckFramebufferStatus`, `glViewport`, `glClearColor`, `glClear` | Prio/RTE 滤镜链的中间画布，输入纹理被 shader 写入新的纹理 |
| 状态和裁剪 | `glEnable`, `glDisable`, `glBlendFunc`, `glScissor`, `glGetIntegerv`, `glGetFloatv`, `glGetError` | 处理透明混合、SR 局部对比、保存/恢复 GL 状态 |
| PBO 回读 | `glReadPixels`, `glMapBufferRange`, `glUnmapBuffer`, `GL_PIXEL_PACK_BUFFER`, `GL_STREAM_READ` | GPU 到 CPU 的异步像素读取，降低直接 `glReadPixels` 的同步阻塞 |

## 7 天专题安排

### Day 29：EGL 上下文、窗口 Surface 和离屏 Surface

阅读：
- `src/comp/graphic/hsys/HJOGEGLCore.cpp`
- `src/comp/graphic/hsys/HJOGRenderEnv.cpp`
- `src/comp/graphic/HJOGEGLSurface.cpp`

实践：
- 运行 `openGL_Study/demo/01-egl-surface-lifecycle.cpp`
- 梳理 `HJOGRenderEnv::priCoreInit` 创建 1x1 offscreen surface 的原因。
- 梳理 `HJOGRenderEnv::priUpdateEglSurface` 如何把窗口创建/变更/销毁映射为 EGLSurface 生命周期。

验收：
- 能解释为什么没有 UI Surface 时仍然需要 `eglCreatePbufferSurface`。
- 能解释 `makeCurrent -> draw -> swap` 的调用顺序。

### Day 30：Texture、Shader 和 FBO 组成的渲染链

阅读：
- `src/comp/graphic/HJOGCommon.cpp`
- `src/comp/graphic/HJOGShaderProgram.cpp`
- `src/comp/graphic/HJOGCopyShaderStrip.cpp`
- `src/comp/graphic/HJOGFBOCtrl.cpp`
- `src/comp/rte/HJRteComDraw.cpp`

实践：
- 运行 `openGL_Study/demo/02-texture-shader-fbo-chain.cpp`
- 对比 `GL_TEXTURE_EXTERNAL_OES` 输入和 `GL_TEXTURE_2D` 中间纹理。
- 追踪 `HJRteComDrawFBO::bind -> shader->draw -> HJRteComDrawFBO::unbind`。

验收：
- 能说明 FBO 为什么是滤镜链的核心。
- 能说明 `uMVPMatrix` 与 `uSTMatrix` 在 HJMedia 中分别影响顶点和纹理坐标。

### Day 31：PBO 双缓冲回读

阅读：
- `src/comp/graphic/HJPBORead.cpp`
- `src/comp/graphic/HJPBOReadWrapper.cpp`
- `src/comp/rte/HJRteComDraw.cpp`

实践：
- 运行 `openGL_Study/demo/03-pbo-readback-pipeline.cpp`
- 复述 `glReadPixels` 写当前 PBO、`glMapBufferRange` 读上一 PBO 的延迟一帧模型。

验收：
- 能解释第一帧为什么返回 `HJ_WOULD_BLOCK`。
- 能说明 PBO 相比直接 `glReadPixels` 的收益和仍然存在的同步风险。

### Day 32：Prio 管线中的 Faceu、Gray、Blur

阅读：
- `src/comp/prio/HJPrioComFaceu.cpp`
- `src/comp/prio/HJPrioComFBOBase.cpp`
- `src/comp/prio/HJPrioComFBOGray.cpp`
- `src/comp/prio/HJPrioComFBOBlur.cpp`
- `src/comp/prio/HJPrioComSourceSeries.cpp`

实践：
- 画出 SourceBridge/OES texture 到 Gray/Blur FBO 再到窗口输出的数据流。
- 标记每个节点的输入 texture、输出 texture、是否需要透明。

验收：
- 能说明 Prio 管线中效果组件如何串联。

### Day 33：RTE 可配置图中的 Draw 节点

阅读：
- `src/comp/rte/HJRteGraphProcConfigSetup.cpp`
- `src/comp/rte/HJRteGraphProcPlaceHolderDefault.cpp`
- `src/comp/rte/HJRteComDraw.cpp`

实践：
- 列出 `HJRteComDrawCopy2DFBO`、`HJRteComDrawCopyOESFBO`、`HJRteComDrawEGLUI_0`、`HJRteComDrawPBOFBO` 的职责。

验收：
- 能说明 RTE 图如何把不同 draw 组件连接成可配置渲染链。

### Day 34：SR / Denoise Shader 参数和状态恢复

阅读：
- `src/comp/rte/HJRteComDrawSRFilter.cpp`
- `src/comp/rte/HJRteComDrawDenoiseFilter.cpp`

实践：
- 记录 SR 的 EASU/RCAS 阶段、uniform 参数、`glScissor` 状态保存和恢复。
- 记录 Denoise 的 kernel offset、spatial weight、range factor。

验收：
- 能说明复杂滤镜的主要差异在 shader 与 uniform，不在 OpenGL 调用数量。

### Day 35：专题复盘和面试复述

实践：
- 用一张图串起 `EGL -> OES/2D Texture -> Shader -> FBO -> Screen/Encoder/PBO`。
- 准备 10 个面试问答：EGL、OES、FBO、PBO、Shader、线程、生命周期、性能、平台差异、问题定位。

验收：
- 能用 3 分钟讲清 HJMedia 使用 OpenGL 的真实场景，不夸大为完整 3D 引擎经验。

## 对应产物

- 学习笔记：`openGL_Study/02-opengl-study-notes.md`
- Demo 1：`openGL_Study/demo/01-egl-surface-lifecycle.cpp`
- Demo 2：`openGL_Study/demo/02-texture-shader-fbo-chain.cpp`
- Demo 3：`openGL_Study/demo/03-pbo-readback-pipeline.cpp`
