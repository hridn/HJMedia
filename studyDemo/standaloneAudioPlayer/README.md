# Standalone Audio Player Demo

这个目录是一个独立 Windows 音频播放 demo，不依赖 HJMedia 主工程，也不引用 HJMedia 头文件。

设计目标是把第一周学习的 MusicPlayer 主链路落成一个最小可运行程序：

```text
AudioGraph
  -> WavDemuxer
  -> PcmDecoder
  -> PcmResampler
  -> WaveOutRender
  -> Timeline
```

## 和 HJMedia 的对应关系

| HJMedia 概念 | 本 demo 类 | 作用 |
|---|---|---|
| `HJGraphMusicPlayer` | `AudioGraph` | 组装插件链，驱动播放流程 |
| `HJPluginDemuxer` | `WavDemuxer` | 解析 RIFF/WAV，产出 PCM packet |
| `HJPluginAudioFFDecoder` | `PcmDecoder` | PCM WAV 不需要真正解码，这里保留 decoder 阶段做透传 |
| `HJPluginAudioResampler` | `PcmResampler` | 保证输出为 waveOut 可播放格式；支持 8-bit PCM 转 16-bit PCM |
| `HJPluginAudioRender` | `WaveOutRender` | 使用 Windows `waveOut` 播放 PCM |
| `HJTimeline` | `Timeline` | 只在 render 完整消费一帧后推进播放进度 |
| `HJMediaFrameDeque` | `FrameQueue<T>` | 串联上下游插件，表达生产/消费边界 |

## 支持范围

- Windows only
- PCM WAV
- 8-bit unsigned PCM 和 16-bit signed PCM
- 命令行播放
- 不支持 MP3/AAC，因为那需要 FFmpeg、Media Foundation 或其它解码库

## 构建

在仓库根目录执行：

```powershell
cmake -S standaloneAudioPlayer -B standaloneAudioPlayer/build-msvc
cmake --build standaloneAudioPlayer/build-msvc --config Debug
```

如果 `cmake` 不在 PATH，可以使用本机已安装的 CMake，例如 CLion 自带 CMake。

如果用 CLion 自带 MinGW 的 `g++.exe` 直接编译，必须链接 Windows 多媒体库 `winmm`：

```powershell
g++.exe standaloneAudioPlayer\src\main.cpp -std=c++17 -o standaloneAudioPlayer\build-mingw\standalone_audio_player.exe -static -static-libgcc -static-libstdc++ -lwinmm -municode
```

也可以进入目录后运行脚本：

```powershell
cd standaloneAudioPlayer
build_mingw.bat
```

如果漏掉 `-lwinmm`，会出现 `undefined reference to __imp_waveOutOpen`、`__imp_waveOutWrite`、`__imp_waveOutClose` 等链接错误。

## 运行

不传参数时，程序会在当前工作目录生成一个 2 秒 440Hz 测试 WAV，然后播放它：

```powershell
standaloneAudioPlayer/build-msvc/Debug/standalone_audio_player.exe
```

也可以传入自己的 PCM WAV。这里的路径必须是真实存在的文件；如果路径里有中文、空格或 `&`，必须加双引号：

```powershell
standaloneAudioPlayer/build-msvc/Debug/standalone_audio_player.exe "D:\audio\李克勤&王赫野-秒针.wav"
```

`build_mingw.bat` 默认使用静态链接，通常可以直接运行 `build-mingw\standalone_audio_player.exe`。如果直接运行时仍提示缺 DLL，用脚本运行会临时加入 CLion MinGW 的 `bin` 目录：

```powershell
cd standaloneAudioPlayer
run_mingw.bat
run_mingw.bat "D:\audio\李克勤&王赫野-秒针.wav"
```

运行日志会体现主链路：

```text
[graph] connect WavDemuxer -> PcmDecoder -> PcmResampler -> WaveOutRender
[wav-demuxer] open ...
[pcm-decoder] PCM wav requires no codec decode; forward pcm frame
[pcm-resampler] format already render-ready ...
[waveout-render] consume pcm bytes=...
[timeline] update currentMs=...
[waveout-render] final playback eof ...
```

## 学习重点

1. `Timeline` 只由 `WaveOutRender` 推进，体现“播放进度来自实际消费，不来自 demux/decoder”。
2. `PcmDecoder` 对 PCM WAV 做透传，但保留链路阶段，方便以后替换成 MP3/AAC decoder。
3. `FrameQueue` 虽然是单线程最小实现，但保留了上下游插件通过队列交接 frame 的结构。
4. Demuxer EOF 先进入链路，最终 EOF 在 Render 收到 EOF 并关闭设备后才成立。

## 问题排查记录

### 1. MinGW 链接 `waveOut*` 失败

现象：

```text
undefined reference to `__imp_waveOutOpen'
undefined reference to `__imp_waveOutWrite'
undefined reference to `__imp_waveOutClose'
```

原因：

`waveOutOpen`、`waveOutWrite`、`waveOutClose` 等函数声明在 `<mmsystem.h>`，但实现位于 Windows 多媒体库 `winmm`。直接用 `g++.exe main.cpp -o main.exe` 时只编译了源码，没有链接 `winmm`。

修复：

```powershell
g++.exe standaloneAudioPlayer\src\main.cpp -std=c++17 -o standaloneAudioPlayer\build-mingw\standalone_audio_player.exe -static -static-libgcc -static-libstdc++ -lwinmm -municode
```

`build_mingw.bat` 已经封装了这条命令。

### 2. MinGW 找不到 `g++.exe` 或运行时 DLL

现象：

```text
'g++.exe' 不是内部或外部命令，也不是可运行的程序
```

或者直接运行 MinGW 生成的 exe 时提示缺 DLL。

原因：

CLion 自带的 MinGW 没有加入系统 PATH；动态链接时还可能缺少 MinGW 运行时 DLL。

修复：

- `build_mingw.bat` 会优先使用 PATH 中的 `g++`，找不到时自动使用 CLion 自带的 `g++.exe`。
- 构建参数加入 `-static -static-libgcc -static-libstdc++`，减少对外部 MinGW DLL 的依赖。
- `run_mingw.bat` 会临时把 CLion MinGW 的 `bin` 加入 PATH，作为运行兜底。

### 3. 改成 `wmain` 后出现 `undefined reference to WinMain`

现象：

```text
undefined reference to `WinMain'
```

原因：

为了支持中文路径，入口函数改成了 `wmain(int argc, wchar_t** argv)`。MinGW 使用宽字符入口时需要额外传入 `-municode`，否则链接器仍按普通 Windows 入口查找。

修复：

`build_mingw.bat` 和 README 中的 MinGW 编译命令都加入了 `-municode`。

### 4. 中文路径或包含 `&` 的路径打不开

现象：

```text
filesystem error: Cannot convert character sequence
failed to open input wav: ...李克勤&王赫野-秒针.wav
```

原因：

MinGW 的 `std::filesystem::path::string()` 和 `std::ifstream` 对中文路径支持不稳定；同时 PowerShell/cmd 中 `&` 是特殊字符，不加引号会被 shell 拆开。

修复：

- 程序入口使用 `wmain` 接收宽字符参数。
- 文件读取改为 Windows API：`CreateFileW` + `ReadFile`。
- 日志路径输出使用 `WideCharToMultiByte(CP_UTF8, ...)` 转成 UTF-8。
- 运行时给路径加双引号：

```powershell
.\build-mingw\standalone_audio_player.exe "D:\audio\李克勤&王赫野-秒针.wav"
```

### 5. 播放下载的音乐时有爆音/杂音

现象：

测试短音频基本正常，但播放较长的自定义 WAV 时出现爆音或杂音。

原因：

早期实现中 `WaveOutRender` 是“写入一个 50ms buffer，然后等待它播放完成，再写下一个 buffer”。这种写一块等一块的方式会在 buffer 之间引入调度间隙，真实音乐中就容易听到爆音、断续或杂音。

修复：

`WaveOutRender` 改成多 buffer 排队：

- 每个 `AudioFrame` 转成一个 `PendingBuffer`。
- 最多提前提交 4 个 `waveOut` buffer。
- 只有队列满时才等待最早的 buffer 完成。
- EOF 到达 Render 后，先 `waitAllBuffers()`，等所有已提交 PCM 播完，再触发 final playback EOF。

这个修改更接近 HJMedia 中 AudioRender 的真实职责：Render 端维护播放缓冲，Timeline 只在实际消费后推进。
