#include <windows.h>
#include <mmsystem.h>

// 独立音频播放器 demo
//
// 这个文件刻意按 HJMedia MusicPlayer 的学习模型组织：
//
//   AudioGraph
//     -> WavDemuxer
//     -> PcmDecoder
//     -> PcmResampler
//     -> WaveOutRender
//     -> Timeline
//
// 但它仍然是独立 demo，不依赖 HJMedia，也不引用 HJMedia 头文件。
// 下面这些类只是用更小的代码模拟相同的架构角色，不是生产代码的拷贝。

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace demo {

constexpr double kPi = 3.14159265358979323846;

// 日志统一使用 [组件] 动作 的格式，让运行输出看起来像一条媒体流水线。
// 这样更容易观察一帧数据经过了哪个阶段。
void logLine(const std::string& component, const std::string& action)
{
    std::cout << "[" << component << "] " << action << '\n';
}

std::string pathForLog(const std::filesystem::path& path)
{
    const auto wide = path.wstring();
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return "<path-convert-failed>";
    }

    std::string result(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
}

// 对应 HJAudioInfo 中这个 demo 需要的最小信息。
// byteRate 和 blockAlign 很重要，因为它们用于在字节数、采样数和播放时间之间换算。
struct AudioFormat {
    std::uint16_t channels{};
    std::uint32_t sampleRate{};
    std::uint16_t bitsPerSample{};
    std::uint16_t blockAlign{};
    std::uint32_t byteRate{};
};

std::string describeFormat(const AudioFormat& format)
{
    return std::to_string(format.sampleRate) + "Hz/"
        + std::to_string(format.channels) + "ch/"
        + std::to_string(format.bitsPerSample) + "bit";
}

enum class FrameKind {
    Audio,
    Eof,
};

// 对应一个最小版 HJMediaFrame：
// - kind 区分普通音频帧和 EOF 这类控制帧。
// - ptsMs 是帧时间戳，后面 Timeline 会用它推进播放进度。
// - data 在这个 demo 中保存原始 PCM 字节。
struct AudioFrame {
    FrameKind kind{FrameKind::Audio};
    AudioFormat format{};
    std::int64_t ptsMs{};
    std::vector<std::uint8_t> data;
};

template <typename T>
class FrameQueue {
public:
    // 在 HJMedia 中，队列位于下游/消费者侧，用来解耦插件。
    // 这个 demo 是单线程的，所以队列故意保持简单；
    // 但它仍然保留了 生产者 -> 队列 -> 消费者 的结构。
    void push(T value)
    {
        frames_.push_back(std::move(value));
    }

    std::optional<T> pop()
    {
        if (frames_.empty()) {
            return std::nullopt;
        }
        T value = std::move(frames_.front());
        frames_.erase(frames_.begin());
        return value;
    }

    bool empty() const
    {
        return frames_.empty();
    }

private:
    std::vector<T> frames_;
};

class Timeline {
public:
    void reset()
    {
        valid_ = false;
        currentMs_ = 0;
    }

    void update(std::int64_t ptsMs)
    {
        // 这是第一周笔记里的关键规则：
        // 播放进度由 Render 在 PCM 被消费后推进，
        // 不是由 Demuxer 在读取字节时推进。
        valid_ = true;
        currentMs_ = std::max(currentMs_, ptsMs);
        std::cout << "[timeline] update currentMs=" << currentMs_ << '\n';
    }

    std::int64_t currentMs() const
    {
        return currentMs_;
    }

    bool valid() const
    {
        return valid_;
    }

private:
    bool valid_{false};
    std::int64_t currentMs_{};
};

std::uint16_t readLe16(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    // WAV/RIFF 字段是小端序。
    // 手动读取可以避免依赖编译器的结构体对齐和 packing 行为。
    if (offset + 2 > data.size()) {
        throw std::runtime_error("unexpected end of file while reading u16");
    }
    return static_cast<std::uint16_t>(data[offset])
        | (static_cast<std::uint16_t>(data[offset + 1]) << 8);
}

std::uint32_t readLe32(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    if (offset + 4 > data.size()) {
        throw std::runtime_error("unexpected end of file while reading u32");
    }
    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

bool fourccEquals(const std::vector<std::uint8_t>& data, std::size_t offset, const char* text)
{
    if (offset + 4 > data.size()) {
        return false;
    }
    return std::memcmp(data.data() + offset, text, 4) == 0;
}

std::vector<std::uint8_t> readAllBytes(const std::filesystem::path& path)
{
    // MinGW 的 std::ifstream 对中文路径支持有问题，
    // 所以这里直接用 CreateFileW + ReadFile 读取文件内容。
    const auto wpath = path.wstring();
    const HANDLE file = CreateFileW(
        wpath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("failed to open input wav: " + pathForLog(path));
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file, &fileSize)) {
        CloseHandle(file);
        throw std::runtime_error("failed to get file size");
    }

    std::vector<std::uint8_t> data(static_cast<std::size_t>(fileSize.QuadPart));
    DWORD bytesRead = 0;
    if (!ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &bytesRead, nullptr)) {
        CloseHandle(file);
        throw std::runtime_error("failed to read wav file");
    }

    CloseHandle(file);
    data.resize(bytesRead);
    return data;
}

void writeLe16(std::ofstream& file, std::uint16_t value)
{
    const std::array<char, 2> bytes{
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
    };
    file.write(bytes.data(), bytes.size());
}

void writeLe32(std::ofstream& file, std::uint32_t value)
{
    const std::array<char, 4> bytes{
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff),
    };
    file.write(bytes.data(), bytes.size());
}

std::filesystem::path createTestWav()
{
    // 如果没有传入音频文件，就生成一个确定可用的 PCM WAV。
    // 这样 demo 不依赖外部资源也能直接运行。
    // 这只是测试输入生成逻辑，不属于播放器主架构。
    const auto path = std::filesystem::current_path() / "test.wav";
    constexpr std::uint16_t channels = 2;
    constexpr std::uint32_t sampleRate = 48000;
    constexpr std::uint16_t bitsPerSample = 16;
    constexpr std::uint16_t blockAlign = channels * bitsPerSample / 8;
    constexpr std::uint32_t byteRate = sampleRate * blockAlign;
    constexpr double seconds = 2.0;
    constexpr std::uint32_t totalSamples = static_cast<std::uint32_t>(sampleRate * seconds);
    constexpr std::uint32_t dataBytes = totalSamples * blockAlign;

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to create test wav");
    }

    file.write("RIFF", 4);
    writeLe32(file, 36 + dataBytes);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    writeLe32(file, 16);
    writeLe16(file, 1);
    writeLe16(file, channels);
    writeLe32(file, sampleRate);
    writeLe32(file, byteRate);
    writeLe16(file, blockAlign);
    writeLe16(file, bitsPerSample);
    file.write("data", 4);
    writeLe32(file, dataBytes);

    for (std::uint32_t i = 0; i < totalSamples; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const auto sample = static_cast<std::int16_t>(std::sin(2.0 * kPi * 440.0 * t) * 9000);
        writeLe16(file, static_cast<std::uint16_t>(sample));
        writeLe16(file, static_cast<std::uint16_t>(sample));
    }

    logLine("app", "created test wav: " + pathForLog(path));
    return path;
}

class WavDemuxer {
public:
    explicit WavDemuxer(FrameQueue<AudioFrame>& output)
        : output_(output)
    {
    }

    void open(const std::filesystem::path& path)
    {
        // Demuxer 的职责：
        // 解析容器，并把压缩包/音频包交给下游。
        // 对 PCM WAV 来说，packet 载荷本身已经是 PCM 字节；
        // 但这里仍然保留 demux 阶段，让架构和 HJMedia 的主链路一致。
        logLine("wav-demuxer", "open " + pathForLog(path));
        const auto bytes = readAllBytes(path);
        if (bytes.size() < 44 || !fourccEquals(bytes, 0, "RIFF") || !fourccEquals(bytes, 8, "WAVE")) {
            throw std::runtime_error("input is not a RIFF/WAVE file");
        }

        bool foundFmt = false;
        bool foundData = false;
        std::size_t dataOffset = 0;
        std::uint32_t dataSize = 0;

        for (std::size_t offset = 12; offset + 8 <= bytes.size();) {
            // RIFF/WAV 由一组 chunk 组成：
            //   "fmt "  描述音频格式
            //   "data"  保存 PCM 字节
            // 文件里也可能有其它 chunk，所以这里扫描 chunk，而不是假设固定偏移。
            const auto chunkSize = readLe32(bytes, offset + 4);
            const auto payloadOffset = offset + 8;
            if (payloadOffset + chunkSize > bytes.size()) {
                throw std::runtime_error("invalid wav chunk size");
            }

            if (fourccEquals(bytes, offset, "fmt ")) {
                const auto audioFormat = readLe16(bytes, payloadOffset);
                if (audioFormat != 1) {
                    throw std::runtime_error("only PCM wav is supported");
                }
                format_.channels = readLe16(bytes, payloadOffset + 2);
                format_.sampleRate = readLe32(bytes, payloadOffset + 4);
                format_.byteRate = readLe32(bytes, payloadOffset + 8);
                format_.blockAlign = readLe16(bytes, payloadOffset + 12);
                format_.bitsPerSample = readLe16(bytes, payloadOffset + 14);
                foundFmt = true;
            } else if (fourccEquals(bytes, offset, "data")) {
                dataOffset = payloadOffset;
                dataSize = chunkSize;
                foundData = true;
            }

            offset = payloadOffset + chunkSize + (chunkSize % 2);
        }

        if (!foundFmt || !foundData || format_.channels == 0 || format_.blockAlign == 0) {
            throw std::runtime_error("wav file misses fmt or data chunk");
        }
        if (format_.bitsPerSample != 8 && format_.bitsPerSample != 16) {
            throw std::runtime_error("only 8-bit and 16-bit PCM wav are supported");
        }

        pcm_.assign(bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset),
            bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset + dataSize));
        cursor_ = 0;

        logLine("wav-demuxer", "parsed format " + describeFormat(format_)
                + ", pcmBytes=" + std::to_string(pcm_.size()));
    }

    bool produceOne()
    {
        if (cursor_ >= pcm_.size()) {
            // 这是 Demuxer EOF：表示源数据已经没有更多字节。
            // 它不等于最终播放 EOF；下游还要收到这个 EOF 控制帧，
            // 并且 Render 必须消费完前面所有数据后，Graph 才能认为播放结束。
            output_.push(AudioFrame{FrameKind::Eof, format_, bytesToMs(cursor_), {}});
            logLine("wav-demuxer", "deliver eof");
            return false;
        }

        const auto remaining = pcm_.size() - cursor_;
        const auto take = std::min<std::size_t>(remaining, targetChunkBytes());
        AudioFrame frame;
        frame.kind = FrameKind::Audio;
        frame.format = format_;
        frame.ptsMs = bytesToMs(cursor_);
        frame.data.assign(pcm_.begin() + static_cast<std::ptrdiff_t>(cursor_),
            pcm_.begin() + static_cast<std::ptrdiff_t>(cursor_ + take));
        cursor_ += take;
        output_.push(std::move(frame));
        logLine("wav-demuxer", "deliver packet ptsMs=" + std::to_string(bytesToMs(cursor_)));
        return true;
    }

private:
    std::size_t targetChunkBytes() const
    {
        // 每次大约产出 50ms 音频。
        // 真实媒体框架通常会根据延迟、缓冲和设备行为调整 packet/frame 大小。
        const auto bytes = static_cast<std::size_t>(format_.byteRate / 20);
        const auto aligned = bytes - (bytes % format_.blockAlign);
        return std::max<std::size_t>(format_.blockAlign, aligned);
    }

    std::int64_t bytesToMs(std::size_t bytes) const
    {
        return static_cast<std::int64_t>((1000.0 * static_cast<double>(bytes)) / format_.byteRate);
    }

    FrameQueue<AudioFrame>& output_;
    AudioFormat format_{};
    std::vector<std::uint8_t> pcm_;
    std::size_t cursor_{};
};

class PcmDecoder {
public:
    PcmDecoder(FrameQueue<AudioFrame>& input, FrameQueue<AudioFrame>& output)
        : input_(input)
        , output_(output)
    {
    }

    bool runOne()
    {
        // Decoder 的职责：
        // 压缩包 -> PCM 帧。
        //
        // PCM WAV 本身已经是解码后的音频，所以这个阶段只是透传。
        // 但保留这个类可以让链路以后更容易替换成 MP3/AAC decoder。
        auto frame = input_.pop();
        if (!frame) {
            return false;
        }
        if (frame->kind == FrameKind::Eof) {
            output_.push(*frame);
            logLine("pcm-decoder", "forward eof");
            return true;
        }
        logLine("pcm-decoder", "PCM wav requires no codec decode; forward pcm frame");
        output_.push(std::move(*frame));
        return true;
    }

private:
    FrameQueue<AudioFrame>& input_;
    FrameQueue<AudioFrame>& output_;
};

class PcmResampler {
public:
    PcmResampler(FrameQueue<AudioFrame>& input, FrameQueue<AudioFrame>& output)
        : input_(input)
        , output_(output)
    {
    }

    bool runOne()
    {
        // Resampler 的职责：
        // 把 PCM 归一化成音频输出设备需要的格式。
        //
        // 这个 demo 只做一个简单转换：8-bit unsigned PCM -> 16-bit signed PCM。
        // 如果输入已经是 16-bit PCM，就直接透传。
        auto frame = input_.pop();
        if (!frame) {
            return false;
        }
        if (frame->kind == FrameKind::Eof) {
            output_.push(*frame);
            logLine("pcm-resampler", "forward eof");
            return true;
        }
        if (frame->format.bitsPerSample == 8) {
            frame = convertU8ToS16(*frame);
            logLine("pcm-resampler", "convert 8-bit unsigned PCM -> 16-bit signed PCM");
        } else {
            logLine("pcm-resampler", "format already render-ready: " + describeFormat(frame->format));
        }
        output_.push(std::move(*frame));
        return true;
    }

private:
    AudioFrame convertU8ToS16(const AudioFrame& input) const
    {
        // WAV 的 8-bit PCM 是无符号格式，静音中心值是 128。
        // 16-bit PCM 是有符号格式，静音中心值是 0。
        AudioFrame output;
        output.kind = input.kind;
        output.ptsMs = input.ptsMs;
        output.format = input.format;
        output.format.bitsPerSample = 16;
        output.format.blockAlign = static_cast<std::uint16_t>(output.format.channels * 2);
        output.format.byteRate = output.format.sampleRate * output.format.blockAlign;
        output.data.reserve(input.data.size() * 2);

        for (const auto value : input.data) {
            const auto sample = static_cast<std::int16_t>((static_cast<int>(value) - 128) << 8);
            output.data.push_back(static_cast<std::uint8_t>(sample & 0xff));
            output.data.push_back(static_cast<std::uint8_t>((sample >> 8) & 0xff));
        }
        return output;
    }

    FrameQueue<AudioFrame>& input_;
    FrameQueue<AudioFrame>& output_;
};

class WaveOutRender {
public:
    WaveOutRender(FrameQueue<AudioFrame>& input, Timeline& timeline)
        : input_(input)
        , timeline_(timeline)
    {
    }

    ~WaveOutRender()
    {
        close();
    }

    bool runOne()
    {
        // Render 的职责：
        // 消费已经适配好的 PCM，并写入平台音频设备。
        // 在 HJMedia 中，这对应平台音频渲染插件。
        auto frame = input_.pop();
        if (!frame) {
            return false;
        }
        if (frame->kind == FrameKind::Eof) {
            // 最终播放 EOF 只有在前面的音频帧都被 Render 消费完，
            // 并且 EOF 控制帧也到达 Render 后才成立。
            waitAllBuffers();
            logLine("waveout-render", "final playback eof, timelineMs="
                    + std::to_string(timeline_.currentMs()));
            close();
            eof_ = true;
            return true;
        }

        ensureOpen(frame->format);
        playFrame(std::move(*frame));
        return true;
    }

    bool eof() const
    {
        return eof_;
    }

private:
    void ensureOpen(const AudioFormat& format)
    {
        // waveOut 是这个学习 demo 中最简单的 Windows 音频输出 API。
        // 生产项目会按平台使用不同 render backend，
        // 但角色相同：接收 PCM，并推送到硬件播放。
        if (device_) {
            return;
        }

        WAVEFORMATEX waveFormat{};
        waveFormat.wFormatTag = WAVE_FORMAT_PCM;
        waveFormat.nChannels = format.channels;
        waveFormat.nSamplesPerSec = format.sampleRate;
        waveFormat.wBitsPerSample = format.bitsPerSample;
        waveFormat.nBlockAlign = format.blockAlign;
        waveFormat.nAvgBytesPerSec = format.byteRate;

        const auto result = waveOutOpen(&device_, WAVE_MAPPER, &waveFormat, 0, 0, CALLBACK_NULL);
        if (result != MMSYSERR_NOERROR) {
            device_ = nullptr;
            throw std::runtime_error("waveOutOpen failed: " + std::to_string(result));
        }
        logLine("waveout-render", "open device " + describeFormat(format));
    }

    void playFrame(AudioFrame frame)
    {
        // 不能“写一块、等一块”。那样每个 50ms buffer 之间会有调度空隙，
        // 播放音乐时容易表现为爆音/杂音。这里保留多个已提交 buffer，
        // 让 waveOut 设备持续消费，接近真实 AudioRender 的缓冲队列行为。
        while (pendingBuffers_.size() >= kMaxQueuedBuffers) {
            waitOneBuffer();
        }

        auto pending = std::make_unique<PendingBuffer>();
        pending->data = std::move(frame.data);
        const auto durationMs = static_cast<std::int64_t>(
            (1000.0 * static_cast<double>(pending->data.size())) / frame.format.byteRate);
        pending->endPtsMs = frame.ptsMs + durationMs;
        pending->header.lpData = reinterpret_cast<LPSTR>(pending->data.data());
        pending->header.dwBufferLength = static_cast<DWORD>(pending->data.size());

        checkWaveOut(waveOutPrepareHeader(device_, &pending->header, sizeof(pending->header)), "waveOutPrepareHeader");
        checkWaveOut(waveOutWrite(device_, &pending->header, sizeof(pending->header)), "waveOutWrite");
        pendingBuffers_.push_back(std::move(pending));
    }

    void waitAllBuffers()
    {
        while (!pendingBuffers_.empty()) {
            waitOneBuffer();
        }
    }

    void waitOneBuffer()
    {
        auto& pending = pendingBuffers_.front();
        while ((pending->header.dwFlags & WHDR_DONE) == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        checkWaveOut(waveOutUnprepareHeader(device_, &pending->header, sizeof(pending->header)), "waveOutUnprepareHeader");
        // 只有当输出 API 报告 buffer 播放完成后才更新 Timeline。
        // 这模拟了“播放进度跟随实际 render 消费”的规则。
        timeline_.update(pending->endPtsMs);
        logLine("waveout-render", "consume pcm bytes=" + std::to_string(pending->data.size()));
        pendingBuffers_.pop_front();
    }

    void close()
    {
        if (!device_) {
            return;
        }
        waveOutReset(device_);
        while (!pendingBuffers_.empty()) {
            auto& pending = pendingBuffers_.front();
            if ((pending->header.dwFlags & WHDR_PREPARED) != 0) {
                waveOutUnprepareHeader(device_, &pending->header, sizeof(pending->header));
            }
            pendingBuffers_.pop_front();
        }
        waveOutClose(device_);
        device_ = nullptr;
        logLine("waveout-render", "close device");
    }

    void checkWaveOut(MMRESULT result, const std::string& operation)
    {
        if (result != MMSYSERR_NOERROR) {
            throw std::runtime_error(operation + " failed: " + std::to_string(result));
        }
    }

    FrameQueue<AudioFrame>& input_;
    Timeline& timeline_;
    HWAVEOUT device_{nullptr};
    bool eof_{false};

    struct PendingBuffer {
        std::vector<std::uint8_t> data;
        WAVEHDR header{};
        std::int64_t endPtsMs{};
    };

    static constexpr std::size_t kMaxQueuedBuffers{4};
    std::deque<std::unique_ptr<PendingBuffer>> pendingBuffers_;
};

class AudioGraph {
public:
    AudioGraph()
        : demuxer_(demuxToDecoder_)
        , decoder_(demuxToDecoder_, decoderToResampler_)
        , resampler_(decoderToResampler_, resamplerToRender_)
        , render_(resamplerToRender_, timeline_)
    {
    }

    void openAndPlay(const std::filesystem::path& path)
    {
        // Graph 的职责：
        // 创建/连接插件，并驱动高层播放生命周期。
        // Graph 持有策略；单个 Plugin 只处理自己这一阶段的事情。
        logLine("graph", "connect WavDemuxer -> PcmDecoder -> PcmResampler -> WaveOutRender");
        timeline_.reset();
        demuxer_.open(path);

        while (!render_.eof()) {
            demuxer_.produceOne();
            pumpUntilIdle();
        }

        logLine("graph", "done, final timeline valid="
                + std::string(timeline_.valid() ? "true" : "false")
                + ", currentMs=" + std::to_string(timeline_.currentMs()));
    }

private:
    void pumpUntilIdle()
    {
        // 这是对 HJMedia 任务调度的单线程简化模拟。
        // 每个 runOne() 最多从输入队列消费一帧，并可能向下游队列产出一帧。
        // 这里持续 pump，直到所有阶段都没有进展。
        for (int guard = 0; guard < 128; ++guard) {
            bool progressed = false;
            progressed = decoder_.runOne() || progressed;
            progressed = resampler_.runOne() || progressed;
            progressed = render_.runOne() || progressed;
            if (!progressed) {
                return;
            }
        }
    }

    FrameQueue<AudioFrame> demuxToDecoder_;
    FrameQueue<AudioFrame> decoderToResampler_;
    FrameQueue<AudioFrame> resamplerToRender_;
    Timeline timeline_;
    WavDemuxer demuxer_;
    PcmDecoder decoder_;
    PcmResampler resampler_;
    WaveOutRender render_;
};

} // namespace demo

int wmain(int argc, wchar_t** argv)
{
    try {
        // 命令行用法：
        //   standalone_audio_player.exe
        //       不传参数时：生成并播放一个 2 秒测试 WAV。
        //
        //   standalone_audio_player.exe "D:\music\李克勤&王赫野-秒针.wav"
        //       传参数时：播放你传入的真实 PCM WAV 路径。
        //       如果路径里有中文、空格或 &，在 PowerShell/cmd 中必须加双引号。
        std::filesystem::path input;
        if (argc >= 2) {
            input = argv[1];
        } else {
            input = demo::createTestWav();
        }

        demo::AudioGraph graph;
        graph.openAndPlay(input);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[app] error: " << error.what() << '\n';
        return 1;
    }
}
