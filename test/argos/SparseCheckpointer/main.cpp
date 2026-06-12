// clang-format off

#include "SimDBTester.hpp"
#include "simdb/apps/argos/Checkpointer.hpp"

#include <cstring>
#include <map>

namespace {

using simdb::argos::Action;
using simdb::argos::SparseCheckpointer;

constexpr auto kCidBytes = sizeof(uint16_t);
constexpr auto kActionBytes = sizeof(uint8_t);
constexpr uint16_t kCid = 55;
constexpr size_t kHeartbeat = 3;
constexpr size_t kLargeHeartbeat = 100;

std::map<uint16_t, std::vector<char>> sparseBins(
    std::initializer_list<std::pair<const uint16_t, std::vector<char>>> elems)
{
    return std::map<uint16_t, std::vector<char>>(elems);
}

std::vector<char> binBytes(char tag)
{
    return {'B', 'i', 'n', tag};
}

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

void expectMinifiedAction(const std::shared_ptr<const simdb::argos::Checkpoint>& cp, Action action)
{
    auto data = cp->getMinifiedData();
    EXPECT_EQUAL(getActionByte(*data), static_cast<uint8_t>(action));
}

void expectSparseFullMap(const std::shared_ptr<const simdb::argos::Checkpoint>& cp,
                         const std::map<uint16_t, std::vector<char>>& expected_bins)
{
    auto data = cp->getFullData();
    EXPECT_EQUAL(getActionByte(*data), static_cast<uint8_t>(Action::FULL));
    const auto payload = getPayload(*data);
    EXPECT_EQUAL(readUint16(payload, 0), simdb::argos::countSparseElements_(expected_bins));
    size_t offset = sizeof(uint16_t);
    for (const auto& [bin_idx, expected_bytes] : expected_bins)
    {
        if (expected_bytes.empty())
        {
            continue;
        }
        EXPECT_EQUAL(readUint16(payload, offset), bin_idx);
        offset += sizeof(uint16_t);
        EXPECT_TRUE(payload.size() >= offset + expected_bytes.size());
        EXPECT_EQUAL(std::vector<char>(payload.begin() + static_cast<std::ptrdiff_t>(offset),
                                       payload.begin() + static_cast<std::ptrdiff_t>(offset + expected_bytes.size())),
                     expected_bytes);
        offset += expected_bytes.size();
    }
}

void testFirstCollectionFull()
{
    SparseCheckpointer checkpointer(kCid, kLargeHeartbeat);
    const auto initial = sparseBins({{1, binBytes('A')}, {5, binBytes('B')}});

    auto first = checkpointer.createCheckpoint(initial);
    EXPECT_TRUE(first->isSnapshot());
    EXPECT_EQUAL(first->parent(), nullptr);
    expectMinifiedAction(first, Action::FULL);
    expectSparseFullMap(first, initial);
    EXPECT_EQUAL(first->getDistanceToSnapshot(), 0u);
}

void testUnchangedCarry()
{
    SparseCheckpointer checkpointer(kCid, kLargeHeartbeat);
    const auto initial = sparseBins({{1, binBytes('A')}, {5, binBytes('B')}});

    checkpointer.createCheckpoint(initial);
    auto carry = checkpointer.createCheckpoint(initial);
    EXPECT_FALSE(carry->isSnapshot());
    expectMinifiedAction(carry, Action::CARRY);
    expectSparseFullMap(carry, initial);
    EXPECT_EQUAL(carry->getDistanceToSnapshot(), 1u);
}

void testSwap()
{
    SparseCheckpointer checkpointer(kCid, kLargeHeartbeat);
    const auto initial = sparseBins({{1, binBytes('A')}, {5, binBytes('B')}});
    checkpointer.createCheckpoint(initial);

    auto swapped = sparseBins({{1, binBytes('A')}, {5, binBytes('X')}});
    auto swap = checkpointer.createCheckpoint(swapped);
    expectMinifiedAction(swap, Action::SPARSE_CONTAINER_SWAP);
    auto data = swap->getMinifiedData();
    const auto payload = getPayload(*data);
    EXPECT_EQUAL(readUint16(payload, 0), 5u);
    EXPECT_EQUAL(std::vector<char>(payload.begin() + sizeof(uint16_t), payload.end()), binBytes('X'));
    expectSparseFullMap(swap, swapped);
}

void testRemove()
{
    SparseCheckpointer checkpointer(kCid, kLargeHeartbeat);
    const auto initial = sparseBins({{1, binBytes('A')}, {5, binBytes('B')}, {9, binBytes('C')}});
    checkpointer.createCheckpoint(initial);

    const auto removed = sparseBins({{1, binBytes('A')}, {9, binBytes('C')}});
    auto remove = checkpointer.createCheckpoint(removed);
    expectMinifiedAction(remove, Action::SPARSE_CONTAINER_REMOVE);
    const auto payload = getPayload(*remove->getMinifiedData());
    EXPECT_EQUAL(readUint16(payload, 0), 5u);
    expectSparseFullMap(remove, removed);
}

void testHeartbeatForcesFull()
{
    SparseCheckpointer checkpointer(kCid, kHeartbeat);
    const auto initial = sparseBins({{1, binBytes('A')}, {5, binBytes('B')}});
    auto swapped = sparseBins({{1, binBytes('A')}, {5, binBytes('X')}});

    checkpointer.createCheckpoint(initial);
    checkpointer.createCheckpoint(initial);
    checkpointer.createCheckpoint(initial);

    auto heartbeat_full = checkpointer.createCheckpoint(swapped);
    EXPECT_TRUE(heartbeat_full->isSnapshot());
    expectMinifiedAction(heartbeat_full, Action::FULL);
    expectSparseFullMap(heartbeat_full, swapped);
    EXPECT_EQUAL(heartbeat_full->getDistanceToSnapshot(), 0u);
}

void testDisableAndReenable()
{
    SparseCheckpointer checkpointer(kCid, kHeartbeat);
    const auto initial = sparseBins({{2, binBytes('G')}, {4, binBytes('H')}});

    checkpointer.createCheckpoint(initial);
    checkpointer.createCheckpoint(initial);

    auto disabled = checkpointer.createDisabledCheckpoint();
    expectMinifiedAction(disabled, Action::DISABLED);
    expectSparseFullMap(disabled, initial);

    auto reenabled = checkpointer.createReenabledCheckpoint();
    EXPECT_TRUE(reenabled->isSnapshot());
    expectMinifiedAction(reenabled, Action::FULL);
    expectSparseFullMap(reenabled, initial);
}

void testMissedFlushHelpers()
{
    SparseCheckpointer checkpointer(kCid, kLargeHeartbeat);
    const auto initial = sparseBins({{1, binBytes('A')}, {3, binBytes('B')}});

    checkpointer.createCheckpoint(initial);
    checkpointer.createCheckpoint(initial);
    EXPECT_FALSE(checkpointer.isDueForWireRefresh());
    EXPECT_EQUAL(checkpointer.getDistanceToSnapshot(), 1u);

    checkpointer.recordMissedFlush();
    EXPECT_EQUAL(checkpointer.getDistanceToSnapshot(), 2u);

    checkpointer.rebaseTipAfterWireFull(*checkpointer.tip()->getFullData());
    EXPECT_EQUAL(checkpointer.getDistanceToSnapshot(), 0u);
}

} // namespace

TEST_INIT;

int main()
{
    testFirstCollectionFull();
    testUnchangedCarry();
    testSwap();
    testRemove();
    testHeartbeatForcesFull();
    testDisableAndReenable();
    testMissedFlushHelpers();

    REPORT_ERROR;
    return ERROR_CODE;
}
