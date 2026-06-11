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

class TestHarness
{
public:
    TestHarness() :
        stager_(kHeartbeat, &timestamp_, &pipeline_queue_)
    {
        stager_.disableAutoSendMode(true);
        stager_.setScalarType(kCid);
    }

    void setTime(uint64_t time)
    {
        sim_time_ = time;
    }

    void stage(const std::vector<char>& payload) { stager_.stage(kCid, payload); }

    void disable() { stager_.onEnabledChanged(kCid, false); }

    void enable() { stager_.onEnabledChanged(kCid, true); }

    void flush()
    {
        stager_.sendCollectedDataToPipeline();
    }

    void advanceSlot() { stager_.advanceSimTimeSlot(); }

    bool pop(QueueCollectionData& entry) { return pipeline_queue_.try_pop(entry); }

    CheckpointPipelineStager& stager() { return stager_; }

private:
    uint64_t sim_time_ = 0;
    simdb::argos::Timestamp timestamp_{&sim_time_};
    simdb::ConcurrentQueue<QueueCollectionData> pipeline_queue_;
    CheckpointPipelineStager stager_;
};

void expectSingleAction(const QueueCollectionData& entry, Action action)
{
    EXPECT_EQUAL(entry.collection_data.size(), 1u);
    EXPECT_EQUAL(getActionByte(*entry.collection_data[0]), static_cast<uint8_t>(action));
}

void expectPayload(const QueueCollectionData& entry, const std::vector<char>& payload)
{
    EXPECT_EQUAL(getPayload(*entry.collection_data[0]), payload);
}

void testFullThenCarry()
{
    const std::vector<char> payload{'A', 'B', 'C'};

    TestHarness harness;
    harness.setTime(100);
    harness.stage(payload);
    harness.flush();

    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    expectSingleAction(entry, Action::FULL);
    expectPayload(entry, payload);

    harness.setTime(101);
    harness.stage(payload);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSingleAction(entry, Action::CARRY);
}

void testOnCollectionHeartbeat()
{
    const std::vector<char> payload{'G', 'H', 'I'};

    TestHarness harness;
    harness.setTime(100);
    harness.stage(payload);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    expectSingleAction(entry, Action::FULL);

    harness.setTime(101);
    harness.stage(payload);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSingleAction(entry, Action::CARRY);

    harness.setTime(102);
    harness.stage(payload);
    harness.flush();
    EXPECT_FALSE(harness.pop(entry));

    harness.setTime(103);
    harness.stage(payload);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSingleAction(entry, Action::FULL);
}

void testDisableAndReenable()
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
    expectSingleAction(entry, Action::DISABLED);

    harness.setTime(102);
    harness.enable();
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSingleAction(entry, Action::FULL);
    expectPayload(entry, payload);
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
    expectSingleAction(entry, Action::FULL);

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
    EXPECT_TRUE(harness.pop(entry));
    expectSingleAction(entry, Action::FULL);
    expectPayload(entry, payload);
}

} // namespace

TEST_INIT;

int main()
{
    testFullThenCarry();
    testOnCollectionHeartbeat();
    testDisableAndReenable();
    testMissingCidHeartbeatInject();

    REPORT_ERROR;
    return ERROR_CODE;
}
