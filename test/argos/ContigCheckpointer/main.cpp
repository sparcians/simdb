// clang-format off

#include "SimDBTester.hpp"
#include "simdb/apps/argos/Checkpointer.hpp"

#include <cstring>

namespace {

using simdb::argos::Action;
using simdb::argos::ContigCheckpointer;

constexpr auto kCidBytes = sizeof(uint16_t);
constexpr auto kActionBytes = sizeof(uint8_t);
constexpr uint16_t kCid = 42;
constexpr size_t kHeartbeat = 3;
constexpr size_t kLargeHeartbeat = 100;

std::vector<std::vector<char>> bins(std::initializer_list<std::vector<char>> elems)
{
    return std::vector<std::vector<char>>(elems);
}

std::vector<char> instBytes(char tag)
{
    return {'I', 'n', 's', 't', tag};
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

void expectContigFullBins(const std::shared_ptr<const simdb::argos::Checkpoint>& cp,
                          const std::vector<std::vector<char>>& expected_bins)
{
    auto data = cp->getFullData();
    EXPECT_EQUAL(getActionByte(*data), static_cast<uint8_t>(Action::FULL));
    const auto payload = getPayload(*data);
    EXPECT_EQUAL(readUint16(payload, 0), simdb::argos::countContigElements(expected_bins));
    size_t offset = sizeof(uint16_t);
    const auto count = simdb::argos::countContigElements(expected_bins);
    for (uint16_t i = 0; i < count; ++i)
    {
        const auto& expected = expected_bins[i];
        EXPECT_TRUE(payload.size() >= offset + expected.size());
        EXPECT_EQUAL(std::vector<char>(payload.begin() + static_cast<std::ptrdiff_t>(offset),
                                       payload.begin() + static_cast<std::ptrdiff_t>(offset + expected.size())),
                     expected);
        offset += expected.size();
    }
}

void testFirstCollectionFull()
{
    ContigCheckpointer checkpointer(kCid, kLargeHeartbeat);
    const auto initial = bins({instBytes('0'), instBytes('1'), instBytes('2')});

    auto first = checkpointer.createCheckpoint(initial);
    EXPECT_TRUE(first->isSnapshot());
    EXPECT_EQUAL(first->parent(), nullptr);
    expectMinifiedAction(first, Action::FULL);
    expectContigFullBins(first, initial);
    EXPECT_EQUAL(first->getDistanceToSnapshot(), 0u);
}

void testUnchangedCarry()
{
    ContigCheckpointer checkpointer(kCid, kLargeHeartbeat);
    const auto initial = bins({instBytes('0'), instBytes('1')});

    checkpointer.createCheckpoint(initial);
    auto carry = checkpointer.createCheckpoint(initial);
    EXPECT_FALSE(carry->isSnapshot());
    expectMinifiedAction(carry, Action::CARRY);
    expectContigFullBins(carry, initial);
    EXPECT_EQUAL(carry->getDistanceToSnapshot(), 1u);
}

void testArrive()
{
    ContigCheckpointer checkpointer(kCid, kLargeHeartbeat);
    std::vector<std::vector<char>> initial;
    for (char tag = '0'; tag <= '9'; ++tag)
    {
        initial.push_back(instBytes(tag));
    }
    initial.push_back(instBytes('A'));
    initial.push_back(instBytes('B'));

    checkpointer.createCheckpoint(initial);

    auto with_arrival = initial;
    with_arrival.push_back(instBytes('C'));
    auto arrive = checkpointer.createCheckpoint(with_arrival);
    expectMinifiedAction(arrive, Action::CONTIG_CONTAINER_ARRIVE);
    auto data = arrive->getMinifiedData();
    EXPECT_EQUAL(getPayload(*data), instBytes('C'));
    expectContigFullBins(arrive, with_arrival);
}

void testSwap()
{
    ContigCheckpointer checkpointer(kCid, kLargeHeartbeat);
    const auto initial = bins({instBytes('A'), instBytes('B'), instBytes('C')});
    checkpointer.createCheckpoint(initial);

    auto swapped = bins({instBytes('A'), instBytes('X'), instBytes('C')});
    auto swap = checkpointer.createCheckpoint(swapped);
    expectMinifiedAction(swap, Action::CONTIG_CONTAINER_SWAP);
    auto data = swap->getMinifiedData();
    const auto payload = getPayload(*data);
    EXPECT_EQUAL(readUint16(payload, 0), 1u);
    EXPECT_EQUAL(std::vector<char>(payload.begin() + sizeof(uint16_t), payload.end()), instBytes('X'));
    expectContigFullBins(swap, swapped);
}

void testDepart()
{
    ContigCheckpointer checkpointer(kCid, kLargeHeartbeat);
    const auto initial = bins({instBytes('A'), instBytes('B'), instBytes('C')});
    checkpointer.createCheckpoint(initial);

    const auto departed = bins({instBytes('B'), instBytes('C')});
    auto depart = checkpointer.createCheckpoint(departed);
    expectMinifiedAction(depart, Action::CONTIG_CONTAINER_DEPART);
    EXPECT_EQUAL(getPayload(*depart->getMinifiedData()).size(), 0u);
    expectContigFullBins(depart, departed);
}

void testBookends()
{
    ContigCheckpointer checkpointer(kCid, kLargeHeartbeat);
    const auto initial = bins({instBytes('A'), instBytes('B'), instBytes('C'), instBytes('D')});
    checkpointer.createCheckpoint(initial);

    const auto shifted = bins({instBytes('B'), instBytes('C'), instBytes('D'), instBytes('E')});
    auto bookends = checkpointer.createCheckpoint(shifted);
    expectMinifiedAction(bookends, Action::CONTIG_CONTAINER_BOOKENDS);
    EXPECT_EQUAL(getPayload(*bookends->getMinifiedData()), instBytes('E'));
    expectContigFullBins(bookends, shifted);
}

void testHeartbeatForcesFull()
{
    ContigCheckpointer checkpointer(kCid, kHeartbeat);
    const auto initial = bins({instBytes('A'), instBytes('B'), instBytes('C'), instBytes('D')});
    const auto shifted = bins({instBytes('B'), instBytes('C'), instBytes('D'), instBytes('E')});

    checkpointer.createCheckpoint(initial);
    checkpointer.createCheckpoint(initial);
    checkpointer.createCheckpoint(initial);

    auto heartbeat_full = checkpointer.createCheckpoint(shifted);
    EXPECT_TRUE(heartbeat_full->isSnapshot());
    expectMinifiedAction(heartbeat_full, Action::FULL);
    expectContigFullBins(heartbeat_full, shifted);
    EXPECT_EQUAL(heartbeat_full->getDistanceToSnapshot(), 0u);
}

void testDisableAndReenable()
{
    ContigCheckpointer checkpointer(kCid, kHeartbeat);
    const auto initial = bins({instBytes('G'), instBytes('H'), instBytes('I')});

    checkpointer.createCheckpoint(initial);
    checkpointer.createCheckpoint(initial);

    auto disabled = checkpointer.createDisabledCheckpoint();
    expectMinifiedAction(disabled, Action::DISABLED);
    expectContigFullBins(disabled, initial);

    auto reenabled = checkpointer.createReenabledCheckpoint();
    EXPECT_TRUE(reenabled->isSnapshot());
    expectMinifiedAction(reenabled, Action::FULL);
    expectContigFullBins(reenabled, initial);
}

void testMissedFlushHelpers()
{
    ContigCheckpointer checkpointer(kCid, kLargeHeartbeat);
    const auto initial = bins({instBytes('A'), instBytes('B')});

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
    testArrive();
    testSwap();
    testDepart();
    testBookends();
    testHeartbeatForcesFull();
    testDisableAndReenable();
    testMissedFlushHelpers();

    REPORT_ERROR;
    return ERROR_CODE;
}
