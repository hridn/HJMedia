/*
 * Day 05: Bounded frame queue practice.
 *
 * Study plan: study/week1-music-player-practice.md
 * Study note: studyNote/week1-music-player-notes.md
 *
 * HJMedia reference source:
 * - src/plugins/HJPlugin.h/.cpp
 * - src/plugins/HJMediaFrameDeque.h/.cpp
 * - src/plugins/doc/HJPlugin.md
 * - src/plugins/doc/HJMediaFrameDeque.md
 */

#include "study_demo_common.h"

namespace {

struct QueueInfo {
    std::size_t dequeSize{};
    int64_t audioDurationMs{};
    int videoFrames{};
    int videoKeyFrames{};
};

enum class DeliverResult {
    Delivered,
    Full,
    Closed,
};

std::mutex g_logMutex;

std::string toString(DeliverResult result)
{
    switch (result) {
    case DeliverResult::Delivered:
        return "delivered";
    case DeliverResult::Full:
        return "full";
    case DeliverResult::Closed:
        return "closed";
    }
    return "unknown";
}

std::string frameName(const hjstudy::Frame& frame)
{
    std::ostringstream stream;
    stream << hjstudy::toString(frame.type) << "#" << frame.ptsMs;
    if (frame.keyFrame) {
        stream << "(key)";
    }
    return stream.str();
}

void logFields(
    std::string_view component,
    std::string_view action,
    const std::map<std::string, std::string>& fields)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    hjstudy::logFields(component, action, fields);
}

void logQueueInfo(std::string_view component, std::string_view action, const QueueInfo& info)
{
    logFields(
        component,
        action,
        {
            {"audioDurationMs", std::to_string(info.audioDurationMs)},
            {"dequeSize", std::to_string(info.dequeSize)},
            {"videoFrames", std::to_string(info.videoFrames)},
            {"videoKeyFrames", std::to_string(info.videoKeyFrames)},
        });
}

class BoundedFrameQueue {
public:
    explicit BoundedFrameQueue(std::size_t capacity)
        : capacity_(capacity)
    {
        if (capacity_ == 0) {
            throw std::invalid_argument("capacity must be greater than zero");
        }
    }

    BoundedFrameQueue(const BoundedFrameQueue&) = delete;
    BoundedFrameQueue& operator=(const BoundedFrameQueue&) = delete;

    DeliverResult tryDeliver(hjstudy::Frame frame, QueueInfo* info = nullptr)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            copyInfo(info);
            return DeliverResult::Closed;
        }
        if (frames_.size() >= capacity_) {
            copyInfo(info);
            return DeliverResult::Full;
        }

        appendLocked(std::move(frame));
        copyInfo(info);
        notEmpty_.notify_one();
        return DeliverResult::Delivered;
    }

    DeliverResult deliverDropOldest(
        hjstudy::Frame frame,
        std::optional<hjstudy::Frame>* dropped,
        QueueInfo* info = nullptr)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            copyInfo(info);
            return DeliverResult::Closed;
        }

        if (frames_.size() >= capacity_) {
            *dropped = frames_.front();
            removeStatsLocked(frames_.front());
            frames_.pop_front();
        } else {
            dropped->reset();
        }

        appendLocked(std::move(frame));
        copyInfo(info);
        notEmpty_.notify_one();
        return DeliverResult::Delivered;
    }

    std::optional<hjstudy::Frame> preview(QueueInfo* info = nullptr) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        copyInfo(info);
        if (frames_.empty()) {
            return std::nullopt;
        }
        return frames_.front();
    }

    std::optional<hjstudy::Frame> receive(QueueInfo* info = nullptr)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (frames_.empty()) {
            copyInfo(info);
            return std::nullopt;
        }

        hjstudy::Frame frame = std::move(frames_.front());
        frames_.pop_front();
        removeStatsLocked(frame);
        copyInfo(info);
        return frame;
    }

    std::optional<hjstudy::Frame> waitReceive(QueueInfo* info = nullptr)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        notEmpty_.wait(lock, [this] {
            return closed_ || !frames_.empty();
        });

        if (frames_.empty()) {
            copyInfo(info);
            return std::nullopt;
        }

        hjstudy::Frame frame = std::move(frames_.front());
        frames_.pop_front();
        removeStatsLocked(frame);
        copyInfo(info);
        return frame;
    }

    void flush(QueueInfo* info = nullptr)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        frames_.clear();
        info_ = {};
        copyInfo(info);
    }

    void close()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        notEmpty_.notify_all();
    }

private:
    void appendLocked(hjstudy::Frame frame)
    {
        addStatsLocked(frame);
        frames_.push_back(std::move(frame));
        info_.dequeSize = frames_.size();
    }

    void addStatsLocked(const hjstudy::Frame& frame)
    {
        if (frame.type == hjstudy::MediaType::Audio) {
            info_.audioDurationMs += 20;
        } else if (frame.type == hjstudy::MediaType::Video) {
            ++info_.videoFrames;
            if (frame.keyFrame) {
                ++info_.videoKeyFrames;
            }
        }
    }

    void removeStatsLocked(const hjstudy::Frame& frame)
    {
        if (frame.type == hjstudy::MediaType::Audio) {
            info_.audioDurationMs -= 20;
        } else if (frame.type == hjstudy::MediaType::Video) {
            --info_.videoFrames;
            if (frame.keyFrame) {
                --info_.videoKeyFrames;
            }
        }
        info_.dequeSize = frames_.size();
    }

    void copyInfo(QueueInfo* info) const
    {
        if (info) {
            *info = info_;
        }
    }

    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::deque<hjstudy::Frame> frames_;
    QueueInfo info_;
    bool closed_{false};
};

hjstudy::Frame audioFrame(int index)
{
    return hjstudy::Frame{index * 20, hjstudy::MediaType::Audio, false, 0, "pcm-frame"};
}

hjstudy::Frame videoFrame(int index, bool keyFrame)
{
    return hjstudy::Frame{index * 33, hjstudy::MediaType::Video, keyFrame, 0, "video-frame"};
}

void demoBoundedQueue()
{
    hjstudy::printTitle("Demo 1: bounded queue returns Full");

    BoundedFrameQueue queue(3);
    for (int index = 0; index < 5; ++index) {
        QueueInfo info;
        const auto frame = audioFrame(index);
        const auto result = queue.tryDeliver(frame, &info);
        logFields(
            "producer",
            "tryDeliver",
            {
                {"frame", frameName(frame)},
                {"result", toString(result)},
                {"queueSize", std::to_string(info.dequeSize)},
            });
    }

    QueueInfo info;
    const auto head = queue.preview(&info);
    logFields(
        "consumer",
        "preview",
        {
            {"frame", head ? frameName(*head) : "none"},
            {"queueSizeAfterPreview", std::to_string(info.dequeSize)},
        });

    while (auto frame = queue.receive(&info)) {
        logFields(
            "consumer",
            "receive",
            {
                {"frame", frameName(*frame)},
                {"queueSizeAfterReceive", std::to_string(info.dequeSize)},
            });
    }
}

void demoDropOldestAndFlush()
{
    hjstudy::printTitle("Demo 2: drop-oldest policy and flush");

    BoundedFrameQueue queue(4);
    const std::vector<hjstudy::Frame> frames = {
        videoFrame(0, true),
        videoFrame(1, false),
        audioFrame(0),
        audioFrame(1),
        videoFrame(2, true),
        audioFrame(2),
    };

    for (const auto& frame : frames) {
        QueueInfo info;
        std::optional<hjstudy::Frame> dropped;
        queue.deliverDropOldest(frame, &dropped, &info);
        logFields(
            "producer",
            "deliverDropOldest",
            {
                {"dropped", dropped ? frameName(*dropped) : "none"},
                {"frame", frameName(frame)},
                {"queueSize", std::to_string(info.dequeSize)},
            });
    }

    QueueInfo info{};
    queue.preview(&info);
    logQueueInfo("queue", "statsBeforeFlush", info);
    queue.flush(&info);
    logQueueInfo("queue", "statsAfterFlush", info);
}

void demoProducerConsumer()
{
    hjstudy::printTitle("Demo 3: producer/consumer with backpressure retry");

    BoundedFrameQueue queue(2);

    std::thread consumer([&queue] {
        QueueInfo info;
        while (auto frame = queue.waitReceive(&info)) {
            logFields(
                "consumer",
                "waitReceive",
                {
                    {"frame", frameName(*frame)},
                    {"remaining", std::to_string(info.dequeSize)},
                });
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        logFields("consumer", "closed", {{"remaining", "0"}});
    });

    std::thread producer([&queue] {
        for (int index = 0; index < 6;) {
            QueueInfo info;
            const auto frame = audioFrame(index);
            const auto result = queue.tryDeliver(frame, &info);
            logFields(
                "producer",
                "tryDeliver",
                {
                    {"frame", frameName(frame)},
                    {"queueSize", std::to_string(info.dequeSize)},
                    {"result", toString(result)},
                });

            if (result == DeliverResult::Delivered) {
                ++index;
            } else if (result == DeliverResult::Full) {
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
            }
        }
        queue.close();
    });

    producer.join();
    consumer.join();
}

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week1-music-player-practice.md Day 5",
        "studyNote/week1-music-player-notes.md Day 5",
        {
            "src/plugins/HJPlugin.h/.cpp",
            "src/plugins/HJMediaFrameDeque.h/.cpp",
            "src/plugins/doc/HJPlugin.md",
            "src/plugins/doc/HJMediaFrameDeque.md",
        });

    demoBoundedQueue();
    demoDropOldestAndFlush();
    demoProducerConsumer();
}
