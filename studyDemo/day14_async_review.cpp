/*
 * Day 14: Week 2 async model review — walk through the concept tree.
 *
 * Study plan: study/week2-thread-plugin-player-practice.md
 * Study note: studyNote/week2-review.md
 *             studyNote/week2-thread-plugin-player-notes.md
 *
 * HJMedia reference source:
 * - src/utils/HJThread/doc/README.md          (thread model)
 * - src/plugins/doc/HJPlugin.md                (plugin lifecycle)
 * - src/graphs/HJGraphMusicPlayer.cpp          (seek + teardown)
 * - src/plugins/HJPluginAVDropping.cpp         (drop policy)
 * - src/core/HJMediaPlayer.cc                  (seek entry)
 * - src/core/HJMediaNode.cc                    (flush propagation)
 *
 * This demo revisits the 5 most important concepts from week 2:
 *   Section 1 — deliver / runTask decoupling (Day 10)
 *   Section 2 — asyncAndClear with self-drive (Day 10)
 *   Section 3 — weak_ptr + teardown (Day 9)
 *   Section 4 — seek flush propagation (Day 13)
 *   Section 5 — player comparison (Day 11-12)
 *
 * Each section runs a minimal scenario, observes behaviour, and writes
 * log entries you'd see in a real HJMedia trace.
 */

#include "study_demo_common.h"

#include <cassert>
#include <chrono>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Section 1 — deliver / runTask decoupling
//
// 验证核心理解：deliver() 只做入队不处理，runTask() 在另一线程上消费。
// 两者之间的桥梁就是 postTask / asyncAndClear。
// ---------------------------------------------------------------------------

namespace section1 {

using Clock = std::chrono::steady_clock;

// 模拟一个简化版的 plugin + looper thread 交互。
// - 外部调用 deliver() 向输入队列入队
// - 每次 deliver 触发 postTask()
// - looper 线程消费 runTask() 时通过 receive() 取帧

struct Frame {
    int64_t ptsMs;
    std::string payload;
};

class SimplePlugin : public std::enable_shared_from_this<SimplePlugin> {
public:
    using Ptr = std::shared_ptr<SimplePlugin>;
    using Wtr = std::weak_ptr<SimplePlugin>;

    explicit SimplePlugin(std::string name)
        : name_(std::move(name))
    {
    }

    // 模拟 HJPlugin::deliver — 入队 + 触发调度
    void deliver(Frame frame)
    {
        queue_.push_back(frame);
        hjstudy::logFields(
            name_,
            "deliver",
            {
                {"ptsMs", std::to_string(frame.ptsMs)},
                {"queueSize", std::to_string(queue_.size())},
                {"threadId", section1ThreadId()},
            });
        postTask(0);
    }

    void postTask(int64_t delayMs)
    {
        if (!worker_) {
            worker_ = std::make_unique<SimWorker>();
        }
        Wtr w = shared_from_this();
        worker_->postDelayed(
            [w] {
                auto self = w.lock();
                if (!self)
                    return;
                int64_t delay = 0;
                if (self->runTask(&delay) == 0) {
                    self->postTask(delay);
                }
            },
            delayMs);
    }

    // runTask 返回 0 表示"还能继续处理"，非 0 表示"先停"
    int runTask(int64_t* delayOut)
    {
        if (queue_.empty()) {
            hjstudy::logFields(name_, "runTask", {{"action", "empty-stop"}, {"threadId", section1ThreadId()}});
            *delayOut = 0;
            return -1; // WOULD_BLOCK
        }

        while (!queue_.empty()) {
            auto f = queue_.front();
            queue_.pop_front();
            hjstudy::logFields(
                name_,
                "receive",
                {
                    {"ptsMs", std::to_string(f.ptsMs)},
                    {"queueSizeAfter", std::to_string(queue_.size())},
                    {"threadId", section1ThreadId()},
                });
        }

        // 还有数据可以继续处理
        *delayOut = 0;
        return 0; // HJ_OK
    }

    bool isDone() const { return false; }

private:
    static std::string section1ThreadId()
    {
        std::ostringstream oss;
        oss << std::this_thread::get_id();
        return oss.str();
    }

    struct SimWorker {
        SimWorker()
            : worker([this] { loop(); })
        {
        }
        ~SimWorker()
        {
            {
                std::lock_guard<std::mutex> lk(mtx);
                stopping = true;
                while (!tasks.empty())
                    tasks.pop();
            }
            cv.notify_one();
            worker.join();
        }

        void postDelayed(std::function<void()> task, int64_t delayMs)
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (stopping)
                return;
            tasks.push(ScheduledTask{Clock::now() + std::chrono::milliseconds(delayMs), seq_++, std::move(task)});
            cv.notify_one();
        }

    private:
        struct ScheduledTask {
            Clock::time_point due;
            uint64_t seq;
            std::function<void()> task;
            bool operator>(const ScheduledTask& o) const
            {
                if (due == o.due)
                    return seq > o.seq;
                return due > o.due;
            }
        };
        void loop()
        {
            std::unique_lock<std::mutex> lk(mtx);
            while (true) {
                if (stopping && tasks.empty())
                    return;
                if (tasks.empty()) {
                    cv.wait(lk);
                    continue;
                }
                auto top = tasks.top();
                if (Clock::now() < top.due) {
                    cv.wait_until(lk, top.due);
                    continue;
                }
                tasks.pop();
                lk.unlock();
                top.task();
                lk.lock();
            }
        }
        std::mutex mtx;
        std::condition_variable cv;
        std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, std::greater<ScheduledTask>> tasks;
        std::thread worker;
        bool stopping{false};
        uint64_t seq_{0};
    };

    std::string name_;
    std::deque<Frame> queue_;
    std::unique_ptr<SimWorker> worker_;
};

void run()
{
    hjstudy::printTitle("Section 1: deliver / runTask decoupling");
    hjstudy::logLine("point", "deliver() 和 runTask() 在不同线程上执行，通过 postTask 连接");

    auto plugin = std::make_shared<SimplePlugin>("decoder");
    plugin->deliver(Frame{1000, "frame-A"});
    plugin->deliver(Frame{1033, "frame-B"});
    plugin->deliver(Frame{1066, "frame-C"});

    std::this_thread::sleep_for(60ms);
    hjstudy::logLine("section1", "所有帧在一个 runTask() 中被消费完毕");
}

} // namespace section1

// ---------------------------------------------------------------------------
// Section 2 — asyncAndClear self-drive
//
// 验证核心理解：
// 1. 消息去重不丢帧（帧在输入队列不在消息队列）
// 2. runTask 返回 HJ_OK 会自调度下一轮（不依赖上游重新触发）
// ---------------------------------------------------------------------------

namespace section2 {

class SelfDrivingPlugin : public std::enable_shared_from_this<SelfDrivingPlugin> {
public:
    using Ptr = std::shared_ptr<SelfDrivingPlugin>;
    using Wtr = std::weak_ptr<SelfDrivingPlugin>;

    explicit SelfDrivingPlugin(std::string name)
        : name_(std::move(name))
    {
        worker_ = std::make_unique<hjstudy::TaskRunner>();
    }

    ~SelfDrivingPlugin() { worker_->stop(); }

    void deliver(std::string payload)
    {
        queue_.push_back(std::move(payload));
        hjstudy::logFields(name_, "deliver", {{"queueSize", std::to_string(queue_.size())}});

        // asyncAndClear 模拟：每次 postTask 前清理旧的 runTask 调度请求
        // 这里用 removeSameId 模拟 asyncAndClear
        Wtr w = shared_from_this();
        worker_->postLatest(
            [w, step = step_++] {
                auto self = w.lock();
                if (!self)
                    return;
                // 一次 runTask 消费所有可用帧
                self->runTaskOnce();
            },
            kRunTaskId, 0);
    }

    void runTaskOnce()
    {
        int consumed = 0;
        while (!queue_.empty()) {
            auto p = queue_.front();
            queue_.pop_front();
            hjstudy::logFields(name_, "receive", {{"payload", p}, {"queueSize", std::to_string(queue_.size())}});
            ++consumed;
        }
        hjstudy::logFields(name_, "runTask-done", {{"consumed", std::to_string(consumed)}});

        // 如果还有数据能处理，自调度下一轮
        // 模拟 runTask 返回 HJ_OK → postTask(delay)
        if (!queue_.empty()) {
            Wtr w = shared_from_this();
            worker_->postLatest([w] {
                if (auto self = w.lock())
                    self->runTaskOnce();
            }, kRunTaskId, 0);
        }
    }

    void pushMoreLater()
    {
        // 模拟上游延迟投递
        Wtr w = shared_from_this();
        worker_->postDelayed([w] {
            auto self = w.lock();
            if (!self)
                return;
            self->deliver("late-frame-X");
            self->deliver("late-frame-Y");
        }, 30ms);
    }

    int step() const { return step_; }

private:
    static constexpr int kRunTaskId = 100;
    std::string name_;
    std::deque<std::string> queue_;
    std::unique_ptr<hjstudy::TaskRunner> worker_;
    int step_{0};
};

void run()
{
    hjstudy::printTitle("Section 2: asyncAndClear + self-drive");
    hjstudy::logLine("point", "消息去重只去重调度信号；runTask 返回 HJ_OK 自调度下一轮");

    auto plugin = std::make_shared<SelfDrivingPlugin>("av-dropping");
    plugin->deliver("frame-1");
    plugin->deliver("frame-2");
    plugin->deliver("frame-3");

    // 三帧已经入队，但消息队列里只会有一次 runTask 调度
    hjstudy::logLine("section2", "3 帧入队但 only 1 runTask 调度消息（asyncAndClear 去重）");
    hjstudy::logLine("section2", "runTask 执行时循环 receive 将 3 帧全部消费");

    std::this_thread::sleep_for(40ms);

    // 模拟上游延迟投递新帧 — 自调度已停止（队列空），新帧触发新一轮
    hjstudy::logLine("section2", "上游延迟投递新帧，再次触发 postTask");
    plugin->pushMoreLater();
    std::this_thread::sleep_for(60ms);
}

} // namespace section2

// ---------------------------------------------------------------------------
// Section 3 — weak_ptr teardown
//
// 验证核心理解：旧 delayed task 使用 weak_ptr 跳过已释放对象。
// ---------------------------------------------------------------------------

namespace section3 {

struct RiskSession : public std::enable_shared_from_this<RiskSession> {
    using Ptr = std::shared_ptr<RiskSession>;
    using Wtr = std::weak_ptr<RiskSession>;

    explicit RiskSession(std::string n)
        : name(std::move(n))
    {
        hjstudy::logLine(name, "created");
    }

    ~RiskSession()
    {
        hjstudy::logLine(name, "destroyed");
    }

    void doSeek(int64_t ts)
    {
        if (closed) {
            hjstudy::logFields(name, "WARN: old task on closed session", {{"ts", std::to_string(ts)}});
            return;
        }
        hjstudy::logFields(name, "seek", {{"ts", std::to_string(ts)}});
    }

    Wtr weak() { return shared_from_this(); }

    std::string name;
    bool closed{false};
};

void run()
{
    hjstudy::printTitle("Section 3: weak_ptr + teardown");
    hjstudy::logLine("point", "异步任务捕获 Wtr（weak_ptr），对象释放后自动跳过");

    hjstudy::TaskRunner handler;

    // 安全场景
    {
        auto sess = std::make_shared<RiskSession>("safe-player");
        Wtr w = sess->weak();
        handler.postDelayed(20ms, [w] {
            if (auto locked = w.lock()) {
                locked->doSeek(5000);
            } else {
                hjstudy::logLine("handler", "safe-player already gone, skipping old seek [CORRECT]");
            }
        });
        sess->closed = true;
        sess.reset();
    }

    std::this_thread::sleep_for(40ms);

    // 不安全场景 — 演示 weak_ptr 保护的价值
    hjstudy::logLine("section3", "如果捕获裸指针，旧 seek 会访问已释放内存或已 close 对象");
    hjstudy::logLine("section3", "HJMedia 的 postTask() 内部用 Wtr + lock() 防止此类问题");
    handler.stop();
}

} // namespace section3

// ---------------------------------------------------------------------------
// Section 4 — seek flush propagation
//
// 验证核心理解：seek 需要 preFlush + flush 全链路 + EOF reset + generation gate。
// 检查点：old frame after seek = 0 才算正确。
// ---------------------------------------------------------------------------

namespace section4 {

struct Stage {
    std::string name;
    std::deque<hjstudy::Frame> queue;
    bool eof{false};
    bool preFlush{false};
};

struct SeekOptions {
    bool enablePreFlush;
    bool enableFlush;
    bool enableEofReset;
    bool enableGenerationGate;
};

void runOneCase(const std::string& label, SeekOptions opts, bool expectOldFrames)
{
    hjstudy::logLine("", "");
    hjstudy::printTitle(label);

    int generation = 0;
    int renderedOld = 0;
    int droppedStale = 0;
    int64_t targetPts = 5000;

    Stage demuxer{"demuxer"};
    Stage decoder{"decoder"};
    Stage render{"render"};

    // before seek state
    demuxer.eof = true;
    decoder.queue.push_back(hjstudy::Frame{960, hjstudy::MediaType::Audio, false, 0, "old-cache"});
    render.queue.push_back(hjstudy::Frame{1000, hjstudy::MediaType::Audio, false, 0, "old-render"});
    render.queue.push_back(hjstudy::Frame{-1, hjstudy::MediaType::Control, false, 0, "eof"});

    // seek
    if (opts.enablePreFlush) {
        render.preFlush = true;
        hjstudy::logLine("seek", "setPreFlush(true)");
    }

    if (opts.enableFlush) {
        for (auto* stage : {&demuxer, &decoder, &render}) {
            auto before = stage->queue.size();
            stage->queue.clear();
            stage->eof = false;
            hjstudy::logFields(
                stage->name, "flush",
                {{"queueBefore", std::to_string(before)},
                 {"queueAfter", "0"},
                 {"eof", "reset"}});
        }
    }

    if (opts.enableEofReset) {
        demuxer.eof = false;
        hjstudy::logLine("eof", "flags reset for new generation");
    }

    ++generation;

    // after seek: demuxer produces new frames
    demuxer.queue.push_back(hjstudy::Frame{5000, hjstudy::MediaType::Audio, false, generation, "new-frame"});
    demuxer.queue.push_back(hjstudy::Frame{5020, hjstudy::MediaType::Audio, false, generation, "new-frame"});

    // propagate
    for (auto& f : demuxer.queue) {
        decoder.queue.push_back(f);
    }
    demuxer.queue.clear();
    for (auto& f : decoder.queue) {
        render.queue.push_back(f);
    }
    decoder.queue.clear();

    // render consumes
    render.preFlush = false;

    while (!render.queue.empty()) {
        auto f = std::move(render.queue.front());
        render.queue.pop_front();

        if (opts.enableGenerationGate && f.generation != generation) {
            ++droppedStale;
            hjstudy::logFields("render", "drop-stale",
                               {{"gen", std::to_string(f.generation)},
                                {"pts", std::to_string(f.ptsMs)}});
            continue;
        }

        if (f.payload == "eof") {
            if (opts.enableGenerationGate) {
                ++droppedStale;
                hjstudy::logFields("render", "drop-stale-eof",
                                   {{"gen", std::to_string(f.generation)}});
                continue;
            }
            render.eof = true;
            hjstudy::logFields("render", "eof-consumed", {{"remainingQueue", std::to_string(render.queue.size())}});
            break;
        }

        if (f.ptsMs < targetPts) {
            ++renderedOld;
            hjstudy::logFields("render", "RENDER-OLD-AFTER-SEEK",
                               {{"pts", std::to_string(f.ptsMs)},
                                {"target", std::to_string(targetPts)}});
        } else {
            hjstudy::logFields("render", "render-new", {{"pts", std::to_string(f.ptsMs)}});
        }
    }

    hjstudy::logFields(label, "summary",
                       {{"renderedOldFrames", std::to_string(renderedOld)},
                        {"renderQueueLeft", std::to_string(render.queue.size())},
                        {"generation", std::to_string(generation)}});

    if (expectOldFrames && renderedOld > 0) {
        hjstudy::logLine("verdict", "OK — broken seek reproduced old frames as expected");
    } else if (!expectOldFrames && renderedOld == 0) {
        hjstudy::logLine("verdict", "OK — fixed seek consumed only new generation frames");
    } else {
        hjstudy::logLine("verdict", "UNEXPECTED — check the scenario");
    }
}

void run()
{
    hjstudy::printTitle("Section 4: seek flush propagation");
    hjstudy::logLine("point", "seek 需要 preFlush + flush + EOF reset + generation gate 组合");

    runOneCase("broken-seek (missing all protections)",
               SeekOptions{false, false, false, false},
               true);

    runOneCase("fixed-seek (all protections enabled)",
               SeekOptions{true, true, true, true},
               false);
}

} // namespace section4

// ---------------------------------------------------------------------------
// Section 5 — player comparison
//
// 验证核心理解：不同产品目标 → 不同技术选择。
// 对比 LivePlayer / VodPlayer / MusicPlayer 在 seek、dropping、EOF 上的差异。
// ---------------------------------------------------------------------------

namespace section5 {

struct PlayerTrait {
    std::string name;
    std::string scene;
    bool seekable;
    bool lowLatency;
    bool hasDropping;
    std::string eofBehavior;
};

void run()
{
    hjstudy::printTitle("Section 5: player comparison");
    hjstudy::logLine("point", "产品目标决定技术策略：直播保实时，点播保完整，音乐保 repeat");

    const std::vector<PlayerTrait> players = {
        {"LivePlayer", "直播音视频流", false, true, true,
         "demuxer EOF/codec error → reset demuxer, do NOT end session"},
        {"VodPlayer", "点播音视频", true, false, false,
         "demuxer EOF → wait audio+video render EOF → report graph EOF"},
        {"MusicPlayer", "纯音频播放", true, false, false,
         "demuxer EOF → depends on repeats; final audioRender EOF = graph EOF"},
    };

    for (const auto& p : players) {
        hjstudy::logFields("player", p.name,
                           {{"scene", p.scene},
                            {"seekable", hjstudy::yesNo(p.seekable)},
                            {"lowLatency", hjstudy::yesNo(p.lowLatency)},
                            {"hasDropping", hjstudy::yesNo(p.hasDropping)},
                            {"eof", p.eofBehavior}});
    }

    hjstudy::logLine("", "");
    hjstudy::logLine("takeaway", "LivePlayer 在 demuxer 后接 HJPluginAVDropping 控制延迟");
    hjstudy::logLine("takeaway", "VodPlayer/MusicPlayer 无 dropping，依靠反压和 seek 语义");
    hjstudy::logLine("takeaway", "MusicPlayer EOF 最复杂：repeat 影响 demuxer EOF → 最终 graph EOF 的判断时机");
}

} // namespace section5

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    hjstudy::printReferences(
        "study/week2-thread-plugin-player-practice.md Day 14",
        "studyNote/week2-review.md",
        {
            "src/utils/HJThread/doc/README.md",
            "src/plugins/doc/HJPlugin.md",
            "src/graphs/HJGraphMusicPlayer.cpp",
            "src/plugins/HJPluginAVDropping.cpp",
            "src/core/HJMediaPlayer.cc",
            "src/core/HJMediaNode.cc",
        });

    section1::run();
    section2::run();
    section3::run();
    section4::run();
    section5::run();

    hjstudy::printTitle("Week 2 Review Complete");
    hjstudy::logLine("review", "复习笔记: studyNote/week2-review.md");
    hjstudy::logLine("review", "面试问答: 15 Q&A covering thread/plugin/player/seek/drop/teardown");
    hjstudy::logLine("review", "5-min talk: async dispatch model intro script available in studyNote/week2-review.md");
    return 0;
}
