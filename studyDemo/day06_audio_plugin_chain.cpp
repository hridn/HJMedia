/*
 * Day 06: Audio plugin chain practice.
 *
 * Study plan: study/week1-music-player-practice.md
 * Study note: studyNote/week1-music-player-notes.md
 *
 * HJMedia reference source:
 * - src/plugins/doc/HJTimeline.md
 * - src/plugins/doc/HJPluginDemuxer.md
 * - src/plugins/doc/HJPluginAudioFFDecoder.md
 * - src/plugins/doc/HJPluginAudioResampler.md
 * - src/plugins/doc/HJPluginAudioRender.md
 */

#include "study_demo_common.h"

namespace {

enum class FrameKind {
    Packet,
    Pcm,
    Flush,
    Eof,
};

std::string toString(FrameKind kind)
{
    switch (kind) {
    case FrameKind::Packet:
        return "packet";
    case FrameKind::Pcm:
        return "pcm";
    case FrameKind::Flush:
        return "flush";
    case FrameKind::Eof:
        return "eof";
    }
    return "unknown";
}

struct AudioFrame {
    FrameKind kind{FrameKind::Packet};
    int64_t ptsMs{};
    int streamIndex{};
    int samples{};
    std::string payload;
};

std::string frameLabel(const AudioFrame& frame)
{
    std::ostringstream stream;
    stream << toString(frame.kind) << "@pts" << frame.ptsMs << "#s" << frame.streamIndex;
    if (frame.samples > 0) {
        stream << "/" << frame.samples << "samples";
    }
    return stream.str();
}

class FrameQueue {
public:
    FrameQueue(std::string name, std::size_t capacity)
        : name_(std::move(name))
        , capacity_(capacity)
    {
        if (capacity_ == 0) {
            throw std::invalid_argument("capacity must be greater than zero");
        }
    }

    bool tryPush(AudioFrame frame)
    {
        if (frames_.size() >= capacity_) {
            hjstudy::logFields(
                name_,
                "backpressure",
                {
                    {"capacity", std::to_string(capacity_)},
                    {"frame", frameLabel(frame)},
                    {"size", std::to_string(frames_.size())},
                });
            return false;
        }

        frames_.push_back(std::move(frame));
        return true;
    }

    std::optional<AudioFrame> pop()
    {
        if (frames_.empty()) {
            return std::nullopt;
        }

        AudioFrame frame = std::move(frames_.front());
        frames_.pop_front();
        return frame;
    }

    void flush()
    {
        frames_.clear();
    }

    std::size_t size() const
    {
        return frames_.size();
    }

private:
    std::string name_;
    std::size_t capacity_{};
    std::deque<AudioFrame> frames_;
};

class Timeline {
public:
    bool setTimestamp(int streamIndex, int64_t ptsMs, double speed)
    {
        if (streamIndex < streamIndex_ || speed <= 0.0) {
            hjstudy::logFields(
                "timeline",
                "reject-old-anchor",
                {
                    {"incomingStream", std::to_string(streamIndex)},
                    {"storedStream", std::to_string(streamIndex_)},
                });
            return false;
        }

        streamIndex_ = streamIndex;
        timestampMs_ = ptsMs;
        speed_ = speed;
        valid_ = true;
        hjstudy::logFields(
            "timeline",
            "update",
            {
                {"ptsMs", std::to_string(timestampMs_)},
                {"speed", std::to_string(speed_)},
                {"streamIndex", std::to_string(streamIndex_)},
            });
        return true;
    }

    void flush()
    {
        valid_ = false;
        paused_ = false;
        hjstudy::logLine("timeline", "flush: current timestamp becomes invalid until render consumes a new frame");
    }

    void pause()
    {
        paused_ = true;
        hjstudy::logLine("timeline", "pause: timestamp is frozen");
    }

    void play()
    {
        paused_ = false;
        hjstudy::logLine("timeline", "play: timestamp can advance again");
    }

    std::string describe() const
    {
        if (!valid_) {
            return "invalid";
        }

        std::ostringstream stream;
        stream << timestampMs_ << "ms";
        if (paused_) {
            stream << "(paused)";
        }
        return stream.str();
    }

private:
    int streamIndex_{-1};
    int64_t timestampMs_{};
    double speed_{1.0};
    bool valid_{false};
    bool paused_{false};
};

class Demuxer {
public:
    explicit Demuxer(FrameQueue& output)
        : output_(output)
    {
    }

    void openUrl(std::string url)
    {
        mediaUrl_ = std::move(url);
        ++streamIndex_;
        nextPacket_ = 0;
        cached_.reset();
        eofSent_ = false;
        hjstudy::logFields(
            "demuxer",
            "open-url",
            {
                {"streamIndex", std::to_string(streamIndex_)},
                {"url", mediaUrl_},
            });
    }

    void seek(int64_t ptsMs)
    {
        ++streamIndex_;
        nextPacket_ = static_cast<int>(ptsMs / packetStepMs_);
        cached_.reset();
        eofSent_ = false;
        hjstudy::logFields(
            "demuxer",
            "seek",
            {
                {"nextPtsMs", std::to_string(nextPacket_ * packetStepMs_)},
                {"streamIndex", std::to_string(streamIndex_)},
            });
    }

    bool produceOne()
    {
        auto frame = nextFrame();
        if (!frame) {
            return false;
        }

        if (!output_.tryPush(*frame)) {
            cached_ = *frame;
            hjstudy::logFields("demuxer", "cache-one-frame", {{"frame", frameLabel(*frame)}});
            return false;
        }

        hjstudy::logFields("demuxer", "deliver", {{"frame", frameLabel(*frame)}});
        cached_.reset();
        return true;
    }

private:
    std::optional<AudioFrame> nextFrame()
    {
        if (cached_) {
            return cached_;
        }

        if (eofSent_) {
            return std::nullopt;
        }

        if (nextPacket_ >= totalPackets_) {
            eofSent_ = true;
            return AudioFrame{FrameKind::Eof, nextPacket_ * packetStepMs_, streamIndex_, 0, "demux-eof"};
        }

        AudioFrame frame{FrameKind::Packet, nextPacket_ * packetStepMs_, streamIndex_, 1024, "aac-packet"};
        ++nextPacket_;
        return frame;
    }

    FrameQueue& output_;
    std::string mediaUrl_;
    int streamIndex_{-1};
    int nextPacket_{};
    bool eofSent_{false};
    std::optional<AudioFrame> cached_;
    static constexpr int totalPackets_{6};
    static constexpr int packetStepMs_{21};
};

class AudioDecoder {
public:
    AudioDecoder(FrameQueue& input, FrameQueue& output)
        : input_(input)
        , output_(output)
    {
    }

    void flush()
    {
        pending_.clear();
        input_.flush();
        output_.flush();
        hjstudy::logLine("audio-decoder", "flush: clear queued packets and pending decoded frames");
    }

    bool runOne()
    {
        if (!pending_.empty()) {
            return deliverPending();
        }

        auto frame = input_.pop();
        if (!frame) {
            return false;
        }

        if (frame->kind == FrameKind::Flush) {
            flush();
            return true;
        }

        if (frame->kind == FrameKind::Eof) {
            return output_.tryPush(*frame);
        }

        pending_.push_back(AudioFrame{FrameKind::Pcm, frame->ptsMs, frame->streamIndex, 512, "decoded-pcm-a"});
        pending_.push_back(AudioFrame{FrameKind::Pcm, frame->ptsMs + 10, frame->streamIndex, 512, "decoded-pcm-b"});
        hjstudy::logFields(
            "audio-decoder",
            "decode-one-packet",
            {
                {"input", frameLabel(*frame)},
                {"outputs", std::to_string(pending_.size())},
            });
        return deliverPending();
    }

private:
    bool deliverPending()
    {
        if (pending_.empty()) {
            return false;
        }

        if (!output_.tryPush(pending_.front())) {
            return false;
        }

        hjstudy::logFields("audio-decoder", "deliver", {{"frame", frameLabel(pending_.front())}});
        pending_.pop_front();
        return true;
    }

    FrameQueue& input_;
    FrameQueue& output_;
    std::deque<AudioFrame> pending_;
};

class AudioResampler {
public:
    AudioResampler(FrameQueue& input, FrameQueue& output)
        : input_(input)
        , output_(output)
    {
    }

    void flush()
    {
        fifo_.clear();
        input_.flush();
        output_.flush();
        hjstudy::logLine("audio-resampler", "flush: clear converter/fifo state");
    }

    bool runOne()
    {
        auto frame = input_.pop();
        if (!frame) {
            return drainFifo(false);
        }

        if (frame->kind == FrameKind::Flush) {
            flush();
            return true;
        }

        if (frame->kind == FrameKind::Eof) {
            if (!fifo_.empty() && !drainFifo(true)) {
                return false;
            }
            const bool delivered = output_.tryPush(*frame);
            if (delivered) {
                hjstudy::logFields("audio-resampler", "forward-eof", {{"frame", frameLabel(*frame)}});
            }
            return delivered;
        }

        AudioFrame converted = *frame;
        converted.payload = "48k-s16-stereo";
        fifo_.push_back(std::move(converted));
        hjstudy::logFields("audio-resampler", "convert-and-buffer", {{"fifoFrames", std::to_string(fifo_.size())}});
        return drainFifo(false);
    }

private:
    bool drainFifo(bool force)
    {
        if (fifo_.empty()) {
            return false;
        }

        if (!force && fifo_.size() < framesPerPackedOutput_) {
            return false;
        }

        const auto first = fifo_.front();
        std::deque<AudioFrame> consumed;
        int samples = 0;
        int64_t ptsMs = first.ptsMs;
        int count = 0;
        while (!fifo_.empty() && count < framesPerPackedOutput_) {
            samples += fifo_.front().samples;
            consumed.push_back(fifo_.front());
            fifo_.pop_front();
            ++count;
        }

        AudioFrame packed{FrameKind::Pcm, ptsMs, first.streamIndex, samples, "packed-render-pcm"};
        if (!output_.tryPush(packed)) {
            while (!consumed.empty()) {
                fifo_.push_front(consumed.back());
                consumed.pop_back();
            }
            return false;
        }

        hjstudy::logFields(
            "audio-resampler",
            "deliver-packed-frame",
            {
                {"frame", frameLabel(packed)},
                {"remainingFifo", std::to_string(fifo_.size())},
            });
        return true;
    }

    FrameQueue& input_;
    FrameQueue& output_;
    std::deque<AudioFrame> fifo_;
    static constexpr int framesPerPackedOutput_{2};
};

class AudioRender {
public:
    AudioRender(FrameQueue& input, Timeline& timeline)
        : input_(input)
        , timeline_(timeline)
    {
    }

    void flush()
    {
        input_.flush();
        timeline_.flush();
        completed_ = false;
        hjstudy::logLine("audio-render", "flush: clear render queue and timeline");
    }

    bool runOne()
    {
        auto frame = input_.pop();
        if (!frame) {
            return false;
        }

        if (frame->kind == FrameKind::Flush) {
            flush();
            return true;
        }

        if (frame->kind == FrameKind::Eof) {
            completed_ = true;
            hjstudy::logLine("audio-render", "final playback eof: all queued pcm has been consumed");
            return true;
        }

        hjstudy::logFields(
            "audio-render",
            "consume-pcm",
            {
                {"frame", frameLabel(*frame)},
                {"payload", frame->payload},
            });
        timeline_.setTimestamp(frame->streamIndex, frame->ptsMs, 1.0);
        return true;
    }

    bool completed() const
    {
        return completed_;
    }

private:
    FrameQueue& input_;
    Timeline& timeline_;
    bool completed_{false};
};

class AudioPluginChain {
public:
    AudioPluginChain()
        : demuxToDecoder_("demux->decoder", 2)
        , decoderToResampler_("decoder->resampler", 3)
        , resamplerToRender_("resampler->render", 2)
        , demuxer_(demuxToDecoder_)
        , decoder_(demuxToDecoder_, decoderToResampler_)
        , resampler_(decoderToResampler_, resamplerToRender_)
        , render_(resamplerToRender_, timeline_)
    {
    }

    void run()
    {
        hjstudy::printTitle("1. Open and show demuxer backpressure");
        demuxer_.openUrl("song-a.aac");
        demuxer_.produceOne();
        demuxer_.produceOne();
        demuxer_.produceOne();

        hjstudy::printTitle("2. Drain through decoder/resampler/render");
        pumpUntilIdle();

        hjstudy::printTitle("3. Retry cached demuxer frame after downstream drains");
        while (demuxer_.produceOne()) {
            pumpUntilIdle();
        }
        pumpUntilIdle();

        hjstudy::printTitle("4. Seek and flush old plugin state");
        demuxer_.seek(42);
        decoder_.flush();
        resampler_.flush();
        render_.flush();
        demuxer_.produceOne();
        pumpUntilIdle();

        hjstudy::printTitle("5. Play until render-side EOF");
        for (int guard = 0; guard < 64 && !render_.completed(); ++guard) {
            demuxer_.produceOne();
            pumpUntilIdle();
        }

        hjstudy::logFields("timeline", "final", {{"current", timeline_.describe()}});
    }

private:
    void pumpUntilIdle()
    {
        for (int guard = 0; guard < 32; ++guard) {
            bool progressed = false;
            progressed = render_.runOne() || progressed;
            progressed = resampler_.runOne() || progressed;
            progressed = decoder_.runOne() || progressed;
            progressed = render_.runOne() || progressed;

            if (!progressed) {
                break;
            }
        }
    }

    FrameQueue demuxToDecoder_;
    FrameQueue decoderToResampler_;
    FrameQueue resamplerToRender_;
    Timeline timeline_;
    Demuxer demuxer_;
    AudioDecoder decoder_;
    AudioResampler resampler_;
    AudioRender render_;
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week1-music-player-practice.md Day 6",
        "studyNote/week1-music-player-notes.md Day 6",
        {
            "src/plugins/doc/HJTimeline.md",
            "src/plugins/doc/HJPluginDemuxer.md",
            "src/plugins/doc/HJPluginAudioFFDecoder.md",
            "src/plugins/doc/HJPluginAudioResampler.md",
            "src/plugins/doc/HJPluginAudioRender.md",
        });

    AudioPluginChain chain;
    chain.run();
}
