// clang-format off

#include "SimDBTester.hpp"
#include "simdb/apps/argos/CheckpointDeltas.hpp"

#include <map>

namespace {

using simdb::argos::ContigDeltaKind;
using simdb::argos::ScalarDeltaKind;
using simdb::argos::SparseDeltaKind;
using simdb::argos::classifyContigChange;
using simdb::argos::classifyScalarChange;
using simdb::argos::classifySparseChange;

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

std::map<uint16_t, std::vector<char>> sparseBins(
    std::initializer_list<std::pair<const uint16_t, std::vector<char>>> elems)
{
    return std::map<uint16_t, std::vector<char>>(elems);
}

void testSparseEmptyPrev()
{
    const auto result = classifySparseChange({}, sparseBins({{3, {'A'}}}));
    EXPECT_EQUAL(result.kind, SparseDeltaKind::FULL);
}

void testSparseCarry()
{
    const auto prev = sparseBins({{1, {'A'}}, {5, {'B'}}});
    const auto result = classifySparseChange(prev, prev);
    EXPECT_EQUAL(result.kind, SparseDeltaKind::CARRY);
}

void testSparseSwap()
{
    const auto prev = sparseBins({{1, {'A'}}, {5, {'B'}}});
    auto curr = sparseBins({{1, {'A'}}, {5, {'X'}}});
    const auto result = classifySparseChange(prev, curr);
    EXPECT_EQUAL(result.kind, SparseDeltaKind::SWAP);
    EXPECT_EQUAL(result.bin_index, 5u);
    EXPECT_EQUAL(result.payload, curr.at(5));
}

void testSparseRemove()
{
    const auto prev = sparseBins({{1, {'A'}}, {5, {'B'}}, {9, {'C'}}});
    const auto curr = sparseBins({{1, {'A'}}, {9, {'C'}}});
    const auto result = classifySparseChange(prev, curr);
    EXPECT_EQUAL(result.kind, SparseDeltaKind::REMOVE);
    EXPECT_EQUAL(result.bin_index, 5u);
}

void testSparseFullFallback()
{
    const auto prev = sparseBins({{1, {'A'}}, {5, {'B'}}});
    const auto curr = sparseBins({{1, {'X'}}, {5, {'Y'}}});
    const auto result = classifySparseChange(prev, curr);
    EXPECT_EQUAL(result.kind, SparseDeltaKind::FULL);
}

void testSparseSizeChange()
{
    const auto prev = sparseBins({{1, {'A'}}});
    const auto curr = sparseBins({{1, {'A'}}, {2, {'B'}}});
    const auto result = classifySparseChange(prev, curr);
    EXPECT_EQUAL(result.kind, SparseDeltaKind::FULL);
}

void testSparseBinRelocate()
{
    const std::vector<char> payload{'I', 'n', 's', 't'};
    const auto prev = sparseBins({{0, payload}});
    const auto curr = sparseBins({{1, payload}});
    const auto result = classifySparseChange(prev, curr);
    EXPECT_EQUAL(result.kind, SparseDeltaKind::FULL);
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
    testSparseEmptyPrev();
    testSparseCarry();
    testSparseSwap();
    testSparseRemove();
    testSparseFullFallback();
    testSparseSizeChange();
    testSparseBinRelocate();

    REPORT_ERROR;
    return ERROR_CODE;
}
