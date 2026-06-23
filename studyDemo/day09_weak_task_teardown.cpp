/*
 * Day 09：延迟任务 teardown 风险实践。
 *
 * 这个 demo 对比两种 teardown 场景：
 * - 不安全：延迟任务捕获裸指针，对象 close 后任务仍然执行；
 * - 安全：延迟任务捕获 weak_ptr，对象销毁后任务自动跳过。
 *
 * 学习计划：study/week2-thread-plugin-player-practice.md
 * 学习笔记：
 * - studyNote/09-thread-teardown-risk.md
 * - studyNote/week2-thread-plugin-player-notes.md Day 9
 *
 * HJMedia 参考源码：
 * - src/utils/HJThread/doc/HJMessageQueue.md
 * - src/utils/HJThread/doc/HJMessage.md
 * - src/graphs/HJGraphMusicPlayer.cpp
 */

#include "study_demo_common.h"

#include <atomic>
#include <memory>

using namespace std::chrono_literals;

namespace {

class PlayerSession : public std::enable_shared_from_this<PlayerSession> {
public:
    using Ptr = std::shared_ptr<PlayerSession>;

    explicit PlayerSession(std::string name)
        : name_(std::move(name))
    {
        hjstudy::logLine(name_, "创建");
    }

    ~PlayerSession()
    {
        hjstudy::logLine(name_, "销毁");
    }

    std::weak_ptr<PlayerSession> weak()
    {
        return shared_from_this();
    }

    void close()
    {
        closed_.store(true);
        hjstudy::logLine(name_, "关闭");
    }

    void seek(int64_t timestampMs)
    {
        if (closed_.load()) {
            hjstudy::logFields(
                name_,
                "旧 delayed seek 访问了已关闭的 session",
                {{"timestampMs", std::to_string(timestampMs)}});
            return;
        }

        hjstudy::logFields(name_, "seek", {{"timestampMs", std::to_string(timestampMs)}});
    }

private:
    std::string name_;
    std::atomic_bool closed_{false};
};

void runUnsafeRawPointerCase(hjstudy::TaskRunner& handler)
{
    hjstudy::printTitle("不安全场景：捕获裸指针");

    auto session = std::make_shared<PlayerSession>("unsafe-player");
    auto* rawSession = session.get();

    handler.postDelayed(50ms, [rawSession] {
        // 真实业务里，这个裸指针此时可能已经指向释放后的内存。
        // demo 为了稳定观察现象，故意让 shared_ptr 多活一会儿，
        // 这样可以看到“旧任务访问已 close 对象”，而不是直接制造未定义行为。
        rawSession->seek(3000);
    });

    session->close();
    std::this_thread::sleep_for(90ms);
}

void runSafeWeakPointerCase(hjstudy::TaskRunner& handler)
{
    hjstudy::printTitle("安全场景：捕获 weak_ptr");

    auto session = std::make_shared<PlayerSession>("safe-player");
    std::weak_ptr<PlayerSession> weakSession = session->weak();

    handler.postDelayed(50ms, [weakSession] {
        if (auto locked = weakSession.lock()) {
            locked->seek(5000);
        } else {
            hjstudy::logLine("graph-handler", "session 已释放，跳过旧 delayed seek");
        }
    });

    session->close();
    session.reset();
    std::this_thread::sleep_for(90ms);
}

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week2-thread-plugin-player-practice.md Day 9",
        "studyNote/09-thread-teardown-risk.md",
        {
            "src/utils/HJThread/doc/HJMessageQueue.md",
            "src/utils/HJThread/doc/HJMessage.md",
            "src/graphs/HJGraphMusicPlayer.cpp",
        });

    hjstudy::TaskRunner handler;

    runUnsafeRawPointerCase(handler);
    runSafeWeakPointerCase(handler);

    handler.stop();
    return 0;
}
