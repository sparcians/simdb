// clang-format off

#include "SimDBTester.hpp"
#include "simdb/apps/argos/Checkpointer.hpp"
#include "simdb/apps/argos/PipelineDataTypes.hpp"

namespace {

class MockCheckpoint : public simdb::argos::Checkpoint
{
public:
    MockCheckpoint(uint16_t cid, std::shared_ptr<const Checkpoint> parent, bool snapshot) :
        cid_(cid),
        parent_(std::move(parent)),
        snapshot_(snapshot)
    {
    }

    std::unique_ptr<simdb::argos::CollectedData> getMinifiedData() const override
    {
        auto data = std::make_unique<simdb::argos::CollectedData>(cid_);
        data->getBuffer().append(simdb::argos::Action::DISABLED);
        return data;
    }

    std::unique_ptr<simdb::argos::CollectedData> getFullData() const override
    {
        auto data = std::make_unique<simdb::argos::CollectedData>(cid_);
        data->getBuffer().append(simdb::argos::Action::FULL);
        data->getBuffer().append(static_cast<uint8_t>(0xAB));
        return data;
    }

    bool isSnapshot() const override { return snapshot_; }

    std::shared_ptr<const simdb::argos::Checkpoint> parent() const override { return parent_; }

private:
    uint16_t cid_;
    std::shared_ptr<const simdb::argos::Checkpoint> parent_;
    bool snapshot_;
};

} // namespace

TEST_INIT;

int main()
{
    auto root = std::make_shared<MockCheckpoint>(1, nullptr, true);
    auto delta = std::make_shared<MockCheckpoint>(1, root, false);

    EXPECT_TRUE(root->isSnapshot());
    EXPECT_FALSE(delta->isSnapshot());
    EXPECT_EQUAL(root->parent(), nullptr);
    EXPECT_EQUAL(delta->parent(), root);

    simdb::argos::QueueCollectionData entry;
    entry.sim_time = 100;
    entry.checkpoints.emplace(1, delta);
    EXPECT_EQUAL(entry.checkpoints.size(), 1u);
    EXPECT_EQUAL(entry.checkpoints.at(1), delta);

    REPORT_ERROR;
    return ERROR_CODE;
}
