#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace hjstudy {

inline void printTitle(std::string_view title)
{
    std::cout << "\n=== " << title << " ===\n";
}

inline void printReferences(
    std::string_view plan,
    std::string_view note,
    std::initializer_list<std::string_view> sourcePaths)
{
    std::cout << "Study plan: " << plan << '\n';
    std::cout << "Study note: " << note << '\n';
    std::cout << "HJMedia references:\n";
    for (const auto path : sourcePaths) {
        std::cout << "  - " << path << '\n';
    }
    std::cout << '\n';
}

inline void logLine(std::string_view component, std::string_view action)
{
    std::cout << "[" << component << "] " << action << '\n';
}

inline void logFields(
    std::string_view component,
    std::string_view action,
    const std::map<std::string, std::string>& fields)
{
    std::cout << "[" << component << "] " << action;
    for (const auto& item : fields) {
        std::cout << " " << item.first << "=" << item.second;
    }
    std::cout << '\n';
}

class ScopeExit {
public:
    explicit ScopeExit(std::function<void()> action)
        : action_(std::move(action))
    {
    }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

    ScopeExit(ScopeExit&& other) noexcept
        : action_(std::move(other.action_))
        , active_(other.active_)
    {
        other.active_ = false;
    }

    ScopeExit& operator=(ScopeExit&&) = delete;

    ~ScopeExit()
    {
        if (active_ && action_) {
            action_();
        }
    }

    void dismiss() noexcept
    {
        active_ = false;
    }

private:
    std::function<void()> action_;
    bool active_{true};
};

enum class MediaType {
    Audio,
    Video,
    Control,
};

inline std::string toString(MediaType type)
{
    switch (type) {
    case MediaType::Audio:
        return "audio";
    case MediaType::Video:
        return "video";
    case MediaType::Control:
        return "control";
    }
    return "unknown";
}

struct Frame {
    int64_t ptsMs{};
    MediaType type{MediaType::Audio};
    bool keyFrame{false};
    int generation{0};
    std::string payload;
};

enum class PushResult {
    Pushed,
    Full,
    Closed,
};

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity)
        : capacity_(capacity)
    {
        if (capacity_ == 0) {
            throw std::invalid_argument("BoundedQueue capacity must be greater than zero");
        }
    }

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    PushResult tryPush(T value)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            return PushResult::Closed;
        }
        if (queue_.size() >= capacity_) {
            return PushResult::Full;
        }
        queue_.push_back(std::move(value));
        notEmpty_.notify_one();
        return PushResult::Pushed;
    }

    PushResult pushDropOldest(T value)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            return PushResult::Closed;
        }
        if (queue_.size() >= capacity_) {
            queue_.pop_front();
        }
        queue_.push_back(std::move(value));
        notEmpty_.notify_one();
        return PushResult::Pushed;
    }

    std::optional<T> waitPop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        notEmpty_.wait(lock, [this] {
            return closed_ || !queue_.empty();
        });
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }

    std::optional<T> tryPop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }

    void close()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        notEmpty_.notify_all();
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::deque<T> queue_;
    bool closed_{false};
};

class TaskRunner {
public:
    TaskRunner()
        : worker_([this] {
            runLoop();
        })
    {
    }

    TaskRunner(const TaskRunner&) = delete;
    TaskRunner& operator=(const TaskRunner&) = delete;

    ~TaskRunner()
    {
        stop();
    }

    bool post(std::function<void()> task)
    {
        return postDelayed(std::chrono::milliseconds{0}, std::move(task));
    }

    bool postDelayed(std::chrono::milliseconds delay, std::function<void()> task)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return false;
        }
        tasks_.push(ScheduledTask{
            Clock::now() + delay,
            nextSequence_++,
            std::move(task),
        });
        changed_.notify_one();
        return true;
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                return;
            }
            stopping_ = true;
            while (!tasks_.empty()) {
                tasks_.pop();
            }
        }
        changed_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    using Clock = std::chrono::steady_clock;

    struct ScheduledTask {
        Clock::time_point due;
        std::uint64_t sequence{};
        std::function<void()> task;
    };

    struct LaterFirst {
        bool operator()(const ScheduledTask& lhs, const ScheduledTask& rhs) const
        {
            if (lhs.due == rhs.due) {
                return lhs.sequence > rhs.sequence;
            }
            return lhs.due > rhs.due;
        }
    };

    void runLoop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (true) {
            if (stopping_ && tasks_.empty()) {
                return;
            }

            if (tasks_.empty()) {
                changed_.wait(lock);
                continue;
            }

            const auto nextDue = tasks_.top().due;
            if (Clock::now() < nextDue) {
                changed_.wait_until(lock, nextDue);
                continue;
            }

            auto task = std::move(tasks_.top().task);
            tasks_.pop();
            lock.unlock();
            task();
            lock.lock();
        }
    }

    std::mutex mutex_;
    std::condition_variable changed_;
    std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, LaterFirst> tasks_;
    std::thread worker_;
    bool stopping_{false};
    std::uint64_t nextSequence_{0};
};

inline std::string yesNo(bool value)
{
    return value ? "yes" : "no";
}

} // namespace hjstudy
