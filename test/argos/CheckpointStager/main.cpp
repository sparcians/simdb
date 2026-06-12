// clang-format off

#include "SimDBTester.hpp"
#include "simdb/apps/argos/CheckpointPipelineStager.hpp"
#include "simdb/utils/ConcurrentQueue.hpp"

#include <cstring>
#include <map>

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

    void quiet() { stager_.onQuietChanged(kCid, true); }

    void awaken() { stager_.onQuietChanged(kCid, false); }

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

    std::shared_ptr<const simdb::argos::Checkpoint> getTip() const { return stager_.getTip(kCid); }

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
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::FULL);
    expectPipelinePayload(entry, payload);
}

void testTimeAdvancesWhileVanished()
{
    const std::vector<char> payload{'D', 'I', 'S'};

    TestHarness harness;
    harness.setTime(100);
    harness.stage(payload);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::FULL);

    harness.setTime(101);
    harness.disable();
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::DISABLED);
    EXPECT_TRUE(harness.getTip() != nullptr);
    EXPECT_EQUAL(static_cast<uint8_t>(harness.getTip()->getAction()), static_cast<uint8_t>(Action::DISABLED));

    for (uint64_t time : {102u, 103u, 104u})
    {
        harness.setTime(time);
        harness.advanceSlot();
        harness.flush();
        EXPECT_FALSE(harness.pop(entry));
    }
}

void testQuietAndAwakenPipeline()
{
    const std::vector<char> payload{'Q', 'U', 'I'};

    TestHarness harness;
    harness.setTime(100);
    harness.stage(payload);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::FULL);

    harness.setTime(101);
    harness.quiet();
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::QUIETED);

    harness.setTime(102);
    harness.awaken();
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::FULL);
    expectPipelinePayload(entry, payload);
}

void testChangedPayloadAtHeartbeatPipeline()
{
    const std::vector<char> payload_a{'A', 'A', 'A'};
    const std::vector<char> payload_b{'B', 'B', 'B'};

    TestHarness harness;
    harness.setTime(100);
    harness.stage(payload_a);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::FULL);

    harness.setTime(101);
    harness.stage(payload_a);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::CARRY);

    harness.setTime(102);
    harness.stage(payload_a);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::CARRY);

    harness.setTime(103);
    harness.stage(payload_b);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSinglePipelineAction(entry, Action::FULL);
    expectPipelinePayload(entry, payload_b);
}

void testMultiCidSameFlush()
{
    constexpr uint16_t kCid2 = 2;
    const std::vector<char> payload1{'O', 'N', 'E'};
    const std::vector<char> payload2{'T', 'W', 'O'};

    uint64_t sim_time = 100;
    simdb::argos::Timestamp timestamp{&sim_time};
    simdb::ConcurrentQueue<QueueCollectionData> pipeline_queue;
    CheckpointPipelineStager stager(kHeartbeat, &timestamp, &pipeline_queue);
    stager.disableAutoSendMode(true);
    stager.setScalarType(kCid);
    stager.setScalarType(kCid2);

    stager.stage(kCid, payload1);
    stager.stage(kCid2, payload2);
    stager.sendCollectedDataToPipeline();

    QueueCollectionData entry;
    EXPECT_TRUE(pipeline_queue.try_pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);
}

std::vector<std::vector<char>> contigBins(std::initializer_list<std::vector<char>> elems)
{
    return std::vector<std::vector<char>>(elems);
}

std::vector<char> instBytes(char tag)
{
    return {'I', 'n', 's', 't', tag};
}

uint16_t readUint16(const std::vector<char>& bytes, size_t offset)
{
    uint16_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(uint16_t));
    return value;
}

uint16_t getEntryCid(const simdb::argos::CollectedData& data)
{
    const auto& bytes = data.getData();
    EXPECT_TRUE(bytes.size() >= kCidBytes);
    uint16_t cid = 0;
    std::memcpy(&cid, bytes.data(), sizeof(cid));
    return cid;
}

const simdb::argos::CollectedData* findPipelineEntry(const QueueCollectionData& entry, uint16_t cid)
{
    for (const auto& pipeline_entry : entry.entries)
    {
        if (getEntryCid(*pipeline_entry) == cid)
        {
            return pipeline_entry.get();
        }
    }
    return nullptr;
}

class ContigTestHarness
{
public:
    ContigTestHarness() :
        stager_(kHeartbeat, &timestamp_, &pipeline_queue_)
    {
        stager_.disableAutoSendMode(true);
        stager_.setContainerType(kContigCid, false, 64);
    }

    void setTime(uint64_t time) { sim_time_ = time; }

    void stage(const std::vector<std::vector<char>>& bins) { stager_.stage(kContigCid, bins); }

    void disable() { stager_.onEnabledChanged(kContigCid, false); }

    void enable() { stager_.onEnabledChanged(kContigCid, true); }

    void quiet() { stager_.onQuietChanged(kContigCid, true); }

    void awaken() { stager_.onQuietChanged(kContigCid, false); }

    void flush() { stager_.sendCollectedDataToPipeline(); }

    bool pop(QueueCollectionData& entry)
    {
        if (!pipeline_queue_.try_pop(entry))
        {
            return false;
        }
        return !entry.entries.empty();
    }

private:
    static constexpr uint16_t kContigCid = 7;

    uint64_t sim_time_ = 0;
    simdb::argos::Timestamp timestamp_{&sim_time_};
    simdb::ConcurrentQueue<QueueCollectionData> pipeline_queue_;
    CheckpointPipelineStager stager_;
};

void testNotesContigEndToEnd()
{
    ContigTestHarness harness;
    std::vector<std::vector<char>> initial;
    for (char tag = '0'; tag <= '9'; ++tag)
    {
        initial.push_back(instBytes(tag));
    }
    initial.push_back(instBytes('A'));
    initial.push_back(instBytes('B'));

    harness.setTime(100);
    harness.stage(initial);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    const auto* full_entry = findPipelineEntry(entry, 7);
    EXPECT_TRUE(full_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*full_entry), static_cast<uint8_t>(Action::FULL));
    const auto full_payload = getPayload(*full_entry);
    EXPECT_EQUAL(readUint16(full_payload, 0), 12u);

    auto with_arrival = initial;
    with_arrival.push_back(instBytes('C'));
    harness.setTime(101);
    harness.stage(with_arrival);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    const auto* arrive_entry = findPipelineEntry(entry, 7);
    EXPECT_TRUE(arrive_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*arrive_entry), static_cast<uint8_t>(Action::CONTIG_CONTAINER_ARRIVE));
    EXPECT_EQUAL(getPayload(*arrive_entry), instBytes('C'));
}

void testContigHeartbeatBookendsOverride()
{
    ContigTestHarness harness;
    const auto initial = contigBins({instBytes('A'), instBytes('B'), instBytes('C'), instBytes('D')});
    const auto shifted = contigBins({instBytes('B'), instBytes('C'), instBytes('D'), instBytes('E')});

    harness.setTime(100);
    harness.stage(initial);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));

    harness.setTime(101);
    harness.stage(initial);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));

    harness.setTime(102);
    harness.stage(initial);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));

    harness.setTime(103);
    harness.stage(shifted);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    const auto* pipeline_entry = findPipelineEntry(entry, 7);
    EXPECT_TRUE(pipeline_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*pipeline_entry), static_cast<uint8_t>(Action::FULL));
}

void testContigDisableQuietPipeline()
{
    ContigTestHarness harness;
    const auto initial = contigBins({instBytes('Q'), instBytes('U'), instBytes('I')});

    harness.setTime(100);
    harness.stage(initial);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*findPipelineEntry(entry, 7)), static_cast<uint8_t>(Action::FULL));

    harness.setTime(101);
    harness.quiet();
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*findPipelineEntry(entry, 7)), static_cast<uint8_t>(Action::QUIETED));

    harness.setTime(102);
    harness.awaken();
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*findPipelineEntry(entry, 7)), static_cast<uint8_t>(Action::FULL));
}

void testScalarAndContigSameFlush()
{
    constexpr uint16_t kScalarCid = 1;
    constexpr uint16_t kContigCid = 7;
    const std::vector<char> scalar_payload{'S', 'C', 'A'};
    const auto contig_payload = contigBins({instBytes('C'), instBytes('T')});

    uint64_t sim_time = 100;
    simdb::argos::Timestamp timestamp{&sim_time};
    simdb::ConcurrentQueue<QueueCollectionData> pipeline_queue;
    CheckpointPipelineStager stager(kHeartbeat, &timestamp, &pipeline_queue);
    stager.disableAutoSendMode(true);
    stager.setScalarType(kScalarCid);
    stager.setContainerType(kContigCid, false, 16);

    stager.stage(kScalarCid, scalar_payload);
    stager.stage(kContigCid, contig_payload);
    stager.sendCollectedDataToPipeline();

    QueueCollectionData entry;
    EXPECT_TRUE(pipeline_queue.try_pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);
    EXPECT_TRUE(findPipelineEntry(entry, kScalarCid) != nullptr);
    EXPECT_TRUE(findPipelineEntry(entry, kContigCid) != nullptr);
}

std::map<uint16_t, std::vector<char>> sparseBins(
    std::initializer_list<std::pair<const uint16_t, std::vector<char>>> elems)
{
    return std::map<uint16_t, std::vector<char>>(elems);
}

std::vector<char> sparseBinBytes(char tag)
{
    return {'B', 'i', 'n', tag};
}

class SparseTestHarness
{
public:
    SparseTestHarness() :
        stager_(kHeartbeat, &timestamp_, &pipeline_queue_)
    {
        stager_.disableAutoSendMode(true);
        stager_.setContainerType(kSparseCid, true, 64);
    }

    void setTime(uint64_t time) { sim_time_ = time; }

    void stage(const std::map<uint16_t, std::vector<char>>& bins) { stager_.stage(kSparseCid, bins); }

    void quiet() { stager_.onQuietChanged(kSparseCid, true); }

    void awaken() { stager_.onQuietChanged(kSparseCid, false); }

    void flush() { stager_.sendCollectedDataToPipeline(); }

    bool pop(QueueCollectionData& entry)
    {
        if (!pipeline_queue_.try_pop(entry))
        {
            return false;
        }
        return !entry.entries.empty();
    }

private:
    static constexpr uint16_t kSparseCid = 9;

    uint64_t sim_time_ = 0;
    simdb::argos::Timestamp timestamp_{&sim_time_};
    simdb::ConcurrentQueue<QueueCollectionData> pipeline_queue_;
    CheckpointPipelineStager stager_;
};

void testSparseFirstCollectionFull()
{
    SparseTestHarness harness;
    const auto initial = sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('B')}});

    harness.setTime(100);
    harness.stage(initial);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    const auto* full_entry = findPipelineEntry(entry, 9);
    EXPECT_TRUE(full_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*full_entry), static_cast<uint8_t>(Action::FULL));
    const auto payload = getPayload(*full_entry);
    EXPECT_EQUAL(readUint16(payload, 0), 2u);
    EXPECT_EQUAL(readUint16(payload, sizeof(uint16_t)), 1u);
}

void testSparseSwapPipeline()
{
    SparseTestHarness harness;
    const auto initial = sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('B')}});
    const auto swapped = sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('X')}});

    harness.setTime(100);
    harness.stage(initial);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));

    harness.setTime(101);
    harness.stage(swapped);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    const auto* swap_entry = findPipelineEntry(entry, 9);
    EXPECT_TRUE(swap_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*swap_entry), static_cast<uint8_t>(Action::SPARSE_CONTAINER_SWAP));
    const auto payload = getPayload(*swap_entry);
    EXPECT_EQUAL(readUint16(payload, 0), 5u);
    EXPECT_EQUAL(std::vector<char>(payload.begin() + sizeof(uint16_t), payload.end()), sparseBinBytes('X'));
}

void testSparseRemovePipeline()
{
    SparseTestHarness harness;
    const auto initial = sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('B')}, {9, sparseBinBytes('C')}});
    const auto removed = sparseBins({{1, sparseBinBytes('A')}, {9, sparseBinBytes('C')}});

    harness.setTime(100);
    harness.stage(initial);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));

    harness.setTime(101);
    harness.stage(removed);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    const auto* remove_entry = findPipelineEntry(entry, 9);
    EXPECT_TRUE(remove_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*remove_entry), static_cast<uint8_t>(Action::SPARSE_CONTAINER_REMOVE));
    EXPECT_EQUAL(readUint16(getPayload(*remove_entry), 0), 5u);
}

void testSparseHeartbeatCarryToFull()
{
    SparseTestHarness harness;
    const auto initial = sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('B')}});
    const auto swapped = sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('X')}});

    harness.setTime(100);
    harness.stage(initial);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));

    harness.setTime(101);
    harness.stage(initial);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*findPipelineEntry(entry, 9)), static_cast<uint8_t>(Action::CARRY));

    harness.setTime(102);
    harness.stage(initial);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*findPipelineEntry(entry, 9)), static_cast<uint8_t>(Action::CARRY));

    harness.setTime(103);
    harness.stage(swapped);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*findPipelineEntry(entry, 9)), static_cast<uint8_t>(Action::FULL));
}

void testSparseQuietAwakenPipeline()
{
    SparseTestHarness harness;
    const auto initial = sparseBins({{2, sparseBinBytes('Q')}, {4, sparseBinBytes('U')}});

    harness.setTime(100);
    harness.stage(initial);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));

    harness.setTime(101);
    harness.quiet();
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*findPipelineEntry(entry, 9)), static_cast<uint8_t>(Action::QUIETED));

    harness.setTime(102);
    harness.awaken();
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*findPipelineEntry(entry, 9)), static_cast<uint8_t>(Action::FULL));
}

void testScalarContigSparseSameFlush()
{
    constexpr uint16_t kScalarCid = 1;
    constexpr uint16_t kContigCid = 7;
    constexpr uint16_t kSparseCid = 9;
    const std::vector<char> scalar_payload{'S', 'C', 'A'};
    const auto contig_payload = contigBins({instBytes('C'), instBytes('T')});
    const auto sparse_payload = sparseBins({{3, sparseBinBytes('S')}, {7, sparseBinBytes('P')}});

    uint64_t sim_time = 100;
    simdb::argos::Timestamp timestamp{&sim_time};
    simdb::ConcurrentQueue<QueueCollectionData> pipeline_queue;
    CheckpointPipelineStager stager(kHeartbeat, &timestamp, &pipeline_queue);
    stager.disableAutoSendMode(true);
    stager.setScalarType(kScalarCid);
    stager.setContainerType(kContigCid, false, 16);
    stager.setContainerType(kSparseCid, true, 16);

    stager.stage(kScalarCid, scalar_payload);
    stager.stage(kContigCid, contig_payload);
    stager.stage(kSparseCid, sparse_payload);
    stager.sendCollectedDataToPipeline();

    QueueCollectionData entry;
    EXPECT_TRUE(pipeline_queue.try_pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 3u);
    EXPECT_TRUE(findPipelineEntry(entry, kScalarCid) != nullptr);
    EXPECT_TRUE(findPipelineEntry(entry, kContigCid) != nullptr);
    EXPECT_TRUE(findPipelineEntry(entry, kSparseCid) != nullptr);
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
    testTimeAdvancesWhileVanished();
    testQuietAndAwakenPipeline();
    testChangedPayloadAtHeartbeatPipeline();
    testMultiCidSameFlush();
    testNotesContigEndToEnd();
    testContigHeartbeatBookendsOverride();
    testContigDisableQuietPipeline();
    testScalarAndContigSameFlush();
    testSparseFirstCollectionFull();
    testSparseSwapPipeline();
    testSparseRemovePipeline();
    testSparseHeartbeatCarryToFull();
    testSparseQuietAwakenPipeline();
    testScalarContigSparseSameFlush();

    REPORT_ERROR;
    return ERROR_CODE;
}
