#include "SimDBTester.hpp"
#include "simdb/utils/Tree.hpp"
#include "simdb/utils/TypeTraits.hpp"

TEST_INIT;

struct IntermediateNode : simdb::Tree::TreeNode
{
    using TreeNode::TreeNode;
};

struct LeafNode : simdb::Tree::TreeNode
{
    using TreeNode::TreeNode;
};

struct OtherLeafNode : simdb::Tree::TreeNode
{
    using TreeNode::TreeNode;
};

void testCreateNodeAndPaths()
{
    simdb::Tree tree;

    auto leaf = tree.createNode<LeafNode, IntermediateNode>("top.mid.leaf");
    EXPECT_NOTEQUAL(leaf, nullptr);
    EXPECT_EQUAL(leaf->getName(), std::string("leaf"));
    EXPECT_EQUAL(leaf->getPath(), std::string("top.mid.leaf"));

    auto top = tree.getRoot()->getChildAs<IntermediateNode>("top");
    EXPECT_NOTEQUAL(top, nullptr);
    EXPECT_EQUAL(top->getPath(), std::string("top"));

    auto mid = top->getChildAs<IntermediateNode>("mid");
    EXPECT_NOTEQUAL(mid, nullptr);
    EXPECT_EQUAL(mid->getPath(), std::string("top.mid"));
}

void testGetChildMustExistBehavior()
{
    simdb::Tree tree;
    auto root = tree.getRoot();

    EXPECT_EQUAL(root->getChild("missing", false), nullptr);
    EXPECT_THROW(([&]() { root->getChild("missing", true); })());

    const auto croot = static_cast<const simdb::Tree::TreeNode*>(root);
    EXPECT_EQUAL(croot->getChild("missing", false), nullptr);
    EXPECT_THROW(([&]() { croot->getChild("missing", true); })());
}

void testGetChildAsBehavior()
{
    simdb::Tree tree;
    auto leaf = tree.createNode<LeafNode, IntermediateNode>("top.mid.leaf");
    EXPECT_NOTEQUAL(leaf, nullptr);

    auto mid = tree.getRoot()->getChildAs<IntermediateNode>("top")->getChildAs<IntermediateNode>("mid");
    EXPECT_NOTEQUAL(mid, nullptr);

    EXPECT_EQUAL(mid->getChildAs<LeafNode>("leaf", false), leaf);
    EXPECT_THROW(([&]() { mid->getChildAs<LeafNode>("missing", true); })());
    EXPECT_EQUAL(mid->getChildAs<LeafNode>("missing", false), nullptr);

    auto other = tree.createNode<OtherLeafNode, IntermediateNode>("top.mid.other");
    EXPECT_NOTEQUAL(other, nullptr);
    EXPECT_EQUAL(mid->getChildAs<LeafNode>("other", false), nullptr);
    EXPECT_THROW(([&]() { mid->getChildAs<LeafNode>("other", true); })());

    const auto cmid = static_cast<const simdb::Tree::TreeNode*>(mid);
    EXPECT_EQUAL(cmid->getChildAs<LeafNode>("leaf", false), leaf);
    EXPECT_THROW(([&]() { cmid->getChildAs<LeafNode>("missing", true); })());
    EXPECT_EQUAL(cmid->getChildAs<LeafNode>("missing", false), nullptr);
    EXPECT_EQUAL(cmid->getChildAs<LeafNode>("other", false), nullptr);
    EXPECT_THROW(([&]() { cmid->getChildAs<LeafNode>("other", true); })());
}

void testCreateNodeTypeMismatchThrows()
{
    simdb::Tree tree;
    auto leaf = tree.createNode<LeafNode, IntermediateNode>("top.mid.leaf");
    EXPECT_NOTEQUAL(leaf, nullptr);

    EXPECT_THROW(([&]() { tree.createNode<OtherLeafNode, IntermediateNode>("top.mid.leaf"); })());
    EXPECT_THROW(([&]() { tree.createNode<LeafNode, LeafNode>("top.mid.another_leaf"); })());
    EXPECT_THROW(([&]() { tree.createNode<LeafNode, IntermediateNode>("top..leaf"); })());
}

void testCreateNodes()
{
    simdb::Tree tree;

    auto leaf = tree.createNodes<IntermediateNode>("top.mid.leaf");
    EXPECT_NOTEQUAL(leaf, nullptr);
    EXPECT_EQUAL(leaf->getPath(), std::string("top.mid.leaf"));

    auto top = tree.getRoot()->getChildAs<IntermediateNode>("top");
    EXPECT_NOTEQUAL(top, nullptr);

    auto mid = top->getChildAs<IntermediateNode>("mid");
    EXPECT_NOTEQUAL(mid, nullptr);
    EXPECT_EQUAL(mid->getPath(), std::string("top.mid"));

    auto same_leaf = tree.createNodes<IntermediateNode>("top.mid.leaf");
    EXPECT_EQUAL(same_leaf, leaf);
}

void testGetNodeApis()
{
    simdb::Tree tree;
    auto leaf = tree.createNode<LeafNode, IntermediateNode>("top.mid.leaf");
    EXPECT_NOTEQUAL(leaf, nullptr);

    // tryGetNodeAs(): positive and negative cases
    EXPECT_EQUAL(tree.tryGetNodeAs<LeafNode>("top.mid.leaf", true), leaf);
    EXPECT_EQUAL(tree.tryGetNodeAs<LeafNode>("top.mid.missing", false), nullptr);
    EXPECT_THROW(([&]() { tree.tryGetNodeAs<LeafNode>("top.mid.missing", true); })());
    EXPECT_EQUAL(tree.tryGetNodeAs<OtherLeafNode>("top.mid.leaf", false), nullptr);
    EXPECT_THROW(([&]() { tree.tryGetNodeAs<OtherLeafNode>("top.mid.leaf", true); })());
    EXPECT_THROW(([&]() { tree.tryGetNodeAs<LeafNode>("top..leaf", false); })());

    // getNode(): positive and negative cases
    auto& leaf_ref = tree.getNode<LeafNode>("top.mid.leaf");
    EXPECT_EQUAL(&leaf_ref, leaf);
    EXPECT_THROW(([&]() { tree.getNode<LeafNode>("top.mid.missing"); })());
    EXPECT_THROW(([&]() { tree.getNode<OtherLeafNode>("top.mid.leaf"); })());
    EXPECT_THROW(([&]() { tree.getNode<LeafNode>("top..leaf"); })());

    // const overloads
    const auto& ctree = static_cast<const simdb::Tree&>(tree);
    EXPECT_EQUAL(ctree.tryGetNodeAs<LeafNode>("top.mid.leaf", true), leaf);
    EXPECT_EQUAL(ctree.tryGetNodeAs<LeafNode>("top.mid.missing", false), nullptr);
    EXPECT_THROW(([&]() { ctree.tryGetNodeAs<LeafNode>("top.mid.missing", true); })());
    EXPECT_EQUAL(ctree.tryGetNodeAs<OtherLeafNode>("top.mid.leaf", false), nullptr);
    EXPECT_THROW(([&]() { ctree.tryGetNodeAs<OtherLeafNode>("top.mid.leaf", true); })());
    EXPECT_THROW(([&]() { ctree.tryGetNodeAs<LeafNode>("top..leaf", false); })());

    const auto& cleaf_ref = ctree.getNode<LeafNode>("top.mid.leaf");
    EXPECT_EQUAL(&cleaf_ref, leaf);
    EXPECT_THROW(([&]() { ctree.getNode<LeafNode>("top.mid.missing"); })());
    EXPECT_THROW(([&]() { ctree.getNode<OtherLeafNode>("top.mid.leaf"); })());
    EXPECT_THROW(([&]() { ctree.getNode<LeafNode>("top..leaf"); })());
}

int main()
{
    testCreateNodeAndPaths();
    testGetChildMustExistBehavior();
    testGetChildAsBehavior();
    testCreateNodeTypeMismatchThrows();
    testCreateNodes();
    testGetNodeApis();

    REPORT_ERROR;
    return ERROR_CODE;
}
