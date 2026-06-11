// clang-format off

#include "SimDBTester.hpp"
#include "simdb/apps/argos/CheckpointPipelineStager.hpp"
#include "simdb/utils/ConcurrentQueue.hpp"

namespace {

using simdb::argos::Action;
using simdb::argos::CheckpointPipelineStager;
using simdb::argos::QueueCollectionData;

constexpr auto kCidBytes = sizeof(uint16_t);
constexpr auto kActionBytes = sizeof(uint8_t);
constexpr uint16_t kCid = 1;
constexpr size_t kHeartbeat = 3;

uint8_t getActionByte(const simdb::argos::CollectedData& data)
{
    const auto& bytes = data.getData();
    EXPECT_TRUE(bytes.size() >= kCidBytes + kActionBytes);
    return static_cast<uint8_t>(bytes[kCidBytes]);
}

std::vector<char> getPayload(const simdb::argos::CollectedData& data)
{
    const auto& bytes = data.getData();
    if (bytes.size() <= kCidBytes + kActionBytes)
    {
        return {};
    }
    return std::vector<char>(bytes.begin() + kCidBytes + kActionBytes, bytes.end());
}

void expectCheckpointAction(const QueueCollectionData& slot, uint16_t cid, Action action)
{
    auto it = slot.checkpoints.find(cid);
    EXPECT_TRUE(it != slot.checkpoints.end());
    auto data = it->second->getMinifiedData();
    EXPECT_EQUAL(getActionByte(*data), static_cast<uint8_t>(action));
}

void expectCheckpointPayload(const QueueCollectionData& slot, uint16_t cid, const std::vector<char>& payload)
{
    auto it = slot.checkpoints.find(cid);
    EXPECT_TRUE(it != slot.checkpoints.end());
    auto data = it->second->getMinifiedData();
    EXPECT_EQUAL(getPayload(*data), payload);
}

void expectSinglePipelineAction(const QueueCollectionData& entry, Action action)
{
    EXPECT_EQUAL(entry.entries.size(), 1u);
    EXPECT_EQUAL(getActionByte(*entry.entries[0]), static_cast<uint8_t>(action));
}

void expectPipelinePayload(const QueueCollectionData& entry, const std::vector<char>& payload)
{
    EXPECT_EQUAL(getPayload(*entry.entries[0]), payload);
}

class TestHarness
{
public:
    TestHarness() :
        stager_(kHeartbeat, &timestamp_, &pipeline_queue_)
    {
        stager_.disableAutoSendMode(true);
        stager_.setScalarType(kCid);
    }

    void setTime(uint64_t time) { sim_time_ = time; }

    void stage(const std::vector<char>& payload) { stager_.stage(kCid, payload); }

    void disable() { stager_.onEnabledChanged(kCid, false); }

    void enable() { stager_.onEnabledChanged(kCid, true); }

    void advanceSlot() { stager_.advanceSimTimeSlot(); }

    void flush() { stager_.sendCollectedDataToPipeline(); }

    const std::vector<QueueCollectionData>& waitingQueue() const { return stager_.getWaitingQueue(); }

    const QueueCollectionData& backSlot() const
    {
        EXPECT_FALSE(waitingQueue().empty());
        return waitingQueue().back();
    }

    void clearWaiting() { stager_.clearWaitingQueue(); }

    bool pop(QueueCollectionData& entry)
    {
        if (!pipeline_queue_.try_pop(entry))
        {
            return false;
        }
        return !entry.entries.empty();
    }

private:
    uint64_t sim_time_ = 0;
    simdb::argos::Timestamp timestamp_{&sim_time_};
    simdb::ConcurrentQueue<QueueCollectionData> pipeline_queue_;
    CheckpointPipelineStager stager_;
};

void testFullThenCarryWaitingQueue()
{
    const std::vector<char> payload{'A', 'B', 'C'};

    TestHarness harness;
    harness.setTime(100);
    harness.stage(payload);
    EXPECT_EQUAL(harness.waitingQueue().size(), 1u);
    expectCheckpointAction(harness.backSlot(), kCid, Action::FULL);
    expectCheckpointPayload(harness.backSlot(), kCid, payload);

    harness.setTime(101);
    harness.stage(payload);
    EXPECT_EQUAL(harness.waitingQueue().size(), 2u);
    expectCheckpointAction(harness.backSlot(), kCid, Action::CARRY);

    harness.setTime(102);
    harness.stage(payload);
    EXPECT_EQUAL(harness.waitingQueue().size(), 3u);
    expectCheckpointAction(harness.backSlot(), kCid, Action::CARRY);

    // One more time. This is a heartbeat boundary, so we should
    // get a FULL snapshot in a new 'waiting queue' entry.
    harness.setTime(103);
    harness.stage(payload);
    EXPECT_EQUAL(harness.waitingQueue().size(), 4u);
    expectCheckpointAction(harness.backSlot(), kCid, Action::FULL);
    expectCheckpointPayload(harness.backSlot(), kCid, payload);
}

void testFullThenCarryPipeline()
{
    const std::vector<char> payload{'A', 'B', 'C'};

    TestHarness harness;
    harness.setTime(100);
    harness.stage(payload);
    harness.flush();

    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::FULL);
    expectPipelinePayload(entry, payload);

    harness.setTime(101);
    harness.stage(payload);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::CARRY);
}

void testOnCollectionHeartbeatWaitingQueue()
{
    const std::vector<char> payload{'G', 'H', 'I'};

    TestHarness harness;
    harness.setTime(100);
    harness.stage(payload);
    expectCheckpointAction(harness.backSlot(), kCid, Action::FULL);

    harness.setTime(101);
    harness.stage(payload);
    expectCheckpointAction(harness.backSlot(), kCid, Action::CARRY);

    harness.setTime(102);
    harness.stage(payload);
    expectCheckpointAction(harness.backSlot(), kCid, Action::CARRY);

    harness.setTime(103);
    harness.stage(payload);
    expectCheckpointAction(harness.backSlot(), kCid, Action::FULL);
    expectCheckpointPayload(harness.backSlot(), kCid, payload);
}

void testOnCollectionHeartbeatPipeline()
{
    const std::vector<char> payload{'G', 'H', 'I'};

    TestHarness harness;
    harness.setTime(100);
    harness.stage(payload);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::FULL);

    harness.setTime(101);
    harness.stage(payload);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::CARRY);

    harness.setTime(102);
    harness.stage(payload);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::CARRY);

    harness.setTime(103);
    harness.stage(payload);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::FULL);
    expectPipelinePayload(entry, payload);
}

void testDisableAndReenableWaitingQueue()
{
    const std::vector<char> payload{'G', 'H', 'I'};

    TestHarness harness;
    harness.setTime(100);
    harness.stage(payload);

    harness.setTime(101);
    harness.disable();
    expectCheckpointAction(harness.backSlot(), kCid, Action::DISABLED);

    harness.setTime(102);
    harness.enable();
    expectCheckpointAction(harness.backSlot(), kCid, Action::FULL);
    expectCheckpointPayload(harness.backSlot(), kCid, payload);
}

void testDisableAndReenablePipeline()
{
    const std::vector<char> payload{'G', 'H', 'I'};

    TestHarness harness;
    harness.setTime(100);
    harness.stage(payload);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));

    harness.setTime(101);
    harness.disable();
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::DISABLED);

    harness.setTime(102);
    harness.enable();
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::FULL);
    expectPipelinePayload(entry, payload);
}

void testLifecycleDoesNotMutateEarlierSlots()
{
    const std::vector<char> payload{'A', 'B', 'C'};

    TestHarness harness;
    harness.setTime(100);
    harness.stage(payload);

    harness.setTime(101);
    harness.disable();

    EXPECT_EQUAL(harness.waitingQueue().size(), 2u);
    expectCheckpointAction(harness.waitingQueue()[0], kCid, Action::FULL);
    expectCheckpointAction(harness.waitingQueue()[1], kCid, Action::DISABLED);
}

void testEmptyTimeSlot()
{
    TestHarness harness;
    harness.setTime(100);
    harness.stage({'X'});
    EXPECT_EQUAL(harness.backSlot().checkpoints.size(), 1u);

    harness.setTime(101);
    harness.advanceSlot();
    EXPECT_EQUAL(harness.waitingQueue().size(), 2u);
    EXPECT_TRUE(harness.backSlot().checkpoints.empty());
}

void testMissingCidHeartbeatInject()
{
    const std::vector<char> payload{'X'};

    TestHarness harness;
    harness.setTime(100);
    harness.stage(payload);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::FULL);

    harness.setTime(101);
    harness.advanceSlot();
    harness.flush();
    EXPECT_FALSE(harness.pop(entry));

    harness.setTime(102);
    harness.advanceSlot();
    harness.flush();
    EXPECT_FALSE(harness.pop(entry));

    harness.setTime(103);
    harness.advanceSlot();
    harness.flush();
    EXPECT_FALSE(harness.pop(entry));
}

} // namespace

TEST_INIT;

int main()
{
    testFullThenCarryWaitingQueue();
    testFullThenCarryPipeline();
    testOnCollectionHeartbeatWaitingQueue();
    testOnCollectionHeartbeatPipeline();
    testDisableAndReenableWaitingQueue();
    testDisableAndReenablePipeline();
    testLifecycleDoesNotMutateEarlierSlots();
    testEmptyTimeSlot();
    testMissingCidHeartbeatInject();

    REPORT_ERROR;
    return ERROR_CODE;
}
