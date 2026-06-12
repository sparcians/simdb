// clang-format off

#include "SimDBTester.hpp"
#include "simdb/apps/argos/Checkpointer.hpp"

namespace {

using simdb::argos::Action;

constexpr auto kCidBytes = sizeof(uint16_t);
constexpr auto kActionBytes = sizeof(uint8_t);
constexpr size_t kHeartbeat = 3;
constexpr uint16_t kCid = 1;

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

void expectMinifiedAction(const std::shared_ptr<const simdb::argos::Checkpoint>& cp, Action action)
{
    auto data = cp->getMinifiedData();
    EXPECT_EQUAL(getActionByte(*data), static_cast<uint8_t>(action));
}

void expectMinifiedPayloadSize(const std::shared_ptr<const simdb::argos::Checkpoint>& cp, size_t payload_bytes)
{
    auto data = cp->getMinifiedData();
    EXPECT_EQUAL(getPayload(*data).size(), payload_bytes);
}

void expectFullPayload(const std::shared_ptr<const simdb::argos::Checkpoint>& cp, const std::vector<char>& payload)
{
    auto data = cp->getFullData();
    EXPECT_EQUAL(getActionByte(*data), static_cast<uint8_t>(Action::FULL));
    EXPECT_EQUAL(getPayload(*data), payload);
}

void testOnCollectionHeartbeat()
{
    const std::vector<char> payload{'G', 'H', 'I'};

    simdb::argos::ScalarCheckpointer checkpointer(kCid, kHeartbeat);

    auto full = checkpointer.createCheckpoint(payload);
    expectMinifiedAction(full, Action::FULL);
    expectFullPayload(full, payload);
    EXPECT_EQUAL(full->getDistanceToSnapshot(), 0u);

    auto carry = checkpointer.createCheckpoint(payload);
    expectMinifiedAction(carry, Action::CARRY);
    expectFullPayload(carry, payload);
    EXPECT_EQUAL(carry->getDistanceToSnapshot(), 1u);

    auto carry_again = checkpointer.createCheckpoint(payload);
    expectMinifiedAction(carry_again, Action::CARRY);
    expectFullPayload(carry_again, payload);
    EXPECT_EQUAL(carry_again->getDistanceToSnapshot(), 2u);

    auto heartbeat_full = checkpointer.createCheckpoint(payload);
    EXPECT_TRUE(heartbeat_full->isSnapshot());
    expectMinifiedAction(heartbeat_full, Action::FULL);
    expectFullPayload(heartbeat_full, payload);
    EXPECT_EQUAL(heartbeat_full->getDistanceToSnapshot(), 0u);
}

void testDisableAndReenable()
{
    const std::vector<char> payload{'G', 'H', 'I'};

    simdb::argos::ScalarCheckpointer checkpointer(kCid, kHeartbeat);

    checkpointer.createCheckpoint(payload);
    checkpointer.createCheckpoint(payload);

    auto disabled = checkpointer.createDisabledCheckpoint();
    EXPECT_FALSE(disabled->isSnapshot());
    expectMinifiedAction(disabled, Action::DISABLED);
    expectMinifiedPayloadSize(disabled, 0);
    expectFullPayload(disabled, payload);

    auto reenabled = checkpointer.createReenabledCheckpoint();
    EXPECT_TRUE(reenabled->isSnapshot());
    expectMinifiedAction(reenabled, Action::FULL);
    expectFullPayload(reenabled, payload);
    EXPECT_EQUAL(reenabled->getDistanceToSnapshot(), 0u);
}

void testQuietAndAwaken()
{
    const std::vector<char> payload{'A', 'B', 'C'};

    simdb::argos::ScalarCheckpointer checkpointer(1, kHeartbeat);
    checkpointer.createCheckpoint(payload);

    auto quieted = checkpointer.createQuietedCheckpoint();
    expectMinifiedAction(quieted, Action::QUIETED);
    expectMinifiedPayloadSize(quieted, 0);
    expectFullPayload(quieted, payload);

    auto awakened = checkpointer.createReenabledCheckpoint();
    EXPECT_TRUE(awakened->isSnapshot());
    expectMinifiedAction(awakened, Action::FULL);
    expectFullPayload(awakened, payload);
}

} // namespace

TEST_INIT;

int main()
{
    testOnCollectionHeartbeat();
    testDisableAndReenable();
    testQuietAndAwaken();

    REPORT_ERROR;
    return ERROR_CODE;
}
