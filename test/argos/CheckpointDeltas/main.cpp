// clang-format off

#include "SimDBTester.hpp"
#include "simdb/apps/argos/CheckpointDeltas.hpp"

namespace {

using simdb::argos::ContigDeltaKind;
using simdb::argos::ScalarDeltaKind;
using simdb::argos::classifyContigChange;
using simdb::argos::classifyScalarChange;

std::vector<std::vector<char>> bins(std::initializer_list<std::vector<char>> elems)
{
    return std::vector<std::vector<char>>(elems);
}

void testScalarUnchanged()
{
    const std::vector<char> prev{'A', 'B'};
    EXPECT_EQUAL(classifyScalarChange(prev, prev), ScalarDeltaKind::UNCHANGED);
}

void testScalarChanged()
{
    const std::vector<char> prev{'A', 'B'};
    const std::vector<char> curr{'A', 'C'};
    EXPECT_EQUAL(classifyScalarChange(prev, curr), ScalarDeltaKind::CHANGED);
}

void testScalarEmptyPrev()
{
    const std::vector<char> curr{'X'};
    EXPECT_EQUAL(classifyScalarChange({}, curr), ScalarDeltaKind::CHANGED);
}

void testContigEmptyPrev()
{
    const auto result = classifyContigChange({}, bins({{'A'}}));
    EXPECT_EQUAL(result.kind, ContigDeltaKind::FULL);
}

void testContigCarry()
{
    const auto prev = bins({{'A'}, {'B'}, {'C'}});
    const auto curr = bins({{'A'}, {'B'}, {'C'}});
    const auto result = classifyContigChange(prev, curr);
    EXPECT_EQUAL(result.kind, ContigDeltaKind::CARRY);
}

void testContigSwap()
{
    const auto prev = bins({{'A'}, {'B'}, {'C'}});
    auto curr = bins({{'A'}, {'X'}, {'C'}});
    const auto result = classifyContigChange(prev, curr);
    EXPECT_EQUAL(result.kind, ContigDeltaKind::SWAP);
    EXPECT_EQUAL(result.swap_index, 1u);
    EXPECT_EQUAL(result.payload, curr[1]);
}

void testContigBookends()
{
    const auto prev = bins({{'A'}, {'B'}, {'C'}, {'D'}});
    const auto curr = bins({{'B'}, {'C'}, {'D'}, {'E'}});
    const auto result = classifyContigChange(prev, curr);
    EXPECT_EQUAL(result.kind, ContigDeltaKind::BOOKENDS);
    EXPECT_EQUAL(result.payload, curr[3]);
}

void testContigArrive()
{
    const auto prev = bins({{'A'}, {'B'}});
    const auto curr = bins({{'A'}, {'B'}, {'C'}});
    const auto result = classifyContigChange(prev, curr);
    EXPECT_EQUAL(result.kind, ContigDeltaKind::ARRIVE);
    EXPECT_EQUAL(result.payload, curr[2]);
}

void testContigDepart()
{
    const auto prev = bins({{'A'}, {'B'}, {'C'}});
    const auto curr = bins({{'B'}, {'C'}});
    const auto result = classifyContigChange(prev, curr);
    EXPECT_EQUAL(result.kind, ContigDeltaKind::DEPART);
}

void testContigFullFallback()
{
    const auto prev = bins({{'A'}, {'B'}});
    const auto curr = bins({{'X'}, {'Y'}});
    const auto result = classifyContigChange(prev, curr);
    EXPECT_EQUAL(result.kind, ContigDeltaKind::FULL);
}

} // namespace

TEST_INIT;

int main()
{
    testScalarUnchanged();
    testScalarChanged();
    testScalarEmptyPrev();
    testContigEmptyPrev();
    testContigCarry();
    testContigSwap();
    testContigBookends();
    testContigArrive();
    testContigDepart();
    testContigFullFallback();

    REPORT_ERROR;
    return ERROR_CODE;
}
