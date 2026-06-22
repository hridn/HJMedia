/*
 * Day 08: Task queue and handler practice.
 *
 * 这个 demo 用一个简化版 Handler 模拟 HJThread 的核心模型：
 * - 主线程只负责投递任务；
 * - worker 线程负责按时间顺序串行执行任务；
 * - delayed task 通过到期时间控制执行时机；
 * - postLatest 模拟 HJLooperThread::Handler::asyncAndClear，用于连续 seek 只保留最后一次。
 *
 * Study plan: study/week2-thread-plugin-player-practice.md
 * Study note: studyNote/08-hjthread-model.md
 *
 * HJMedia reference source:
 * - src/utils/HJThread/doc/README.md
 * - src/utils/HJThread/doc/HJLooperThread.md
 * - src/utils/HJThread/doc/HJLooper.md
 * - src/utils/HJThread/doc/HJHandler.md
 * - src/utils/HJThread/doc/HJMessageQueue.md
 * - src/utils/HJThread/doc/HJMessage.md
 */

#include "study_demo_common.h"

#include <iomanip>

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::milliseconds;

namespace {

Clock::time_point g_start = Clock::now();

// 相对启动时间，方便观察 delayed task 是否按预期时间执行。
long long elapsedMs()
{
    return std::chrono::duration_cast<Ms>(Clock::now() - g_start).count();
}

// 打印 std::thread::id，验证任务实际运行在哪一个线程上。
std::string threadId()
{
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

// 统一日志格式：时间 + 线程 ID + 事件描述。
void logTask(const std::string& msg)
{
    std::cout << std::setw(4) << elapsedMs() << "ms"
              << " [tid=" << threadId() << "] " << msg << '\n';
}

// DemoHandler 是 HJLooperThread::Handler 的简化学习版。
// 真实 HJMedia 中，线程由 HJLooperThread 持有，消息循环由 HJLooper/HJMessageQueue 完成；
// 这里为了单文件 demo 易读，把线程、队列、投递 API 合在一个类里。
class DemoHandler {
public:
    using Task = std::function<void()>;

    explicit DemoHandler(std::string name)
        : name_(std::move(name))
        , worker_(&DemoHandler::loop, this)
    {
        logTask(name_ + " started");
    }

    ~DemoHandler()
    {
        stop();
    }

    DemoHandler(const DemoHandler&) = delete;
    DemoHandler& operator=(const DemoHandler&) = delete;

    // 立即投递任务，对应 HJHandler::post(...)。
    void post(Task task, int id = 0)
    {
        postDelayed(std::move(task), id, 0);
    }

    // 延迟投递任务，对应 HJHandler::postDelayed(...)。
    // delay 只决定任务什么时候到期，不会创建新线程；任务仍然在 worker 线程串行执行。
    void postDelayed(Task task, int id, int delayMs)
    {
        if (!task) {
            return;
        }

        const auto due = Clock::now() + Ms(std::max(delayMs, 0));

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                return;
            }
            tasks_.push(ScheduledTask{
                due,
                sequence_++,
                id,
                std::move(task),
            });
        }
        changed_.notify_one();
    }

    // latest-only 投递模式，对应 HJLooperThread::Handler::asyncAndClear(...)。
    // 典型场景是 seek：连续 seek(10s)、seek(20s)、seek(30s) 时，前两个旧请求没有必要再执行。
    void postLatest(Task task, int id, int delayMs = 0)
    {
        remove(id);
        postDelayed(std::move(task), id, delayMs);
    }

    // 按消息 id 移除还没执行的任务，对应 HJHandler::removeMessages(what)。
    void remove(int id)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, LaterFirst> kept;
        while (!tasks_.empty()) {
            auto item = tasks_.top();
            tasks_.pop();
            if (item.id != id) {
                kept.push(std::move(item));
            }
        }
        tasks_.swap(kept);
    }

    // 停止 worker 线程。这里会清空未执行任务，模拟 looper quit(false) 的效果。
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
        changed_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
        logTask(name_ + " stopped");
    }

private:
    // 一个 ScheduledTask 对应真实 HJThread 中的一个 HJMessage：
    // when/sequence 用于排序，id 对应 message.what，task 对应 message.callback。
    struct ScheduledTask {
        Clock::time_point when;
        std::uint64_t sequence;
        int id;
        Task task;
    };

    // priority_queue 默认是“大顶堆”，这里反过来比较，让最早到期的任务排在 top。
    // 如果到期时间相同，则按投递顺序 sequence 保持 FIFO。
    struct LaterFirst {
        bool operator()(const ScheduledTask& lhs, const ScheduledTask& rhs) const
        {
            if (lhs.when == rhs.when) {
                return lhs.sequence > rhs.sequence;
            }
            return lhs.when > rhs.when;
        }
    };

    // worker 线程的消息循环，对应 HJLooper::loop() + HJMessageQueue::next()。
    // 它会阻塞等待任务；如果队头任务还没到期，就 wait_until 到指定时间。
    void loop()
    {
        while (true) {
            ScheduledTask item;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                changed_.wait(lock, [this] {
                    return stopping_ || !tasks_.empty();
                });

                if (stopping_) {
                    break;
                }

                const auto now = Clock::now();
                const auto nextTime = tasks_.top().when;
                if (now < nextTime) {
                    changed_.wait_until(lock, nextTime);
                    continue;
                }

                // 取出任务后释放锁再执行，避免任务内部再 post/remove 时发生锁重入问题。
                item = tasks_.top();
                tasks_.pop();
            }

            item.task();
        }
    }

    std::string name_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, LaterFirst> tasks_;
    bool stopping_{false};
    std::uint64_t sequence_{0};
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week2-thread-plugin-player-practice.md Day 8",
        "studyNote/08-hjthread-model.md",
        {
            "src/utils/HJThread/doc/README.md",
            "src/utils/HJThread/doc/HJLooperThread.md",
            "src/utils/HJThread/doc/HJLooper.md",
            "src/utils/HJThread/doc/HJHandler.md",
            "src/utils/HJThread/doc/HJMessageQueue.md",
            "src/utils/HJThread/doc/HJMessage.md",
        });

    logTask("main thread begin");

    // graphHandler 模拟播放器 Graph 自己的 handler 线程。
    // 调用线程不直接修改 graph 状态，只把 open/seek/render 等控制动作投递过来。
    DemoHandler graphHandler("graph-handler");
    constexpr int kSeekMsg = 1001;

    graphHandler.post([] {
        logTask("open graph");
    });

    graphHandler.postDelayed([] {
        logTask("delayed preload after 80ms");
    }, 2001, 80);

    graphHandler.post([] {
        logTask("decode one frame");
    });

    // 三次 seek 使用同一个消息 id，postLatest 会先 remove(id)，所以最终只执行最后一次 seek。
    graphHandler.postLatest([] {
        logTask("seek to 10s, should be cleared");
    }, kSeekMsg, 120);

    graphHandler.postLatest([] {
        logTask("seek to 20s, should be cleared");
    }, kSeekMsg, 120);

    graphHandler.postLatest([] {
        logTask("seek to 30s, only latest seek runs");
    }, kSeekMsg, 120);

    graphHandler.postDelayed([] {
        logTask("render frame after seek");
    }, 2002, 180);

    // 等待 worker 执行完延迟任务。真实播放器中通常由生命周期接口控制线程退出。
    std::this_thread::sleep_for(Ms(260));

    graphHandler.stop();
    logTask("main thread end");
    return 0;
}
