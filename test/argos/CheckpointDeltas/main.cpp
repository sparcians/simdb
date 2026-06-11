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

    EXPECT_EQUAL(classifyScalarChange(empty, payload_a), ScalarDeltaKind::Changed);
    EXPECT_EQUAL(classifyScalarChange(payload_a, payload_a), ScalarDeltaKind::Unchanged);
    EXPECT_EQUAL(classifyScalarChange(payload_a, payload_b), ScalarDeltaKind::Changed);

    REPORT_ERROR;
    return ERROR_CODE;
}
