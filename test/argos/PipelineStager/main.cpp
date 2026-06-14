// clang-format off

#include "SimDBTester.hpp"
#include "simdb/apps/argos/PipelineStager.hpp"

#include <cstring>

namespace {

using simdb::argos::Action;
using simdb::argos::PipelineStager;
using simdb::argos::QueueCollectionData;

constexpr auto kCidBytes = sizeof(uint16_t);
constexpr auto kActionBytes = sizeof(uint8_t);
constexpr uint16_t kScalarCid = 1;
constexpr uint16_t kContigCid = 7;
constexpr uint16_t kSparseCid = 9;
constexpr size_t kHeartbeat = 3;
constexpr size_t kLargeHeartbeat = 100;

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

uint16_t readUint16(const std::vector<char>& bytes, size_t offset)
{
    EXPECT_TRUE(bytes.size() >= offset + sizeof(uint16_t));
    uint16_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(uint16_t));
    return value;
}

const simdb::argos::CollectedData* findPipelineEntry(const QueueCollectionData& entry, uint16_t cid)
{
    for (const auto& collected : entry.entries)
    {
        if (collected->getCID() == cid)
        {
            return collected.get();
        }
    }
    return nullptr;
}

std::vector<std::vector<char>> contigBins(std::initializer_list<std::vector<char>> elems)
{
    return std::vector<std::vector<char>>(elems);
}

std::vector<char> instBytes(char tag)
{
    return {'I', 'n', 's', 't', tag};
}

constexpr uint16_t kFastScalarCid = 10;
constexpr uint16_t kSlowScalarCid = 20;
constexpr uint16_t kSlowContigCid = 27;
constexpr uint16_t kSlowSparseCid = 37;

const std::vector<char> kFastPayload{'F'};
const std::vector<char> kSlowPayload{'S'};
const std::vector<char> kSlowChangedPayload{'X'};

void expectAbsent(const QueueCollectionData& entry, uint16_t cid)
{
    EXPECT_TRUE(findPipelineEntry(entry, cid) == nullptr);
}

void expectAction(const QueueCollectionData& entry, uint16_t cid, Action action)
{
    const auto* pipeline_entry = findPipelineEntry(entry, cid);
    EXPECT_TRUE(pipeline_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*pipeline_entry), static_cast<uint8_t>(action));
}

void expectPayload(const QueueCollectionData& entry, uint16_t cid, const std::vector<char>& payload)
{
    const auto* pipeline_entry = findPipelineEntry(entry, cid);
    EXPECT_TRUE(pipeline_entry != nullptr);
    EXPECT_EQUAL(getPayload(*pipeline_entry), payload);
}

void expectFlush(const QueueCollectionData& entry, size_t expected_count, Action fast_action,
                 bool slow_present, Action slow_action = Action::FULL)
{
    EXPECT_EQUAL(entry.entries.size(), expected_count);
    expectAction(entry, kFastScalarCid, fast_action);
    if (slow_present)
    {
        expectAction(entry, kSlowScalarCid, slow_action);
    } else
    {
        expectAbsent(entry, kSlowScalarCid);
    }
}

void expectContigAction(const QueueCollectionData& entry, uint16_t cid, Action action)
{
    expectAction(entry, cid, action);
}

void expectContigSwap(const QueueCollectionData& entry, uint16_t cid, uint16_t swap_index,
                      const std::vector<char>& bin)
{
    const auto* pipeline_entry = findPipelineEntry(entry, cid);
    EXPECT_TRUE(pipeline_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*pipeline_entry), static_cast<uint8_t>(Action::CONTIG_CONTAINER_SWAP));
    const auto payload = getPayload(*pipeline_entry);
    EXPECT_EQUAL(readUint16(payload, 0), swap_index);
    EXPECT_EQUAL(std::vector<char>(payload.begin() + sizeof(uint16_t), payload.end()), bin);
}

void expectContigFullBinCount(const QueueCollectionData& entry, uint16_t cid, uint16_t bin_count)
{
    const auto* pipeline_entry = findPipelineEntry(entry, cid);
    EXPECT_TRUE(pipeline_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*pipeline_entry), static_cast<uint8_t>(Action::FULL));
    EXPECT_EQUAL(readUint16(getPayload(*pipeline_entry), 0), bin_count);
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

void expectSparseAction(const QueueCollectionData& entry, uint16_t cid, Action action)
{
    expectAction(entry, cid, action);
}

void expectSparseSwap(const QueueCollectionData& entry, uint16_t cid, uint16_t bin_index,
                      const std::vector<char>& bin)
{
    const auto* pipeline_entry = findPipelineEntry(entry, cid);
    EXPECT_TRUE(pipeline_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*pipeline_entry), static_cast<uint8_t>(Action::SPARSE_CONTAINER_SWAP));
    const auto payload = getPayload(*pipeline_entry);
    EXPECT_EQUAL(readUint16(payload, 0), bin_index);
    EXPECT_EQUAL(std::vector<char>(payload.begin() + sizeof(uint16_t), payload.end()), bin);
}

void expectSparseRemove(const QueueCollectionData& entry, uint16_t cid, uint16_t bin_index)
{
    const auto* pipeline_entry = findPipelineEntry(entry, cid);
    EXPECT_TRUE(pipeline_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*pipeline_entry), static_cast<uint8_t>(Action::SPARSE_CONTAINER_REMOVE));
    EXPECT_EQUAL(readUint16(getPayload(*pipeline_entry), 0), bin_index);
}

void expectSparseFullBinCount(const QueueCollectionData& entry, uint16_t cid, uint16_t bin_count)
{
    const auto* pipeline_entry = findPipelineEntry(entry, cid);
    EXPECT_TRUE(pipeline_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*pipeline_entry), static_cast<uint8_t>(Action::FULL));
    EXPECT_EQUAL(readUint16(getPayload(*pipeline_entry), 0), bin_count);
}

class FastScalarSlowContigHarness
{
public:
    explicit FastScalarSlowContigHarness(size_t contig_heartbeat = kLargeHeartbeat) :
        stager_(contig_heartbeat, &timestamp_, &pipeline_queue_)
    {
        stager_.disableAutoSendMode(true);
        stager_.setScalarType(kFastScalarCid);
        stager_.setContainerType(kSlowContigCid, false, 64);
    }

    void setTime(uint64_t time)
    {
        sim_time_ = time;
        stager_.advanceSimTimeSlot();
    }

    void stageFast(const std::vector<char>& payload = kFastPayload)
    {
        stager_.stage(kFastScalarCid, payload);
    }

    void stageContig(const std::vector<std::vector<char>>& bins) { stager_.stage(kSlowContigCid, bins); }

    void disableContig() { stager_.onEnabledChanged(kSlowContigCid, false); }

    void enableContig() { stager_.onEnabledChanged(kSlowContigCid, true); }

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
    uint64_t sim_time_ = 0;
    simdb::argos::Timestamp timestamp_{&sim_time_};
    simdb::ConcurrentQueue<QueueCollectionData> pipeline_queue_;
    PipelineStager stager_;
};

class DualScalarHarness
{
public:
    DualScalarHarness() :
        stager_(kHeartbeat, &timestamp_, &pipeline_queue_)
    {
        stager_.disableAutoSendMode(true);
        stager_.setScalarType(kFastScalarCid);
        stager_.setScalarType(kSlowScalarCid);
        stager_.setCollectableClock(kFastScalarCid, 1);
        stager_.setCollectableClock(kSlowScalarCid, 2);
    }

    void setTime(uint64_t time)
    {
        sim_time_ = time;
        stager_.advanceSimTimeSlot();
    }

    void stageFast(const std::vector<char>& payload = kFastPayload)
    {
        stager_.stage(kFastScalarCid, payload);
    }

    void stageSlow(const std::vector<char>& payload = kSlowPayload)
    {
        stager_.stage(kSlowScalarCid, payload);
    }

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
    uint64_t sim_time_ = 0;
    simdb::argos::Timestamp timestamp_{&sim_time_};
    simdb::ConcurrentQueue<QueueCollectionData> pipeline_queue_;
    PipelineStager stager_;
};

class ScalarTestHarness
{
public:
    ScalarTestHarness() :
        stager_(kHeartbeat, &timestamp_, &pipeline_queue_)
    {
        stager_.disableAutoSendMode(true);
        stager_.setScalarType(kScalarCid);
    }

    void setTime(uint64_t time)
    {
        sim_time_ = time;
        stager_.advanceSimTimeSlot();
    }

    void stage(const std::vector<char>& payload) { stager_.stage(kScalarCid, payload); }

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
    uint64_t sim_time_ = 0;
    simdb::argos::Timestamp timestamp_{&sim_time_};
    simdb::ConcurrentQueue<QueueCollectionData> pipeline_queue_;
    PipelineStager stager_;
};

class ContigTestHarness
{
public:
    ContigTestHarness(size_t heartbeat = kLargeHeartbeat) :
        stager_(heartbeat, &timestamp_, &pipeline_queue_)
    {
        stager_.disableAutoSendMode(true);
        stager_.setContainerType(kContigCid, false, 64);
    }

    void setTime(uint64_t time)
    {
        sim_time_ = time;
        stager_.advanceSimTimeSlot();
    }

    void stage(const std::vector<std::vector<char>>& bins) { stager_.stage(kContigCid, bins); }

    void disable() { stager_.onEnabledChanged(kContigCid, false); }

    void enable() { stager_.onEnabledChanged(kContigCid, true); }

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
    uint64_t sim_time_ = 0;
    simdb::argos::Timestamp timestamp_{&sim_time_};
    simdb::ConcurrentQueue<QueueCollectionData> pipeline_queue_;
    PipelineStager stager_;
};

class SparseTestHarness
{
public:
    explicit SparseTestHarness(size_t heartbeat = kLargeHeartbeat) :
        stager_(heartbeat, &timestamp_, &pipeline_queue_)
    {
        stager_.disableAutoSendMode(true);
        stager_.setContainerType(kSparseCid, true, 64);
    }

    void setTime(uint64_t time)
    {
        sim_time_ = time;
        stager_.advanceSimTimeSlot();
    }

    void stage(const std::map<uint16_t, std::vector<char>>& bins) { stager_.stage(kSparseCid, bins); }

    void disable() { stager_.onEnabledChanged(kSparseCid, false); }

    void enable() { stager_.onEnabledChanged(kSparseCid, true); }

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
    uint64_t sim_time_ = 0;
    simdb::argos::Timestamp timestamp_{&sim_time_};
    simdb::ConcurrentQueue<QueueCollectionData> pipeline_queue_;
    PipelineStager stager_;
};

class FastScalarSlowSparseHarness
{
public:
    explicit FastScalarSlowSparseHarness(size_t sparse_heartbeat = kLargeHeartbeat) :
        stager_(sparse_heartbeat, &timestamp_, &pipeline_queue_)
    {
        stager_.disableAutoSendMode(true);
        stager_.setScalarType(kFastScalarCid);
        stager_.setContainerType(kSlowSparseCid, true, 64);
    }

    void setTime(uint64_t time)
    {
        sim_time_ = time;
        stager_.advanceSimTimeSlot();
    }

    void stageFast(const std::vector<char>& payload = kFastPayload)
    {
        stager_.stage(kFastScalarCid, payload);
    }

    void stageSparse(const std::map<uint16_t, std::vector<char>>& bins) { stager_.stage(kSlowSparseCid, bins); }

    void disableSparse() { stager_.onEnabledChanged(kSlowSparseCid, false); }

    void enableSparse() { stager_.onEnabledChanged(kSlowSparseCid, true); }

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
    uint64_t sim_time_ = 0;
    simdb::argos::Timestamp timestamp_{&sim_time_};
    simdb::ConcurrentQueue<QueueCollectionData> pipeline_queue_;
    PipelineStager stager_;
};

// Scalar smoke tests (heartbeat=3, CID=1).
// Harness pattern: stage at Time, then setTime(Time+1) to close that window and flush.
/*
 * testScalarFullThenCarryPipeline
 *
 * Collects: Scalar at W1 and W2 with unchanged payload ABC.
 * Covers:   First collection emits FULL; unchanged re-stage emits CARRY.
 *
 * Payload key: ABC={'A','B','C'}
 *
 * Time  WindowID  Scalar(collect)  WindowSent
 * ----  --------  ---------------  -----------------
 *  100         1              ABC  Scalar(FULL)
 *  101         2              ABC  Scalar(CARRY)
 *
 * TODO cnyce: We are emitting CARRY inside a heartbeat interval
 * that is already anchored by FULL at T=100. That's a waste of
 * disk space, but the redesign logic is not worth the effort.
 * We can revisit this later to keep making the DB smaller.
 */
void testScalarFullThenCarryPipeline()
{
    const std::vector<char> payload{'A', 'B', 'C'};

    ScalarTestHarness harness;
    harness.setTime(100);
    harness.stage(payload);
    harness.setTime(101);
    harness.flush();

    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*entry.entries[0]), static_cast<uint8_t>(Action::FULL));
    EXPECT_EQUAL(getPayload(*entry.entries[0]), payload);

    harness.stage(payload);
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*entry.entries[0]), static_cast<uint8_t>(Action::CARRY));
}

/*
 * testScalarHeartbeatPipeline
 *
 * Collects: Scalar every window W1-W4 with unchanged payload GHI.
 * Covers:   wire_distance_ heartbeat: FULL, CARRY, CARRY, FULL (heartbeat=3).
 *
 * Payload key: GHI={'G','H','I'}
 *
 * Time  WindowID  Scalar(collect)  WindowSent
 * ----  --------  ---------------  -----------------
 *  100         1              GHI  Scalar(FULL)
 *  101         2              GHI  Scalar(CARRY)
 *  102         3              GHI  Scalar(CARRY)
 *  103         4              GHI  Scalar(FULL)
 *
 * TODO cnyce: We are emitting CARRY inside a heartbeat interval
 * that is already anchored by FULL at T=100. That's a waste of
 * disk space, but the redesign logic is not worth the effort.
 * We can revisit this later to keep making the DB smaller.
 */
void testScalarHeartbeatPipeline()
{
    const std::vector<char> payload{'G', 'H', 'I'};

    ScalarTestHarness harness;
    harness.setTime(100);
    harness.stage(payload);
    harness.setTime(101);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*entry.entries[0]), static_cast<uint8_t>(Action::FULL));

    harness.stage(payload);
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*entry.entries[0]), static_cast<uint8_t>(Action::CARRY));

    harness.stage(payload);
    harness.setTime(103);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*entry.entries[0]), static_cast<uint8_t>(Action::CARRY));

    harness.stage(payload);
    harness.setTime(104);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*entry.entries[0]), static_cast<uint8_t>(Action::FULL));
    EXPECT_EQUAL(getPayload(*entry.entries[0]), payload);
}

// Dual-scalar tests (heartbeat=3, CIDs 10=fast / 20=slow).
// Harness pattern: stage at Time, then setTime(Time+1) to close that window and flush.
// Table columns: Time = sim time when collect/stage occurs; WindowID = window sent on flush;
// Fast/Slow(collect) = payload staged that window (- = not staged);
// WindowSent = pipeline actions encoded for that window.
// forceSnapshot_ triggers on wire_distance OR window gap since last_full_wired_window_id_
// so Python can rebuild any CID from [T-heartbeat+1, T] without lookback beyond that range.
// TODO cnyce: Many dual-scalar expectations emit interior CARRY while a prior FULL
// still anchors the heartbeat window. That is redundant for Python rebuild and wastes
// disk space, but omit-on-unchanged encoding is not worth the effort right now.
// We can revisit this later to keep making the DB smaller.
/*
 * testDualScalarSlowSkipsTwoWindows (Tier 1)
 *
 * Collects: Fast every window (payload F); slow at W1 and W4 only (payload S).
 * Covers:   Per-CID wire_distance_ isolation; slow omitted when not staged;
 *           fast heartbeat FULL at W4; slow FULL on re-stage at window-gap
 *           boundary (Python lookback [101,103] requires a FULL anchor).
 *
 * Payload key: F={'F'}  S={'S'}  - = not staged
 *
 * Time  WindowID  Fast(collect)  Slow(collect)  WindowSent
 * ----  --------  -------------  -------------  --------------------------
 *  100         1              F              S  Fast(FULL)  Slow(FULL)
 *  101         2              F              -  Fast(CARRY)
 *  102         3              F              -  Fast(CARRY)
 *  103         4              F              S  Fast(FULL)  Slow(FULL)
 */
void testDualScalarSlowSkipsTwoWindows()
{
    DualScalarHarness harness;
    QueueCollectionData entry;

    harness.setTime(100);
    harness.stageFast();
    harness.stageSlow();
    harness.setTime(101);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 2u, Action::FULL, true, Action::FULL);
    expectPayload(entry, kFastScalarCid, kFastPayload);
    expectPayload(entry, kSlowScalarCid, kSlowPayload);

    harness.stageFast();
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 1u, Action::CARRY, false);

    harness.stageFast();
    harness.setTime(103);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 1u, Action::CARRY, false);

    harness.stageFast();
    harness.stageSlow();
    harness.setTime(104);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 2u, Action::FULL, true, Action::FULL);
    expectPayload(entry, kFastScalarCid, kFastPayload);
    expectPayload(entry, kSlowScalarCid, kSlowPayload);
}

/*
 * testDualScalarMultiClockPeriod5
 *
 * Collects: Fast every window; slow every 5th global tick (multi-clock cadence).
 * Covers:   Sparse blobs (fast-only vs both); clock_ids on wired batches.
 */
void testDualScalarMultiClockPeriod5()
{
    DualScalarHarness harness;
    QueueCollectionData entry;

    harness.setTime(100);
    harness.stageFast();
    harness.stageSlow();
    harness.setTime(101);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 2u, Action::FULL, true, Action::FULL);
    EXPECT_TRUE(entry.clock_ids.count(1) == 1);
    EXPECT_TRUE(entry.clock_ids.count(2) == 1);

    for (uint64_t t = 101; t <= 102; ++t)
    {
        harness.stageFast();
        harness.setTime(t + 1);
        harness.flush();
        EXPECT_TRUE(harness.pop(entry));
        expectFlush(entry, 1u, Action::CARRY, false);
        EXPECT_TRUE(entry.clock_ids.count(1) == 1);
    }

    harness.stageFast();
    harness.setTime(104);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 1u);
    EXPECT_TRUE(entry.clock_ids.count(1) == 1);

    harness.stageFast();
    harness.stageSlow();
    harness.setTime(105);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);
    EXPECT_TRUE(findPipelineEntry(entry, kFastScalarCid) != nullptr);
    EXPECT_TRUE(findPipelineEntry(entry, kSlowScalarCid) != nullptr);
    EXPECT_TRUE(entry.clock_ids.count(1) == 1);
    EXPECT_TRUE(entry.clock_ids.count(2) == 1);
}

/*
 * testDualScalarSlowEveryOtherWindow (Tier 2)
 *
 * Collects: Fast every window (F); slow every other window at W1, W3, W5 (S).
 * Covers:   Sparse slow cadence; slow omitted on fast-only windows (no absent refresh).
 *
 * Time  WindowID  Fast(collect)  Slow(collect)  WindowSent
 * ----  --------  -------------  -------------  --------------------------
 *  100         1              F              S  Fast(FULL)  Slow(FULL)
 *  101         2              F              -  Fast(CARRY)
 *  102         3              F              S  Fast(CARRY) Slow(CARRY)
 *  103         4              F              -  Fast(FULL)
 *  104         5              F              S  Fast(CARRY) Slow(CARRY)
 */
void testDualScalarSlowEveryOtherWindow()
{
    DualScalarHarness harness;
    QueueCollectionData entry;

    harness.setTime(100);
    harness.stageFast();
    harness.stageSlow();
    harness.setTime(101);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 2u, Action::FULL, true, Action::FULL);

    harness.stageFast();
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 1u, Action::CARRY, false);

    harness.stageFast();
    harness.stageSlow();
    harness.setTime(103);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 2u, Action::CARRY, true, Action::CARRY);

    harness.stageFast();
    harness.setTime(104);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 1u, Action::FULL, false);

    harness.stageFast();
    harness.stageSlow();
    harness.setTime(105);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);
    EXPECT_TRUE(findPipelineEntry(entry, kFastScalarCid) != nullptr);
    EXPECT_TRUE(findPipelineEntry(entry, kSlowScalarCid) != nullptr);
}

/*
 * testDualScalarSlowReturnsMidInterval (Tier 2)
 *
 * Collects: Fast every window (F); slow at W1 and W2 (S), absent at W3.
 * Covers:   Slow CARRY coexists with fast CARRY in the same flush at W2.
 *
 * Time  WindowID  Fast(collect)  Slow(collect)  WindowSent
 * ----  --------  -------------  -------------  --------------------------
 *  100         1              F              S  Fast(FULL)   Slow(FULL)
 *  101         2              F              S  Fast(CARRY)  Slow(CARRY)
 *  102         3              F              -  Fast(CARRY)
 */
void testDualScalarSlowReturnsMidInterval()
{
    DualScalarHarness harness;
    QueueCollectionData entry;

    harness.setTime(100);
    harness.stageFast();
    harness.stageSlow();
    harness.setTime(101);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 2u, Action::FULL, true, Action::FULL);

    harness.stageFast();
    harness.stageSlow();
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 2u, Action::CARRY, true, Action::CARRY);

    harness.stageFast();
    harness.setTime(103);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 1u, Action::CARRY, false);
}

/*
 * testDualScalarSlowReturnsOnFastHeartbeatBoundary (Tier 2)
 *
 * Collects: Fast every window (F); slow at W1 and W4 (S).
 * Covers:   Fast heartbeat FULL and slow FULL on re-stage at the same window-gap
 *           boundary (W4). Same timeline as testDualScalarSlowSkipsTwoWindows.
 *
 * Time  WindowID  Fast(collect)  Slow(collect)  WindowSent
 * ----  --------  -------------  -------------  --------------------------
 *  100         1              F              S  Fast(FULL)  Slow(FULL)
 *  101         2              F              -  Fast(CARRY)
 *  102         3              F              -  Fast(CARRY)
 *  103         4              F              S  Fast(FULL)  Slow(FULL)
 */
void testDualScalarSlowReturnsOnFastHeartbeatBoundary()
{
    DualScalarHarness harness;
    QueueCollectionData entry;

    harness.setTime(100);
    harness.stageFast();
    harness.stageSlow();
    harness.setTime(101);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 2u, Action::FULL, true, Action::FULL);

    harness.stageFast();
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 1u, Action::CARRY, false);

    harness.stageFast();
    harness.setTime(103);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 1u, Action::CARRY, false);

    harness.stageFast();
    harness.stageSlow();
    harness.setTime(104);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 2u, Action::FULL, true, Action::FULL);
}

/*
 * testDualScalarSlowChangedAfterGap (Tier 3)
 *
 * Collects: Fast every window (F); slow at W1 (S) then W4 with changed payload X.
 * Covers:   CHANGED classification overrides carry after absent windows W2-W3;
 *           slow emits FULL when re-staging with new bytes.
 *
 * Payload key: X={'X'}
 *
 * Time  WindowID  Fast(collect)  Slow(collect)  WindowSent
 * ----  --------  -------------  -------------  --------------------------
 *  100         1              F              S  Fast(FULL)  Slow(FULL)
 *  101         2              F              -  Fast(CARRY)
 *  102         3              F              -  Fast(CARRY)
 *  103         4              F              X  Fast(FULL)  Slow(FULL)
 */
void testDualScalarSlowChangedAfterGap()
{
    DualScalarHarness harness;
    QueueCollectionData entry;

    harness.setTime(100);
    harness.stageFast();
    harness.stageSlow();
    harness.setTime(101);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));

    harness.stageFast();
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));

    harness.stageFast();
    harness.setTime(103);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));

    harness.stageFast();
    harness.stageSlow(kSlowChangedPayload);
    harness.setTime(104);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 2u, Action::FULL, true, Action::FULL);
    expectPayload(entry, kSlowScalarCid, kSlowChangedPayload);
}

/*
 * testDualScalarSlowChangedMidInterval (Tier 3)
 *
 * Collects: Fast every window (F); slow at W1 (S) then W2 with changed payload NEW.
 * Covers:   Slow FULL from payload change while fast is CARRY in the same flush.
 *
 * Payload key: NEW={'N','E','W'}
 *
 * Time  WindowID  Fast(collect)  Slow(collect)  WindowSent
 * ----  --------  -------------  -------------  --------------------------
 *  100         1              F              S  Fast(FULL)   Slow(FULL)
 *  101         2              F            NEW  Fast(CARRY)  Slow(FULL)
 */
void testDualScalarSlowChangedMidInterval()
{
    DualScalarHarness harness;
    QueueCollectionData entry;

    harness.setTime(100);
    harness.stageFast();
    harness.stageSlow();
    harness.setTime(101);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 2u, Action::FULL, true, Action::FULL);

    harness.stageFast();
    harness.stageSlow(std::vector<char>{'N', 'E', 'W'});
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 2u, Action::CARRY, true, Action::FULL);
    expectPayload(entry, kSlowScalarCid, std::vector<char>{'N', 'E', 'W'});
}

/*
 * testDualScalarSlowAbsentThreeFlushes (Tier 4)
 *
 * Collects: Fast every window (F); slow staged only at W1 (S).
 * Covers:   Slow omitted on fast-only windows; no absent-window heartbeat refresh.
 *
 * Time  WindowID  Fast(collect)  Slow(collect)  WindowSent
 * ----  --------  -------------  -------------  --------------------------
 *  100         1              F              S  Fast(FULL)  Slow(FULL)
 *  101         2              F              -  Fast(CARRY)
 *  102         3              F              -  Fast(CARRY)
 *  103         4              F              -  Fast(FULL)
 */
void testDualScalarSlowAbsentThreeFlushes()
{
    DualScalarHarness harness;
    QueueCollectionData entry;

    harness.setTime(100);
    harness.stageFast();
    harness.stageSlow();
    harness.setTime(101);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 2u, Action::FULL, true, Action::FULL);

    harness.stageFast();
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 1u);

    harness.stageFast();
    harness.setTime(103);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 1u);

    harness.stageFast();
    harness.setTime(104);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 1u);
}

/*
 * testDualScalarSlowAbsentUntilW5 (Tier 4)
 *
 * Collects: Fast every window (F); slow staged only at W1 (S).
 * Covers:   Slow omitted on fast-only windows through W5.
 *
 * Time  WindowID  Fast(collect)  Slow(collect)  WindowSent
 * ----  --------  -------------  -------------  --------------------------
 *  100         1              F              S  Fast(FULL)  Slow(FULL)
 *  101         2              F              -  Fast(CARRY)
 *  102         3              F              -  Fast(CARRY)
 *  103         4              F              -  Fast(FULL)
 *  104         5              F              -  Fast(CARRY)
 */
void testDualScalarSlowAbsentUntilW5()
{
    DualScalarHarness harness;
    QueueCollectionData entry;

    harness.setTime(100);
    harness.stageFast();
    harness.stageSlow();
    harness.setTime(101);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));

    harness.stageFast();
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));

    harness.stageFast();
    harness.setTime(103);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));

    harness.stageFast();
    harness.setTime(104);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 1u);

    harness.stageFast();
    harness.setTime(105);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 1u);
}

/*
 * testDualScalarSlowReappearsAfterForcedRefresh (Tier 4)
 *
 * Collects: Slow at W1 (S); fast from W2 onward (F); slow re-stages at W5 (S).
 * Covers:   Slow re-stages at W5 after fast-only windows; no absent refresh at W4.
 *
 * Time  WindowID  Fast(collect)  Slow(collect)  WindowSent
 * ----  --------  -------------  -------------  --------------------------
 *  100         1              -              S  Slow(FULL)
 *  101         2              F              -  Fast(FULL)
 *  102         3              F              -  Fast(CARRY)
 *  103         4              F              -  Fast(CARRY)
 *  104         5              F              S  Fast(FULL)   Slow(CARRY)
 */
void testDualScalarSlowReappearsAfterForcedRefresh()
{
    DualScalarHarness harness;
    QueueCollectionData entry;

    harness.setTime(100);
    harness.stageSlow();
    harness.setTime(101);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 1u);
    expectAction(entry, kSlowScalarCid, Action::FULL);

    harness.stageFast();
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 1u, Action::FULL, false);

    harness.stageFast();
    harness.setTime(103);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 1u, Action::CARRY, false);

    harness.stageFast();
    harness.setTime(104);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 1u);

    harness.stageFast();
    harness.stageSlow();
    harness.setTime(105);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);
    EXPECT_TRUE(findPipelineEntry(entry, kFastScalarCid) != nullptr);
    EXPECT_TRUE(findPipelineEntry(entry, kSlowScalarCid) != nullptr);
}

/*
 * testDualScalarSlowSecondHeartbeatIntervalRefresh (Tier 4)
 *
 * Collects: Both at W1 (F, S); fast only W2-W9 with a new payload each window
 *           (F0, F1, ... F7); slow never re-stages after W1.
 * Covers:   Slow omitted on all fast-only windows after W1 (no absent refresh).
 *
 * Payload key: Fn={'F',n}
 *
 * Time  WindowID  Fast(collect)  Slow(collect)  WindowSent
 * ----  --------  -------------  -------------  --------------------------
 *  100         1              F              S  Fast(FULL)  Slow(FULL)
 *  101         2             F0              -  Fast(FULL)
 *  102         3             F1              -  Fast(FULL)
 *  103         4             F2              -  Fast(FULL)
 *  104         5             F3              -  Fast(FULL)
 *  105         6             F4              -  Fast(FULL)
 *  106         7             F5              -  Fast(FULL)
 *  107         8             F6              -  Fast(FULL)
 *  108         9             F7              -  Fast(FULL)
 */
void testDualScalarSlowSecondHeartbeatIntervalRefresh()
{
    DualScalarHarness harness;
    QueueCollectionData entry;

    harness.setTime(100);
    harness.stageFast();
    harness.stageSlow();
    harness.setTime(101);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectFlush(entry, 2u, Action::FULL, true, Action::FULL);
    expectPayload(entry, kSlowScalarCid, kSlowPayload);

    for (size_t step = 0; step < 8; ++step)
    {
        const auto fast_payload = std::vector<char>{'F', static_cast<char>('0' + step)};

        harness.stageFast(fast_payload);
        harness.setTime(102 + step);
        harness.flush();
        EXPECT_TRUE(harness.pop(entry));

        expectAction(entry, kFastScalarCid, Action::FULL);
        expectPayload(entry, kFastScalarCid, fast_payload);
        EXPECT_EQUAL(entry.entries.size(), 1u);
        expectAbsent(entry, kSlowScalarCid);
    }
}

// Contig container tests (CID=7). Default heartbeat=100 unless noted.
// Bin notation: InstX = {'I','n','s','t',X}. Empty trailing bins omitted from collect.
// TODO cnyce: Interior CARRY rows (unchanged contig re-stage) inside a heartbeat
// interval already anchored by FULL are redundant for Python rebuild and waste disk
// space, but omit-on-unchanged encoding is not worth the effort right now.
// We can revisit this later to keep making the DB smaller.
/*
 * testContigFirstCollectionFull
 *
 * Collects: Contig at W1 with three populated bins [Inst0, Inst1, Inst2].
 * Covers:   First contig collection always emits FULL with bin count in payload.
 *
 * Time  WindowID  Contig(collect)           WindowSent
 * ----  --------  ------------------------  -----------------
 *  100         1  [Inst0, Inst1, Inst2]     Contig(FULL)
 */
void testContigFirstCollectionFull()
{
    ContigTestHarness harness;
    const auto initial = contigBins({instBytes('0'), instBytes('1'), instBytes('2')});

    harness.setTime(100);
    harness.stage(initial);
    harness.setTime(101);
    harness.flush();

    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    const auto* pipeline_entry = findPipelineEntry(entry, kContigCid);
    EXPECT_TRUE(pipeline_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*pipeline_entry), static_cast<uint8_t>(Action::FULL));
    const auto payload = getPayload(*pipeline_entry);
    EXPECT_EQUAL(readUint16(payload, 0), 3u);
}

/*
 * testContigUnchangedCarry
 *
 * Collects: Contig at W1 and W2 with same two-bin payload [Inst0, Inst1].
 * Covers:   Unchanged contig re-stage emits CARRY (no delta classification).
 *
 * Time  WindowID  Contig(collect)    WindowSent
 * ----  --------  -----------------  -----------------
 *  100         1  [Inst0, Inst1]     Contig(FULL)
 *  101         2  [Inst0, Inst1]     Contig(CARRY)
 */
void testContigUnchangedCarry()
{
    ContigTestHarness harness;
    const auto initial = contigBins({instBytes('0'), instBytes('1')});

    harness.setTime(100);
    harness.stage(initial);
    harness.setTime(101);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));

    harness.stage(initial);
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    const auto* pipeline_entry = findPipelineEntry(entry, kContigCid);
    EXPECT_TRUE(pipeline_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*pipeline_entry), static_cast<uint8_t>(Action::CARRY));
}

/*
 * testContigArrive
 *
 * Collects: Contig at W1 with 12 bins (Inst0..Inst9, InstA, InstB); W2 adds InstC.
 * Covers:   classifyContigChange detects ARRIVE; wire payload is the new bin only.
 *
 * Time  WindowID  Contig(collect)              WindowSent
 * ----  --------  ---------------------------  --------------------------
 *  100         1  [Inst0..InstB] (12 bins)     Contig(FULL)
 *  101         2  [Inst0..InstB, InstC]        Contig(ARRIVE) InstC
 */
void testContigArrive()
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
    harness.setTime(101);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));

    auto with_arrival = initial;
    with_arrival.push_back(instBytes('C'));
    harness.stage(with_arrival);
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    const auto* pipeline_entry = findPipelineEntry(entry, kContigCid);
    EXPECT_TRUE(pipeline_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*pipeline_entry), static_cast<uint8_t>(Action::CONTIG_CONTAINER_ARRIVE));
    EXPECT_EQUAL(getPayload(*pipeline_entry), instBytes('C'));
}

/*
 * testContigSwap
 *
 * Collects: Contig at W1 [InstA, InstB, InstC]; W2 swaps middle bin to InstX.
 * Covers:   classifyContigChange detects SWAP; wire payload is index + new bin.
 *
 * Time  WindowID  Contig(collect)         WindowSent
 * ----  --------  ----------------------  --------------------------
 *  100         1  [InstA, InstB, InstC]   Contig(FULL)
 *  101         2  [InstA, InstX, InstC]   Contig(SWAP) idx=1 InstX
 */
void testContigSwap()
{
    ContigTestHarness harness;
    const auto initial = contigBins({instBytes('A'), instBytes('B'), instBytes('C')});
    const auto swapped = contigBins({instBytes('A'), instBytes('X'), instBytes('C')});

    harness.setTime(100);
    harness.stage(initial);
    harness.setTime(101);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));

    harness.stage(swapped);
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    const auto* pipeline_entry = findPipelineEntry(entry, kContigCid);
    EXPECT_TRUE(pipeline_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*pipeline_entry), static_cast<uint8_t>(Action::CONTIG_CONTAINER_SWAP));
    const auto payload = getPayload(*pipeline_entry);
    EXPECT_EQUAL(readUint16(payload, 0), 1u);
    EXPECT_EQUAL(std::vector<char>(payload.begin() + sizeof(uint16_t), payload.end()), instBytes('X'));
}

/*
 * testContigDepart
 *
 * Collects: Contig at W1 [InstA, InstB, InstC]; W2 removes head bin (shrink).
 * Covers:   classifyContigChange detects DEPART; wire payload is empty.
 *
 * Time  WindowID  Contig(collect)       WindowSent
 * ----  --------  --------------------  --------------------------
 *  100         1  [InstA, InstB, InstC]  Contig(FULL)
 *  101         2  [InstB, InstC]         Contig(DEPART)
 */
void testContigDepart()
{
    ContigTestHarness harness;
    const auto initial = contigBins({instBytes('A'), instBytes('B'), instBytes('C')});
    const auto departed = contigBins({instBytes('B'), instBytes('C')});

    harness.setTime(100);
    harness.stage(initial);
    harness.setTime(101);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));

    harness.stage(departed);
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    const auto* pipeline_entry = findPipelineEntry(entry, kContigCid);
    EXPECT_TRUE(pipeline_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*pipeline_entry), static_cast<uint8_t>(Action::CONTIG_CONTAINER_DEPART));
    EXPECT_EQUAL(getPayload(*pipeline_entry).size(), 0u);
}

/*
 * testContigBookends
 *
 * Collects: Contig at W1 [InstA..InstD]; W2 shifts window (drop InstA, add InstE).
 * Covers:   classifyContigChange detects BOOKENDS; wire payload is the new tail bin.
 *
 * Time  WindowID  Contig(collect)           WindowSent
 * ----  --------  ------------------------  --------------------------
 *  100         1  [InstA, InstB, InstC, InstD]  Contig(FULL)
 *  101         2  [InstB, InstC, InstD, InstE]  Contig(BOOKENDS) InstE
 */
void testContigBookends()
{
    ContigTestHarness harness;
    const auto initial = contigBins({instBytes('A'), instBytes('B'), instBytes('C'), instBytes('D')});
    const auto shifted = contigBins({instBytes('B'), instBytes('C'), instBytes('D'), instBytes('E')});

    harness.setTime(100);
    harness.stage(initial);
    harness.setTime(101);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));

    harness.stage(shifted);
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    const auto* pipeline_entry = findPipelineEntry(entry, kContigCid);
    EXPECT_TRUE(pipeline_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*pipeline_entry), static_cast<uint8_t>(Action::CONTIG_CONTAINER_BOOKENDS));
    EXPECT_EQUAL(getPayload(*pipeline_entry), instBytes('E'));
}

/*
 * testContigHeartbeatBookendsOverride
 *
 * Collects: Contig W1-W3 unchanged [InstA..InstD]; W4 shifts to bookends shape
 *           [InstB, InstC, InstD, InstE] (heartbeat=3).
 * Covers:   force_snapshot_ at heartbeat boundary overrides BOOKENDS delta;
 *           W4 emits FULL instead of CONTIG_CONTAINER_BOOKENDS.
 *
 * Time  WindowID  Contig(collect)           WindowSent
 * ----  --------  ------------------------  -----------------
 *  100         1  [InstA, InstB, InstC, InstD]  Contig(FULL)
 *  101         2  [InstA, InstB, InstC, InstD]  Contig(CARRY)
 *  102         3  [InstA, InstB, InstC, InstD]  Contig(CARRY)
 *  103         4  [InstB, InstC, InstD, InstE]  Contig(FULL)
 */
void testContigHeartbeatBookendsOverride()
{
    ContigTestHarness harness(kHeartbeat);
    const auto initial = contigBins({instBytes('A'), instBytes('B'), instBytes('C'), instBytes('D')});
    const auto shifted = contigBins({instBytes('B'), instBytes('C'), instBytes('D'), instBytes('E')});

    harness.setTime(100);
    harness.stage(initial);
    harness.setTime(101);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*findPipelineEntry(entry, kContigCid)), static_cast<uint8_t>(Action::FULL));

    harness.stage(initial);
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*findPipelineEntry(entry, kContigCid)), static_cast<uint8_t>(Action::CARRY));

    harness.stage(initial);
    harness.setTime(103);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*findPipelineEntry(entry, kContigCid)), static_cast<uint8_t>(Action::CARRY));

    harness.stage(shifted);
    harness.setTime(104);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    const auto* pipeline_entry = findPipelineEntry(entry, kContigCid);
    EXPECT_TRUE(pipeline_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*pipeline_entry), static_cast<uint8_t>(Action::FULL));
}

/*
 * testContigDisableEnablePipeline
 *
 * Collects: Contig data at W1 [InstQ, InstU, InstI]; disable at W2; enable at W3.
 * Covers:   Lifecycle events DISABLED and ENABLED on the contig checkpointer;
 *           ENABLED carries prior full container snapshot (bin count=3).
 *
 * Time  WindowID  Contig(collect)       WindowSent
 * ----  --------  --------------------  -----------------
 *  100         1  [InstQ, InstU, InstI]  Contig(FULL)
 *  101         2  (disable)              Contig(DISABLED)
 *  102         3  (enable)               Contig(ENABLED) snapshot size=3
 */
void testContigDisableEnablePipeline()
{
    ContigTestHarness harness;
    const auto initial = contigBins({instBytes('Q'), instBytes('U'), instBytes('I')});

    harness.setTime(100);
    harness.stage(initial);
    harness.setTime(101);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*findPipelineEntry(entry, kContigCid)), static_cast<uint8_t>(Action::FULL));

    harness.disable();
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(getActionByte(*findPipelineEntry(entry, kContigCid)), static_cast<uint8_t>(Action::DISABLED));

    harness.enable();
    harness.setTime(103);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    const auto* pipeline_entry = findPipelineEntry(entry, kContigCid);
    EXPECT_TRUE(pipeline_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*pipeline_entry), static_cast<uint8_t>(Action::ENABLED));
    const auto payload = getPayload(*pipeline_entry);
    EXPECT_EQUAL(readUint16(payload, 0), 3u);
}

// Fast scalar (every window) + slow contig (sparse cadence). Contig heartbeat=100 in phase 1 so
// wire_distance does not force FULL between delta actions; phase 2 uses heartbeat=3.
/*
 * testFastScalarSlowContigExhaustive
 *
 * Collects: Fast scalar every window (F); slow contig staged only on action windows.
 * Covers:   Every contig wire action with fast scalar coexisting in the same flush;
 *           phase 2 verifies heartbeat forces FULL over BOOKENDS classification;
 *           phase 3 verifies classifyContigChange FULL inside a heartbeat interval.
 *
 * Phase 1 — contig delta + lifecycle (contig heartbeat=100).
 * Contig(collect): bin payload staged, "-" if not staged, or lifecycle disable/enable.
 *
 * Time  WindowID  Fast(collect)  Contig(collect)      WindowSent
 * ----  --------  -------------  -------------------  ---------------------------------
 *  100         1              F  [A,B,C]              Fast(FULL)   Contig(FULL)
 *  101         2              F  -                    Fast(CARRY)
 *  102         3              F  [A,B,C]              Fast(CARRY)  Contig(CARRY)
 *  103         4              F  -                    Fast(CARRY)
 *  104         5              F  [A,X,C]              Fast(CARRY)  Contig(SWAP) idx=1 X
 *  105         6              F  -                    Fast(CARRY)
 *  106         7              F  [A,X,C,D]            Fast(CARRY)  Contig(ARRIVE) D
 *  107         8              F  -                    Fast(CARRY)
 *  108         9              F  [X,C,D]              Fast(CARRY)  Contig(DEPART)
 *  109        10              F  -                    Fast(CARRY)
 *  110        11              F  [C,D,E]              Fast(CARRY)  Contig(BOOKENDS) E
 *  111        12              F  disable              Fast(CARRY)  Contig(DISABLED)
 *  112        13              F  -                    Fast(CARRY)
 *  113        14              F  enable               Fast(CARRY)  Contig(ENABLED) bins=3
 *
 * Phase 2 — heartbeat FULL override (contig heartbeat=3):
 *
 * Time  WindowID  Fast(collect)  Contig(collect)      WindowSent
 * ----  --------  -------------  -------------------  ---------------------------------
 *  200         1              F  [A,B,C,D]            Fast(FULL)   Contig(FULL)
 *  201         2              F  [A,B,C,D]            Fast(CARRY)  Contig(CARRY)
 *  202         3              F  [A,B,C,D]            Fast(CARRY)  Contig(CARRY)
 *  203         4              F  [B,C,D,E]            Fast(FULL)   Contig(FULL)
 *
 * Two interior CARRY wires advance contig wire_distance so heartbeat forces FULL at W4.
 *
 * Phase 3 — classifyContigChange FULL inside heartbeat interval (contig heartbeat=100):
 *
 * Time  WindowID  Fast(collect)  Contig(collect)      WindowSent
 * ----  --------  -------------  -------------------  ---------------------------------
 *  300         1              F  [A,B,C]              Fast(FULL)   Contig(FULL)
 *  301         2              F  -                    Fast(CARRY)
 *  302         3              F  [Z,Y,W]              Fast(CARRY)  Contig(FULL)
 *
 * W3 changes all three bins (no single SWAP/BOOKENDS delta); still inside the interval
 * anchored by W1 FULL — emit FULL from classification, not heartbeat forcing.
 */
 void testFastScalarSlowContigExhaustive()
 {
     QueueCollectionData entry;
 
     const auto bins_abc = contigBins({instBytes('A'), instBytes('B'), instBytes('C')});
     const auto bins_axc = contigBins({instBytes('A'), instBytes('X'), instBytes('C')});
     const auto bins_axcd = contigBins({instBytes('A'), instBytes('X'), instBytes('C'), instBytes('D')});
     const auto bins_xcd = contigBins({instBytes('X'), instBytes('C'), instBytes('D')});
     const auto bins_cde = contigBins({instBytes('C'), instBytes('D'), instBytes('E')});
     const auto bins_zyw = contigBins({instBytes('Z'), instBytes('Y'), instBytes('W')});
 
     FastScalarSlowContigHarness harness;
 
     harness.setTime(100);
     harness.stageFast();
     harness.stageContig(bins_abc);
     harness.setTime(101);
     harness.flush();
     EXPECT_TRUE(harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 2u);
     expectContigFullBinCount(entry, kSlowContigCid, 3u);
 
     harness.stageFast();
     harness.setTime(102);
     harness.flush();
     EXPECT_TRUE(harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 1u);
     expectAbsent(entry, kSlowContigCid);
 
     harness.stageFast();
     harness.stageContig(bins_abc);
     harness.setTime(103);
     harness.flush();
     EXPECT_TRUE(harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 2u);
     expectContigAction(entry, kSlowContigCid, Action::CARRY);
 
     harness.stageFast();
     harness.setTime(104);
     harness.flush();
     EXPECT_TRUE(harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 1u);
 
     harness.stageFast();
     harness.stageContig(bins_axc);
     harness.setTime(105);
     harness.flush();
     EXPECT_TRUE(harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 2u);
     expectContigSwap(entry, kSlowContigCid, 1u, instBytes('X'));
 
     harness.stageFast();
     harness.setTime(106);
     harness.flush();
     EXPECT_TRUE(harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 1u);
 
     harness.stageFast();
     harness.stageContig(bins_axcd);
     harness.setTime(107);
     harness.flush();
     EXPECT_TRUE(harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 2u);
     expectContigAction(entry, kSlowContigCid, Action::CONTIG_CONTAINER_ARRIVE);
     expectPayload(entry, kSlowContigCid, instBytes('D'));
 
     harness.stageFast();
     harness.setTime(108);
     harness.flush();
     EXPECT_TRUE(harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 1u);
 
     harness.stageFast();
     harness.stageContig(bins_xcd);
     harness.setTime(109);
     harness.flush();
     EXPECT_TRUE(harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 2u);
     expectContigAction(entry, kSlowContigCid, Action::CONTIG_CONTAINER_DEPART);
     expectPayload(entry, kSlowContigCid, {});
 
     harness.stageFast();
     harness.setTime(110);
     harness.flush();
     EXPECT_TRUE(harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 1u);
 
     harness.stageFast();
     harness.stageContig(bins_cde);
     harness.setTime(111);
     harness.flush();
     EXPECT_TRUE(harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 2u);
     expectContigAction(entry, kSlowContigCid, Action::CONTIG_CONTAINER_BOOKENDS);
     expectPayload(entry, kSlowContigCid, instBytes('E'));
 
     harness.stageFast();
     harness.disableContig();
     harness.setTime(112);
     harness.flush();
     EXPECT_TRUE(harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 2u);
     expectContigAction(entry, kSlowContigCid, Action::DISABLED);
 
     harness.stageFast();
     harness.setTime(113);
     harness.flush();
     EXPECT_TRUE(harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 1u);
 
     harness.stageFast();
     harness.enableContig();
     harness.setTime(114);
     harness.flush();
     EXPECT_TRUE(harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 2u);
     expectContigAction(entry, kSlowContigCid, Action::ENABLED);
     EXPECT_EQUAL(readUint16(getPayload(*findPipelineEntry(entry, kSlowContigCid)), 0), 3u);
 
     const auto bins_abcd = contigBins({instBytes('A'), instBytes('B'), instBytes('C'), instBytes('D')});
     const auto bins_bcde = contigBins({instBytes('B'), instBytes('C'), instBytes('D'), instBytes('E')});
 
     FastScalarSlowContigHarness heartbeat_harness(kHeartbeat);
 
     heartbeat_harness.setTime(200);
     heartbeat_harness.stageFast();
     heartbeat_harness.stageContig(bins_abcd);
     heartbeat_harness.setTime(201);
     heartbeat_harness.flush();
     EXPECT_TRUE(heartbeat_harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 2u);
     expectContigFullBinCount(entry, kSlowContigCid, 4u);
 
     heartbeat_harness.stageFast();
     heartbeat_harness.stageContig(bins_abcd);
     heartbeat_harness.setTime(202);
     heartbeat_harness.flush();
     EXPECT_TRUE(heartbeat_harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 2u);
     expectContigAction(entry, kSlowContigCid, Action::CARRY);
 
     heartbeat_harness.stageFast();
     heartbeat_harness.stageContig(bins_abcd);
     heartbeat_harness.setTime(203);
     heartbeat_harness.flush();
     EXPECT_TRUE(heartbeat_harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 2u);
     expectContigAction(entry, kSlowContigCid, Action::CARRY);
 
     heartbeat_harness.stageFast();
     heartbeat_harness.stageContig(bins_bcde);
     heartbeat_harness.setTime(204);
     heartbeat_harness.flush();
     EXPECT_TRUE(heartbeat_harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 2u);
     expectContigFullBinCount(entry, kSlowContigCid, 4u);
 
     FastScalarSlowContigHarness classify_full_harness;
 
     classify_full_harness.setTime(300);
     classify_full_harness.stageFast();
     classify_full_harness.stageContig(bins_abc);
     classify_full_harness.setTime(301);
     classify_full_harness.flush();
     EXPECT_TRUE(classify_full_harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 2u);
     expectContigFullBinCount(entry, kSlowContigCid, 3u);
 
     classify_full_harness.stageFast();
     classify_full_harness.setTime(302);
     classify_full_harness.flush();
     EXPECT_TRUE(classify_full_harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 1u);
 
     classify_full_harness.stageFast();
     classify_full_harness.stageContig(bins_zyw);
     classify_full_harness.setTime(303);
     classify_full_harness.flush();
     EXPECT_TRUE(classify_full_harness.pop(entry));
     EXPECT_EQUAL(entry.entries.size(), 2u);
     expectContigFullBinCount(entry, kSlowContigCid, 3u);
 }
 
 // Sparse container tests (CID=9). Default heartbeat=100 unless noted.
// Bin notation: idx:BinX = map entry {idx, {'B','i','n',X}}.
// TODO cnyce: Interior CARRY rows (unchanged sparse re-stage) inside a heartbeat
// interval already anchored by FULL are redundant for Python rebuild and waste disk
// space, but omit-on-unchanged encoding is not worth the effort right now.
// We can revisit this later to keep making the DB smaller.
/*
 * testSparseFirstCollectionFull
 *
 * Collects: Sparse at W1 with bins {1:BinA, 5:BinB}.
 * Covers:   First sparse collection always emits FULL with element count in payload.
 *
 * Time  WindowID  Sparse(collect)       WindowSent
 * ----  --------  --------------------  -----------------
 *  100         1  {1:BinA, 5:BinB}      Sparse(FULL)
 */
void testSparseFirstCollectionFull()
{
    SparseTestHarness harness;
    const auto initial = sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('B')}});

    harness.setTime(100);
    harness.stage(initial);
    harness.setTime(101);
    harness.flush();

    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    expectSparseFullBinCount(entry, kSparseCid, 2u);
    EXPECT_EQUAL(readUint16(getPayload(*findPipelineEntry(entry, kSparseCid)), sizeof(uint16_t)), 1u);
}

/*
 * testSparseUnchangedCarry
 *
 * Collects: Sparse at W1 and W2 with same map {1:BinA, 5:BinB}.
 * Covers:   Unchanged sparse re-stage emits CARRY.
 *
 * Time  WindowID  Sparse(collect)       WindowSent
 * ----  --------  --------------------  -----------------
 *  100         1  {1:BinA, 5:BinB}      Sparse(FULL)
 *  101         2  {1:BinA, 5:BinB}      Sparse(CARRY)
 */
void testSparseUnchangedCarry()
{
    SparseTestHarness harness;
    const auto initial = sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('B')}});

    harness.setTime(100);
    harness.stage(initial);
    harness.setTime(101);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));

    harness.stage(initial);
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSparseAction(entry, kSparseCid, Action::CARRY);
}

/*
 * testSparseSwap
 *
 * Collects: Sparse at W1 {1:BinA, 5:BinB}; W2 swaps bin 5 to BinX.
 * Covers:   classifySparseChange detects SWAP; wire payload is index + new bin.
 *
 * Time  WindowID  Sparse(collect)       WindowSent
 * ----  --------  --------------------  --------------------------
 *  100         1  {1:BinA, 5:BinB}      Sparse(FULL)
 *  101         2  {1:BinA, 5:BinX}      Sparse(SWAP) idx=5 BinX
 */
void testSparseSwap()
{
    SparseTestHarness harness;
    const auto initial = sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('B')}});
    const auto swapped = sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('X')}});

    harness.setTime(100);
    harness.stage(initial);
    harness.setTime(101);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));

    harness.stage(swapped);
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSparseSwap(entry, kSparseCid, 5u, sparseBinBytes('X'));
}

/*
 * testSparseRemove
 *
 * Collects: Sparse at W1 {1:BinA, 5:BinB, 9:BinC}; W2 removes bin 5.
 * Covers:   classifySparseChange detects REMOVE; wire payload is index only.
 *
 * Time  WindowID  Sparse(collect)            WindowSent
 * ----  --------  -------------------------  --------------------------
 *  100         1  {1:BinA, 5:BinB, 9:BinC}   Sparse(FULL)
 *  101         2  {1:BinA, 9:BinC}           Sparse(REMOVE) idx=5
 */
void testSparseRemove()
{
    SparseTestHarness harness;
    const auto initial =
        sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('B')}, {9, sparseBinBytes('C')}});
    const auto removed = sparseBins({{1, sparseBinBytes('A')}, {9, sparseBinBytes('C')}});

    harness.setTime(100);
    harness.stage(initial);
    harness.setTime(101);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));

    harness.stage(removed);
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSparseRemove(entry, kSparseCid, 5u);
}

/*
 * testSparseHeartbeatSwapOverride
 *
 * Collects: Sparse W1-W3 unchanged {1:BinA, 5:BinB}; W4 swaps bin 5 to BinX (heartbeat=3).
 * Covers:   force_snapshot_ at heartbeat boundary overrides SWAP delta; W4 emits FULL.
 *
 * Time  WindowID  Sparse(collect)       WindowSent
 * ----  --------  --------------------  -----------------
 *  100         1  {1:BinA, 5:BinB}      Sparse(FULL)
 *  101         2  {1:BinA, 5:BinB}      Sparse(CARRY)
 *  102         3  {1:BinA, 5:BinB}      Sparse(CARRY)
 *  103         4  {1:BinA, 5:BinX}      Sparse(FULL)
 */
void testSparseHeartbeatSwapOverride()
{
    SparseTestHarness harness(kHeartbeat);
    const auto initial = sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('B')}});
    const auto swapped = sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('X')}});

    harness.setTime(100);
    harness.stage(initial);
    harness.setTime(101);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    expectSparseFullBinCount(entry, kSparseCid, 2u);

    harness.stage(initial);
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSparseAction(entry, kSparseCid, Action::CARRY);

    harness.stage(initial);
    harness.setTime(103);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSparseAction(entry, kSparseCid, Action::CARRY);

    harness.stage(swapped);
    harness.setTime(104);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSparseFullBinCount(entry, kSparseCid, 2u);
}

/*
 * testSparseClassifyFullMidInterval
 *
 * Collects: Sparse W1 {1:BinA, 5:BinB}; W2 changes both bins (heartbeat=100).
 * Covers:   classifySparseChange FULL inside heartbeat interval (not heartbeat forcing).
 *
 * Time  WindowID  Sparse(collect)       WindowSent
 * ----  --------  --------------------  -----------------
 *  100         1  {1:BinA, 5:BinB}      Sparse(FULL)
 *  101         2  {1:BinX, 5:BinY}      Sparse(FULL)
 */
void testSparseClassifyFullMidInterval()
{
    SparseTestHarness harness;
    const auto initial = sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('B')}});
    const auto changed = sparseBins({{1, sparseBinBytes('X')}, {5, sparseBinBytes('Y')}});

    harness.setTime(100);
    harness.stage(initial);
    harness.setTime(101);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    expectSparseFullBinCount(entry, kSparseCid, 2u);

    harness.stage(changed);
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSparseFullBinCount(entry, kSparseCid, 2u);
}

/*
 * testSparseDisableEnablePipeline
 *
 * Collects: Sparse data at W1 {2:BinQ, 4:BinU, 6:BinI}; disable at W2; enable at W3.
 * Covers:   Lifecycle events DISABLED and ENABLED; ENABLED carries prior sparse snapshot.
 *
 * Time  WindowID  Sparse(collect)            WindowSent
 * ----  --------  -------------------------  -----------------
 *  100         1  {2:BinQ, 4:BinU, 6:BinI}   Sparse(FULL)
 *  101         2  (disable)                  Sparse(DISABLED)
 *  102         3  (enable)                   Sparse(ENABLED) elements=3
 */
void testSparseDisableEnablePipeline()
{
    SparseTestHarness harness;
    const auto initial =
        sparseBins({{2, sparseBinBytes('Q')}, {4, sparseBinBytes('U')}, {6, sparseBinBytes('I')}});

    harness.setTime(100);
    harness.stage(initial);
    harness.setTime(101);
    harness.flush();
    QueueCollectionData entry;
    EXPECT_TRUE(harness.pop(entry));
    expectSparseFullBinCount(entry, kSparseCid, 3u);

    harness.disable();
    harness.setTime(102);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSparseAction(entry, kSparseCid, Action::DISABLED);

    harness.enable();
    harness.setTime(103);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    expectSparseAction(entry, kSparseCid, Action::ENABLED);
    EXPECT_EQUAL(readUint16(getPayload(*findPipelineEntry(entry, kSparseCid)), 0), 3u);
}

// Fast scalar (every window) + slow sparse (sparse). Sparse heartbeat=100 in phase 1 so
// wire_distance does not force FULL between delta actions; phase 2 uses heartbeat=3.
/*
 * testFastScalarSlowSparseExhaustive
 *
 * Collects: Fast scalar every window (F); slow sparse staged only on action windows.
 * Covers:   Every sparse wire action with fast scalar coexisting in the same flush;
 *           phase 2 verifies heartbeat forces FULL over SWAP classification;
 *           phase 3 verifies classifySparseChange FULL inside a heartbeat interval.
 *
 * Phase 1 — sparse delta + lifecycle (sparse heartbeat=100).
 * Sparse(collect): map payload staged, "-" if not staged, or lifecycle disable/enable.
 *
 * Time  WindowID  Fast(collect)  Sparse(collect)              WindowSent
 * ----  --------  -------------  ---------------------------  ---------------------------------
 *  400         1              F  {1:BinA,5:BinB,9:BinC}       Fast(FULL)   Sparse(FULL)
 *  401         2              F  -                            Fast(CARRY)
 *  402         3              F  {1:BinA,5:BinB,9:BinC}       Fast(CARRY)  Sparse(CARRY)
 *  403         4              F  -                            Fast(CARRY)
 *  404         5              F  {1:BinA,5:BinX,9:BinC}       Fast(CARRY)  Sparse(SWAP) idx=5 X
 *  405         6              F  -                            Fast(CARRY)
 *  406         7              F  {1:BinA,5:BinX,9:BinC}       Fast(CARRY)  Sparse(CARRY)
 *  407         8              F  -                            Fast(CARRY)
 *  408         9              F  {1:BinA,9:BinC}              Fast(CARRY)  Sparse(REMOVE) idx=5
 *  409        10              F  -                            Fast(CARRY)
 *  410        11              F  disable                      Fast(CARRY)  Sparse(DISABLED)
 *  411        12              F  -                            Fast(CARRY)
 *  412        13              F  enable                       Fast(CARRY)  Sparse(ENABLED) elems=2
 *
 * Phase 2 — heartbeat FULL override (sparse heartbeat=3):
 *
 * Time  WindowID  Fast(collect)  Sparse(collect)              WindowSent
 * ----  --------  -------------  ---------------------------  ---------------------------------
 *  500         1              F  {1:BinA,5:BinB,9:BinC,12:BinD}  Fast(FULL)  Sparse(FULL)
 *  501         2              F  {1:BinA,5:BinB,9:BinC,12:BinD}  Fast(CARRY) Sparse(CARRY)
 *  502         3              F  {1:BinA,5:BinB,9:BinC,12:BinD}  Fast(CARRY) Sparse(CARRY)
 *  503         4              F  {1:BinA,5:BinX,9:BinC,12:BinD}  Fast(FULL)  Sparse(FULL)
 *
 * Phase 3 — classifySparseChange FULL inside heartbeat interval (sparse heartbeat=100):
 *
 * Time  WindowID  Fast(collect)  Sparse(collect)       WindowSent
 * ----  --------  -------------  --------------------  ---------------------------------
 *  600         1              F  {1:BinA, 5:BinB}      Fast(FULL)   Sparse(FULL)
 *  601         2              F  -                     Fast(CARRY)
 *  602         3              F  {1:BinX, 5:BinY}      Fast(CARRY)  Sparse(FULL)
 */
void testFastScalarSlowSparseExhaustive()
{
    QueueCollectionData entry;

    const auto bins_abc =
        sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('B')}, {9, sparseBinBytes('C')}});
    const auto bins_axc =
        sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('X')}, {9, sparseBinBytes('C')}});
    const auto bins_ac = sparseBins({{1, sparseBinBytes('A')}, {9, sparseBinBytes('C')}});
    const auto bins_ab = sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('B')}});
    const auto bins_xy = sparseBins({{1, sparseBinBytes('X')}, {5, sparseBinBytes('Y')}});
    const auto bins_abcd =
        sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('B')}, {9, sparseBinBytes('C')},
                    {12, sparseBinBytes('D')}});
    const auto bins_axcd =
        sparseBins({{1, sparseBinBytes('A')}, {5, sparseBinBytes('X')}, {9, sparseBinBytes('C')},
                    {12, sparseBinBytes('D')}});

    FastScalarSlowSparseHarness harness;

    harness.setTime(400);
    harness.stageFast();
    harness.stageSparse(bins_abc);
    harness.setTime(401);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);
    expectSparseFullBinCount(entry, kSlowSparseCid, 3u);

    harness.stageFast();
    harness.setTime(402);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 1u);
    expectAbsent(entry, kSlowSparseCid);

    harness.stageFast();
    harness.stageSparse(bins_abc);
    harness.setTime(403);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);
    expectSparseAction(entry, kSlowSparseCid, Action::CARRY);

    harness.stageFast();
    harness.setTime(404);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 1u);

    harness.stageFast();
    harness.stageSparse(bins_axc);
    harness.setTime(405);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);
    expectSparseSwap(entry, kSlowSparseCid, 5u, sparseBinBytes('X'));

    harness.stageFast();
    harness.setTime(406);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 1u);

    harness.stageFast();
    harness.stageSparse(bins_axc);
    harness.setTime(407);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);
    expectSparseAction(entry, kSlowSparseCid, Action::CARRY);

    harness.stageFast();
    harness.setTime(408);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 1u);

    harness.stageFast();
    harness.stageSparse(bins_ac);
    harness.setTime(409);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);
    expectSparseRemove(entry, kSlowSparseCid, 5u);

    harness.stageFast();
    harness.setTime(410);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 1u);

    harness.stageFast();
    harness.disableSparse();
    harness.setTime(411);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);
    expectSparseAction(entry, kSlowSparseCid, Action::DISABLED);

    harness.stageFast();
    harness.setTime(412);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 1u);

    harness.stageFast();
    harness.enableSparse();
    harness.setTime(413);
    harness.flush();
    EXPECT_TRUE(harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);
    expectSparseAction(entry, kSlowSparseCid, Action::ENABLED);
    EXPECT_EQUAL(readUint16(getPayload(*findPipelineEntry(entry, kSlowSparseCid)), 0), 2u);

    FastScalarSlowSparseHarness heartbeat_harness(kHeartbeat);

    heartbeat_harness.setTime(500);
    heartbeat_harness.stageFast();
    heartbeat_harness.stageSparse(bins_abcd);
    heartbeat_harness.setTime(501);
    heartbeat_harness.flush();
    EXPECT_TRUE(heartbeat_harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);
    expectSparseFullBinCount(entry, kSlowSparseCid, 4u);

    heartbeat_harness.stageFast();
    heartbeat_harness.stageSparse(bins_abcd);
    heartbeat_harness.setTime(502);
    heartbeat_harness.flush();
    EXPECT_TRUE(heartbeat_harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);
    expectSparseAction(entry, kSlowSparseCid, Action::CARRY);

    heartbeat_harness.stageFast();
    heartbeat_harness.stageSparse(bins_abcd);
    heartbeat_harness.setTime(503);
    heartbeat_harness.flush();
    EXPECT_TRUE(heartbeat_harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);
    expectSparseAction(entry, kSlowSparseCid, Action::CARRY);

    heartbeat_harness.stageFast();
    heartbeat_harness.stageSparse(bins_axcd);
    heartbeat_harness.setTime(504);
    heartbeat_harness.flush();
    EXPECT_TRUE(heartbeat_harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);
    expectSparseFullBinCount(entry, kSlowSparseCid, 4u);

    FastScalarSlowSparseHarness classify_full_harness;

    classify_full_harness.setTime(600);
    classify_full_harness.stageFast();
    classify_full_harness.stageSparse(bins_ab);
    classify_full_harness.setTime(601);
    classify_full_harness.flush();
    EXPECT_TRUE(classify_full_harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);
    expectSparseFullBinCount(entry, kSlowSparseCid, 2u);

    classify_full_harness.stageFast();
    classify_full_harness.setTime(602);
    classify_full_harness.flush();
    EXPECT_TRUE(classify_full_harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 1u);

    classify_full_harness.stageFast();
    classify_full_harness.stageSparse(bins_xy);
    classify_full_harness.setTime(603);
    classify_full_harness.flush();
    EXPECT_TRUE(classify_full_harness.pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);
    expectSparseFullBinCount(entry, kSlowSparseCid, 2u);
}

// Mixed-type flush (heartbeat=3, scalar CID=1, contig CID=7).
/*
 * testScalarAndContigSameFlush
 *
 * Collects: Scalar SCA and contig [InstC, InstT] staged together at W1.
 * Covers:   PipelineStager emits both CIDs in a single QueueCollectionData
 *           when they share the same window flush.
 *
 * Payload key: SCA={'S','C','A'}
 *
 * Time  WindowID  Scalar(collect)  Contig(collect)  WindowSent
 * ----  --------  ---------------  ---------------  --------------------------
 *  100         1              SCA  [InstC, InstT]   Scalar(FULL)  Contig(FULL)
 */
void testScalarAndContigSameFlush()
{
    const std::vector<char> scalar_payload{'S', 'C', 'A'};
    const auto contig_payload = contigBins({instBytes('C'), instBytes('T')});

    uint64_t sim_time = 100;
    simdb::argos::Timestamp timestamp{&sim_time};
    simdb::ConcurrentQueue<QueueCollectionData> pipeline_queue;
    PipelineStager stager(kHeartbeat, &timestamp, &pipeline_queue);
    stager.disableAutoSendMode(true);
    stager.setScalarType(kScalarCid);
    stager.setContainerType(kContigCid, false, 16);

    stager.stage(kScalarCid, scalar_payload);
    stager.stage(kContigCid, contig_payload);
    sim_time = 101;
    stager.advanceSimTimeSlot();
    stager.sendCollectedDataToPipeline();

    QueueCollectionData entry;
    EXPECT_TRUE(pipeline_queue.try_pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);

    const auto* scalar_entry = findPipelineEntry(entry, kScalarCid);
    EXPECT_TRUE(scalar_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*scalar_entry), static_cast<uint8_t>(Action::FULL));
    EXPECT_EQUAL(getPayload(*scalar_entry), scalar_payload);

    const auto* contig_entry = findPipelineEntry(entry, kContigCid);
    EXPECT_TRUE(contig_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*contig_entry), static_cast<uint8_t>(Action::FULL));
    EXPECT_EQUAL(readUint16(getPayload(*contig_entry), 0), 2u);
}

/*
 * testScalarAndSparseSameFlush
 *
 * Collects: Scalar SCA and sparse {3:BinS, 7:BinP} staged together at W1.
 * Covers:   PipelineStager emits both CIDs in a single QueueCollectionData flush.
 *
 * Payload key: SCA={'S','C','A'}
 *
 * Time  WindowID  Scalar(collect)  Sparse(collect)       WindowSent
 * ----  --------  ---------------  --------------------  --------------------------
 *  100         1              SCA  {3:BinS, 7:BinP}      Scalar(FULL)  Sparse(FULL)
 */
void testScalarAndSparseSameFlush()
{
    const std::vector<char> scalar_payload{'S', 'C', 'A'};
    const auto sparse_payload = sparseBins({{3, sparseBinBytes('S')}, {7, sparseBinBytes('P')}});

    uint64_t sim_time = 100;
    simdb::argos::Timestamp timestamp{&sim_time};
    simdb::ConcurrentQueue<QueueCollectionData> pipeline_queue;
    PipelineStager stager(kHeartbeat, &timestamp, &pipeline_queue);
    stager.disableAutoSendMode(true);
    stager.setScalarType(kScalarCid);
    stager.setContainerType(kSparseCid, true, 16);

    stager.stage(kScalarCid, scalar_payload);
    stager.stage(kSparseCid, sparse_payload);
    sim_time = 101;
    stager.advanceSimTimeSlot();
    stager.sendCollectedDataToPipeline();

    QueueCollectionData entry;
    EXPECT_TRUE(pipeline_queue.try_pop(entry));
    EXPECT_EQUAL(entry.entries.size(), 2u);

    const auto* scalar_entry = findPipelineEntry(entry, kScalarCid);
    EXPECT_TRUE(scalar_entry != nullptr);
    EXPECT_EQUAL(getActionByte(*scalar_entry), static_cast<uint8_t>(Action::FULL));
    EXPECT_EQUAL(getPayload(*scalar_entry), scalar_payload);

    expectSparseFullBinCount(entry, kSparseCid, 2u);
}

} // namespace

TEST_INIT;

int main()
{
    testScalarFullThenCarryPipeline();
    testScalarHeartbeatPipeline();
    testDualScalarSlowSkipsTwoWindows();
    testDualScalarMultiClockPeriod5();
    testDualScalarSlowEveryOtherWindow();
    testDualScalarSlowReturnsMidInterval();
    testDualScalarSlowReturnsOnFastHeartbeatBoundary();
    testDualScalarSlowChangedAfterGap();
    testDualScalarSlowChangedMidInterval();
    testDualScalarSlowAbsentThreeFlushes();
    testDualScalarSlowAbsentUntilW5();
    testDualScalarSlowReappearsAfterForcedRefresh();
    testDualScalarSlowSecondHeartbeatIntervalRefresh();
    testContigFirstCollectionFull();
    testContigUnchangedCarry();
    testContigArrive();
    testContigSwap();
    testContigDepart();
    testContigBookends();
    testContigHeartbeatBookendsOverride();
    testContigDisableEnablePipeline();
    testFastScalarSlowContigExhaustive();
    testSparseFirstCollectionFull();
    testSparseUnchangedCarry();
    testSparseSwap();
    testSparseRemove();
    testSparseHeartbeatSwapOverride();
    testSparseClassifyFullMidInterval();
    testSparseDisableEnablePipeline();
    testFastScalarSlowSparseExhaustive();
    testScalarAndContigSameFlush();
    testScalarAndSparseSameFlush();

    REPORT_ERROR;
    return ERROR_CODE;
}
