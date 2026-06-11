// clang-format off

#include "SimDBTester.hpp"
#include "simdb/apps/argos/Checkpointer.hpp"

namespace {

using simdb::argos::Action;

constexpr auto kCidBytes = sizeof(uint16_t);
constexpr auto kActionBytes = sizeof(uint8_t);

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

void expectFullPayload(const std::shared_ptr<const simdb::argos::Checkpoint>& cp, const std::vector<char>& payload)
{
    auto data = cp->getFullData();
    EXPECT_EQUAL(getActionByte(*data), static_cast<uint8_t>(Action::FULL));
    EXPECT_EQUAL(getPayload(*data), payload);
}

} // namespace

TEST_INIT;

int main()
{
    const std::vector<char> payload_a{'A', 'B', 'C'};
    const std::vector<char> payload_b{'X', 'Y', 'Z'};

    const uint16_t cid = 1;
    simdb::argos::ScalarCheckpointer checkpointer(cid);

    auto first = checkpointer.createCheckpoint(payload_a);
    EXPECT_TRUE(first->isSnapshot());
    EXPECT_EQUAL(first->parent(), nullptr);
    expectMinifiedAction(first, Action::FULL);
    expectFullPayload(first, payload_a);
    EXPECT_EQUAL(checkpointer.getCyclesSinceLastFull(), 0u);

    auto carry = checkpointer.createCheckpoint(payload_a);
    EXPECT_FALSE(carry->isSnapshot());
    EXPECT_EQUAL(carry->parent(), first);
    expectMinifiedAction(carry, Action::CARRY);
    expectFullPayload(carry, payload_a);
    EXPECT_EQUAL(checkpointer.getCyclesSinceLastFull(), 1u);

    auto carry_again = checkpointer.createCheckpoint(payload_a);
    EXPECT_FALSE(carry_again->isSnapshot());
    expectMinifiedAction(carry_again, Action::CARRY);
    expectFullPayload(carry_again, payload_a);
    EXPECT_EQUAL(checkpointer.getCyclesSinceLastFull(), 2u);

    auto changed = checkpointer.createCheckpoint(payload_b);
    EXPECT_TRUE(changed->isSnapshot());
    EXPECT_EQUAL(changed->parent(), carry_again);
    expectMinifiedAction(changed, Action::FULL);
    expectFullPayload(changed, payload_b);
    EXPECT_EQUAL(checkpointer.getCyclesSinceLastFull(), 0u);

    REPORT_ERROR;
    return ERROR_CODE;
}
