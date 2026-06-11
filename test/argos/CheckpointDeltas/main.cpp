// clang-format off

#include "SimDBTester.hpp"
#include "simdb/apps/argos/CheckpointDeltas.hpp"

TEST_INIT;

int main()
{
    using simdb::argos::ScalarDeltaKind;
    using simdb::argos::classifyScalarChange;

    const std::vector<char> payload_a{'A', 'B', 'C'};
    const std::vector<char> payload_b{'X', 'Y', 'Z'};
    const std::vector<char> empty;

    EXPECT_EQUAL(classifyScalarChange(empty, payload_a), ScalarDeltaKind::CHANGED);
    EXPECT_EQUAL(classifyScalarChange(payload_a, payload_a), ScalarDeltaKind::UNCHANGED);
    EXPECT_EQUAL(classifyScalarChange(payload_a, payload_b), ScalarDeltaKind::CHANGED);

    REPORT_ERROR;
    return ERROR_CODE;
}
