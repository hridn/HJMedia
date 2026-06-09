/*
 * Day 05: Bounded frame queue practice.
 *
 * Study plan: study/week1-music-player-practice.md
 * Study note: studyNote/week1-music-player-notes.md
 *
 * HJMedia reference source:
 * - src/plugins/doc/HJPlugin.md
 * - src/plugins/doc/HJMediaFrameDeque.md
 */

#include "study_demo_common.h"

namespace {

void producer(hjstudy::BoundedQueue<hjstudy::Frame>& queue)
{
    for (int index = 0; index < 6; ++index) {
        hjstudy::Frame frame{index * 20, hjstudy::MediaType::Audio, index == 0, 0, "pcm-frame"};
        const auto result = queue.tryPush(frame);
        hjstudy::logFields(
            "producer",
            "tryPush",
            {
                {"ptsMs", std::to_string(frame.ptsMs)},
                {"result", result == hjstudy::PushResult::Pushed ? "pushed" : "full"},
                {"queueSize", std::to_string(queue.size())},
            });
    }
    queue.close();
}

void consumer(hjstudy::BoundedQueue<hjstudy::Frame>& queue)
{
    while (auto frame = queue.waitPop()) {
        hjstudy::logFields(
            "consumer",
            "pop",
            {{"ptsMs", std::to_string(frame->ptsMs)}, {"type", hjstudy::toString(frame->type)}});
    }
}

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week1-music-player-practice.md Day 5",
        "studyNote/week1-music-player-notes.md Day 5",
        {
            "src/plugins/doc/HJPlugin.md",
            "src/plugins/doc/HJMediaFrameDeque.md",
        });

    hjstudy::BoundedQueue<hjstudy::Frame> queue(3);
    std::thread producerThread(producer, std::ref(queue));
    std::thread consumerThread(consumer, std::ref(queue));

    producerThread.join();
    consumerThread.join();
}
