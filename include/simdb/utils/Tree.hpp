// <Tree.hpp> -*- C++ -*-

#pragma once

#include "simdb/Exceptions.hpp"

#include <boost/algorithm/string.hpp>

#include <functional>
#include <memory>
#include <ostream>
#include <queue>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace simdb {

/// \class Tree
/// \brief Generic tree utility supporting typed node creation by dot-delimited path.
class Tree
{
public:
    /// \class TreeNode
    /// \brief Base node type used by Tree and by user-defined node subclasses.
    class TreeNode
    {
    public:
        /// \brief Virtual destructor for polymorphic node hierarchies.
        virtual ~TreeNode() = default;

        /// \brief Construct a node with no parent (typically root).
        /// \param name Node name.
        explicit TreeNode(const std::string& name) :
            TreeNode(name, nullptr)
        {
        }

        /// \brief Construct a node with a parent.
        /// \param name Node name.
        /// \param parent Parent node pointer.
        TreeNode(const std::string& name, TreeNode* parent) :
            name_(name),
            parent_(parent)
        {
            validateName_(name_);
        }

        /// \brief Get this node's local name.
        /// \return Node name.
        const std::string& getName() const { return name_; }

        /// \brief Find a child by name.
        /// \param child_name Child node name.
        /// \param must_exist If true, throw when child is not found.
        /// \return Child pointer, or nullptr if not found.
        /// \throw DBException If \a must_exist is true and child is not found.
        TreeNode* getChild(const std::string& child_name, bool must_exist = true)
        {
            for (auto& child : children_)
            {
                if (child->name_ == child_name)
                {
                    return child.get();
                }
            }
            if (must_exist)
            {
                throw DBException("Child node does not exist: ")
                    << child_name << " under parent path '" << getPath() << "'";
            }
            return nullptr;
        }

        /// \brief Find a child by name (const overload).
        /// \param child_name Child node name.
        /// \param must_exist If true, throw when child is not found.
        /// \return Child pointer, or nullptr if not found.
        /// \throw DBException If \a must_exist is true and child is not found.
        const TreeNode* getChild(const std::string& child_name, bool must_exist = true) const
        {
            for (const auto& child : children_)
            {
                if (child->name_ == child_name)
                {
                    return child.get();
                }
            }
            if (must_exist)
            {
                throw DBException("Child node does not exist: ")
                    << child_name << " under parent path '" << getPath() << "'";
            }
            return nullptr;
        }

        /// \brief Find a child by name and cast to a specific node type.
        /// \tparam NodeT Desired child type derived from TreeNode.
        /// \param child_name Child node name.
        /// \param must_exist If true, throw when child is missing or has incompatible type.
        /// \return Typed child pointer, or nullptr if the child does not exist or has a different type.
        /// \throw DBException If \a must_exist is true and the child is missing or has incompatible type.
        template <typename NodeT> NodeT* getChildAs(const std::string& child_name, bool must_exist = true)
        {
            static_assert(std::is_base_of_v<TreeNode, NodeT>, "NodeT must derive from Tree::TreeNode");
            auto child = getChild(child_name, must_exist);
            if (!child && must_exist)
            {
                throw DBException("Child node '") << child_name << "' does not exist under path '" << getPath() << "'";
            }

            auto typed_child = dynamic_cast<NodeT*>(child);
            if (!typed_child && must_exist)
            {
                throw DBException("Child node exists but has incompatible type: ")
                    << child_name << " under parent path '" << getPath() << "'";
            }
            return typed_child;
        }

        /// \brief Find a child by name and cast to a specific node type (const overload).
        /// \tparam NodeT Desired child type derived from TreeNode.
        /// \param child_name Child node name.
        /// \param must_exist If true, throw when child is missing or has incompatible type.
        /// \return Typed child pointer, or nullptr if the child does not exist or has a different type.
        /// \throw DBException If \a must_exist is true and the child is missing or has incompatible type.
        template <typename NodeT> const NodeT* getChildAs(const std::string& child_name, bool must_exist = true) const
        {
            static_assert(std::is_base_of_v<TreeNode, NodeT>, "NodeT must derive from Tree::TreeNode");
            auto child = getChild(child_name, must_exist);
            if (!child && must_exist)
            {
                throw DBException("Child node '") << child_name << "' does not exist under path '" << getPath() << "'";
            }

            auto typed_child = dynamic_cast<const NodeT*>(child);
            if (!typed_child && must_exist)
            {
                throw DBException("Child node exists but has incompatible type: ")
                    << child_name << " under parent path '" << getPath() << "'";
            }
            return typed_child;
        }

        /// \brief Add a typed child by name, or no-op if an equivalent child already exists.
        /// \tparam NodeT Child node type derived from TreeNode.
        /// \param name Child name; must not contain '.'.
        /// \throw DBException If \a name contains '.', if a sibling with the same name exists
        /// but is not a \a NodeT, or if a new child cannot be added.
        template <typename NodeT = TreeNode> NodeT* addChild(const std::string& name)
        {
            static_assert(std::is_base_of_v<TreeNode, NodeT>, "NodeT must derive from Tree::TreeNode");
            validateName_(name);
            auto existing = getChild(name, false);
            if (existing)
            {
                auto typed_existing = dynamic_cast<NodeT*>(existing);
                if (!typed_existing)
                {
                    throw DBException("Child node exists but has incompatible type: ")
                        << name << " under parent path '" << getPath() << "'";
                }
                return typed_existing;
            }
            return createChild_<NodeT>(name, this);
        }

        /// \brief Get this node's children.
        /// \return Mutable vector of owned child nodes.
        std::vector<std::unique_ptr<TreeNode>>& getChildren() { return children_; }

        /// \brief Get this node's children (const overload).
        /// \return Const vector of owned child nodes.
        const std::vector<std::unique_ptr<TreeNode>>& getChildren() const { return children_; }

        /// \brief Get this node's parent.
        /// \return Parent node pointer, or nullptr for root.
        TreeNode* getParent() { return parent_; }

        /// \brief Get this node's parent (const overload).
        /// \return Parent node pointer, or nullptr for root.
        const TreeNode* getParent() const { return parent_; }

        /// \brief Get the parent cast to a specfic node type
        /// \tparam NodeT Desired parent type derived from TreeNode.
        /// \param must_exist If true, throw when parent is missing or has incompatible type.
        /// \return Typed parent pointer, or nullptr if the parent does not exist or has a different type.
        /// \throw DBException If \a must_exist is true and the parent is missing or has incompatible type.
        template <typename NodeT> NodeT* getParentAs(bool must_exist = true)
        {
            if (!parent_ && must_exist)
            {
                throw DBException("Node does not have a parent: ") << getPath();
            }

            auto typed_parent = dynamic_cast<NodeT*>(parent_);
            if (!typed_parent && must_exist)
            {
                throw DBException("Parent exists but has incompatible type. Occurred at path: ") << getPath();
            }

            return typed_parent;
        }

        /// \brief Get the parent cast to a specfic node type (const version)
        /// \tparam NodeT Desired parent type derived from TreeNode.
        /// \param must_exist If true, throw when parent is missing or has incompatible type.
        /// \return Typed parent pointer, or nullptr if the parent does not exist or has a different type.
        /// \throw DBException If \a must_exist is true and the parent is missing or has incompatible type.
        template <typename NodeT> const NodeT* getParentAs(bool must_exist = true) const
        {
            if (!parent_ && must_exist)
            {
                throw DBException("Node does not have a parent: ") << getPath();
            }

            const auto typed_parent = dynamic_cast<const NodeT*>(parent_);
            if (!typed_parent && must_exist)
            {
                throw DBException("Parent exists but has incompatible type. Occurred at path: ") << getPath();
            }

            return typed_parent;
        }

        /// \brief Build this node's dot-delimited path from root.
        /// \details Root node path is an empty string. Non-root paths exclude the root name.
        /// \return Dot-delimited path.
        const std::string& getPath() const
        {
            if (!cached_path_.empty())
            {
                return cached_path_;
            }

            std::vector<std::string> node_names;
            auto node = this;
            while (node != nullptr && node->parent_ != nullptr)
            {
                node_names.push_back(node->name_);
                node = node->parent_;
            }

            std::string path;
            for (auto it = node_names.rbegin(); it != node_names.rend(); ++it)
            {
                if (!path.empty())
                {
                    path += ".";
                }
                path += *it;
            }

            cached_path_ = std::move(path);
            return cached_path_;
        }

    private:
        TreeNode() = default;

        template <typename NodeT, typename... Args> NodeT* createChild_(Args&&... args)
        {
            static_assert(std::is_base_of_v<TreeNode, NodeT>, "NodeT must derive from Tree::TreeNode");
            auto child = std::make_unique<NodeT>(std::forward<Args>(args)...);
            if (getChild(child->getName(), false) != nullptr)
            {
                throw DBException("Cannot add duplicate sibling tree node name: ")
                    << child->getName() << " under parent path '" << getPath() << "'";
            }
            auto raw_ptr = child.get();
            children_.emplace_back(std::move(child));
            return raw_ptr;
        }

        static void validateName_(const std::string& name)
        {
            if (name.find('.') != std::string::npos)
            {
                throw DBException("Tree node name cannot contain '.': ") << name;
            }
        }

        std::string name_;
        std::vector<std::unique_ptr<TreeNode>> children_;
        TreeNode* parent_ = nullptr;
        mutable std::string cached_path_;

        friend class Tree;
    };

    /// \brief Construct a tree with a default TreeNode root named "root".
    Tree() :
        root_(std::make_unique<TreeNode>("root"))
    {
    }

    /// \brief Construct a tree from an already-created root node.
    /// \tparam RootT Root node type derived from TreeNode.
    /// \param root Owned root node; must not be null.
    template <typename RootT>
    explicit Tree(std::unique_ptr<RootT> root) :
        root_(adoptRootNode_(std::move(root)))
    {
    }

    virtual ~Tree() = default;

    /// \brief Get the tree root node.
    /// \return Mutable root node pointer.
    TreeNode* getRoot() { return root_.get(); }

    /// \brief Get the tree root node (const overload).
    /// \return Const root node pointer.
    const TreeNode* getRoot() const { return root_.get(); }

    /// \brief Get the tree root node cast to a specific type.
    /// \tparam RootT Desired root type derived from TreeNode.
    /// \param must_exist If true, throw when root has incompatible type.
    /// \return Typed root pointer, or nullptr on incompatible type when must_exist is false.
    template <typename RootT> RootT* getRootAs(bool must_exist = true)
    {
        static_assert(std::is_base_of_v<TreeNode, RootT>, "RootT must derive from Tree::TreeNode");
        auto typed_root = dynamic_cast<RootT*>(getRoot());
        if (!typed_root && must_exist)
        {
            throw DBException("Tree root exists but has incompatible type");
        }
        return typed_root;
    }

    /// \brief Get the tree root node cast to a specific type (const overload).
    /// \tparam RootT Desired root type derived from TreeNode.
    /// \param must_exist If true, throw when root has incompatible type.
    /// \return Typed root pointer, or nullptr on incompatible type when must_exist is false.
    template <typename RootT> const RootT* getRootAs(bool must_exist = true) const
    {
        static_assert(std::is_base_of_v<TreeNode, RootT>, "RootT must derive from Tree::TreeNode");
        auto typed_root = dynamic_cast<const RootT*>(getRoot());
        if (!typed_root && must_exist)
        {
            throw DBException("Tree root exists but has incompatible type");
        }
        return typed_root;
    }

    /// \brief Traverse the tree in breadth-first order.
    /// \param callback Called for each visited node; return false to stop traversal.
    void bfs(std::function<bool(TreeNode*)> callback)
    {
        std::queue<TreeNode*> queue;
        queue.push(root_.get());

        while (!queue.empty())
        {
            auto node = queue.front();
            queue.pop();

            if (!callback(node))
            {
                return;
            }

            for (auto& child : node->getChildren())
            {
                queue.push(child.get());
            }
        }
    }

    /// \brief Traverse the tree in breadth-first order (const overload).
    /// \param callback Called for each visited node; return false to stop traversal.
    void bfs(std::function<bool(const TreeNode*)> callback) const
    {
        std::queue<const TreeNode*> queue;
        queue.push(root_.get());

        while (!queue.empty())
        {
            auto node = queue.front();
            queue.pop();

            if (!callback(node))
            {
                return;
            }

            for (const auto& child : node->getChildren())
            {
                queue.push(child.get());
            }
        }
    }

    /// \brief Traverse nodes of a specific type in breadth-first order.
    /// \tparam NodeT Desired node type derived from TreeNode.
    /// \param callback Called for each visited typed node; return false to stop traversal.
    template <typename NodeT> void bfsTypedNodes(std::function<bool(NodeT*)> callback)
    {
        static_assert(std::is_base_of_v<TreeNode, NodeT>, "NodeT must derive from Tree::TreeNode");
        bfs([&callback](TreeNode* node) {
            auto typed_node = dynamic_cast<NodeT*>(node);
            if (!typed_node)
            {
                return true;
            }
            return callback(typed_node);
        });
    }

    /// \brief Traverse nodes of a specific type in breadth-first order (const overload).
    /// \tparam NodeT Desired node type derived from TreeNode.
    /// \param callback Called for each visited typed node; return false to stop traversal.
    template <typename NodeT> void bfsTypedNodes(std::function<bool(const NodeT*)> callback) const
    {
        static_assert(std::is_base_of_v<TreeNode, NodeT>, "NodeT must derive from Tree::TreeNode");
        bfs([&callback](const TreeNode* node) {
            auto typed_node = dynamic_cast<const NodeT*>(node);
            if (!typed_node)
            {
                return true;
            }
            return callback(typed_node);
        });
    }

    /// \brief Traverse the tree in depth-first pre-order.
    /// \param callback Called for each visited node; return false to stop traversal.
    void dfs(std::function<bool(TreeNode*)> callback) { dfsImpl_(root_.get(), callback); }

    /// \brief Traverse the tree in depth-first pre-order (const overload).
    /// \param callback Called for each visited node; return false to stop traversal.
    void dfs(std::function<bool(const TreeNode*)> callback) const { dfsImpl_(root_.get(), callback); }

    /// \brief Traverse nodes of a specific type in depth-first pre-order.
    /// \tparam NodeT Desired node type derived from TreeNode.
    /// \param callback Called for each visited typed node; return false to stop traversal.
    template <typename NodeT> void dfsTypedNodes(std::function<bool(NodeT*)> callback)
    {
        static_assert(std::is_base_of_v<TreeNode, NodeT>, "NodeT must derive from Tree::TreeNode");
        dfs([&callback](TreeNode* node) {
            auto typed_node = dynamic_cast<NodeT*>(node);
            if (!typed_node)
            {
                return true;
            }
            return callback(typed_node);
        });
    }

    /// \brief Traverse nodes of a specific type in depth-first pre-order (const overload).
    /// \tparam NodeT Desired node type derived from TreeNode.
    /// \param callback Called for each visited typed node; return false to stop traversal.
    template <typename NodeT> void dfsTypedNodes(std::function<bool(const NodeT*)> callback) const
    {
        static_assert(std::is_base_of_v<TreeNode, NodeT>, "NodeT must derive from Tree::TreeNode");
        dfs([&callback](const TreeNode* node) {
            auto typed_node = dynamic_cast<const NodeT*>(node);
            if (!typed_node)
            {
                return true;
            }
            return callback(typed_node);
        });
    }

    /// \brief Get or create a leaf node for a dot-delimited path.
    /// \tparam LeafT Node type for the final path segment.
    /// \tparam IntermediateT Node type for all non-leaf segments.
    /// \param path Dot-delimited path, e.g. "top.mid.leaf".
    /// \return Pointer to the leaf node at \a path.
    /// \throw DBException If path is empty, contains empty segments, or if an existing node
    /// has an incompatible runtime type for LeafT/IntermediateT.
    template <typename LeafT = TreeNode, typename IntermediateT = TreeNode> LeafT* createNode(const std::string& path)
    {
        static_assert(std::is_base_of_v<TreeNode, LeafT>, "LeafT must derive from Tree::TreeNode");
        static_assert(std::is_base_of_v<TreeNode, IntermediateT>, "IntermediateT must derive from Tree::TreeNode");

        std::vector<std::string> path_parts;
        boost::split(path_parts, path, boost::is_any_of("."));
        if (path_parts.empty())
        {
            throw DBException("Cannot create tree node from an empty path");
        }
        for (const auto& token : path_parts)
        {
            if (token.empty())
            {
                throw DBException("Tree path contains an empty segment: ") << path;
            }
        }

        TreeNode* current = root_.get();
        for (size_t idx = 0; idx < path_parts.size(); ++idx)
        {
            const auto& token = path_parts[idx];
            const bool is_leaf = (idx == path_parts.size() - 1);

            auto existing_child = current->getChild(token, false);
            if (existing_child)
            {
                if (is_leaf)
                {
                    auto typed_leaf = dynamic_cast<LeafT*>(existing_child);
                    if (!typed_leaf)
                    {
                        throw DBException("Tree node already exists at path but has incompatible type: ") << path;
                    }
                    current = typed_leaf;
                } else
                {
                    auto typed_intermediate = dynamic_cast<IntermediateT*>(existing_child);
                    if (!typed_intermediate)
                    {
                        throw DBException("Intermediate tree node already exists at path but has incompatible type: ")
                            << path;
                    }
                    current = typed_intermediate;
                }
                continue;
            }

            current = is_leaf ? static_cast<TreeNode*>(current->createChild_<LeafT>(token, current))
                              : static_cast<TreeNode*>(current->createChild_<IntermediateT>(token, current));
        }

        return dynamic_cast<LeafT*>(current);
    }

    /// \brief Get or create nodes for a dot-delimited path using one node type.
    /// \tparam NodeT Node type used for all path segments.
    /// \param path Dot-delimited path, e.g. "top.mid.leaf".
    /// \return Pointer to the leaf node at \a path.
    template <typename NodeT = TreeNode> NodeT* createNodes(const std::string& path)
    {
        return createNode<NodeT, NodeT>(path);
    }

    /// \brief Get a node at a dot-delimited path and return it by reference.
    /// \tparam NodeT Expected runtime node type at \a path.
    /// \param path Dot-delimited path, e.g. "top.mid.leaf".
    /// \return Reference to the typed node.
    /// \throw DBException If path is invalid, missing, or has incompatible runtime type.
    template <typename NodeT> NodeT& getNode(const std::string& path)
    {
        auto node = tryGetNodeAs<NodeT>(path, true /*must_exist*/);
        return *node;
    }

    /// \brief Get a node at a dot-delimited path and return it by reference (const overload).
    /// \tparam NodeT Expected runtime node type at \a path.
    /// \param path Dot-delimited path, e.g. "top.mid.leaf".
    /// \return Const reference to the typed node.
    /// \throw DBException If path is invalid, missing, or has incompatible runtime type.
    template <typename NodeT> const NodeT& getNode(const std::string& path) const
    {
        auto node = tryGetNodeAs<NodeT>(path, true /*must_exist*/);
        return *node;
    }

    /// \brief Try to get a typed node at a dot-delimited path.
    /// \tparam NodeT Expected runtime node type at \a path.
    /// \param path Dot-delimited path, e.g. "top.mid.leaf".
    /// \param must_exist If true, throw when missing or type-incompatible.
    /// \return Typed node pointer, or nullptr when not found/incompatible and \a must_exist is false.
    /// \throw DBException If path is invalid, or if \a must_exist is true and node is missing/incompatible.
    template <typename NodeT> NodeT* tryGetNodeAs(const std::string& path, bool must_exist = true)
    {
        static_assert(std::is_base_of_v<TreeNode, NodeT>, "NodeT must derive from Tree::TreeNode");

        std::vector<std::string> path_parts;
        boost::split(path_parts, path, boost::is_any_of("."));
        if (path_parts.empty())
        {
            throw DBException("Cannot get tree node from an empty path");
        }
        for (const auto& token : path_parts)
        {
            if (token.empty())
            {
                throw DBException("Tree path contains an empty segment: ") << path;
            }
        }

        TreeNode* current = root_.get();
        for (const auto& token : path_parts)
        {
            current = current->getChild(token, false);
            if (!current)
            {
                if (must_exist)
                {
                    throw DBException("Tree node does not exist at path: ") << path;
                }
                return nullptr;
            }
        }

        auto typed_node = dynamic_cast<NodeT*>(current);
        if (!typed_node && must_exist)
        {
            throw DBException("Tree node exists at path but has incompatible type: ") << path;
        }
        return typed_node;
    }

    /// \brief Try to get a typed node at a dot-delimited path (const overload).
    /// \tparam NodeT Expected runtime node type at \a path.
    /// \param path Dot-delimited path, e.g. "top.mid.leaf".
    /// \param must_exist If true, throw when missing or type-incompatible.
    /// \return Typed node pointer, or nullptr when not found/incompatible and \a must_exist is false.
    /// \throw DBException If path is invalid, or if \a must_exist is true and node is missing/incompatible.
    template <typename NodeT> const NodeT* tryGetNodeAs(const std::string& path, bool must_exist = true) const
    {
        static_assert(std::is_base_of_v<TreeNode, NodeT>, "NodeT must derive from Tree::TreeNode");

        std::vector<std::string> path_parts;
        boost::split(path_parts, path, boost::is_any_of("."));
        if (path_parts.empty())
        {
            throw DBException("Cannot get tree node from an empty path");
        }
        for (const auto& token : path_parts)
        {
            if (token.empty())
            {
                throw DBException("Tree path contains an empty segment: ") << path;
            }
        }

        const TreeNode* current = root_.get();
        for (const auto& token : path_parts)
        {
            current = current->getChild(token, false);
            if (!current)
            {
                if (must_exist)
                {
                    throw DBException("Tree node does not exist at path: ") << path;
                }
                return nullptr;
            }
        }

        auto typed_node = dynamic_cast<const NodeT*>(current);
        if (!typed_node && must_exist)
        {
            throw DBException("Tree node exists at path but has incompatible type: ") << path;
        }
        return typed_node;
    }

    /// \brief Try to get the node at a dot-delimited path (untyped).
    /// \param path Dot-delimited path, e.g. "top.mid.leaf".
    /// \param must_exist If true, throw when the path is invalid or the node is missing.
    /// \return Node pointer, or nullptr when not found and \a must_exist is false.
    /// \throw DBException If path is invalid, or if \a must_exist is true and the node is missing.
    TreeNode* tryGet(const std::string& path, bool must_exist = true)
    {
        return tryGetNodeAs<TreeNode>(path, must_exist);
    }

    /// \brief Try to get the node at a dot-delimited path (untyped, const overload).
    /// \param path Dot-delimited path, e.g. "top.mid.leaf".
    /// \param must_exist If true, throw when the path is invalid or the node is missing.
    /// \return Const node pointer, or nullptr when not found and \a must_exist is false.
    /// \throw DBException If path is invalid, or if \a must_exist is true and the node is missing.
    const TreeNode* tryGet(const std::string& path, bool must_exist = true) const
    {
        return tryGetNodeAs<TreeNode>(path, must_exist);
    }

    /// \brief Check if there is a node at the given path
    bool contains(const std::string& path) const { return tryGet(path, false) != nullptr; }

private:
    template <typename RootT> static std::unique_ptr<TreeNode> adoptRootNode_(std::unique_ptr<RootT> root)
    {
        static_assert(std::is_base_of_v<TreeNode, RootT>, "RootT must derive from Tree::TreeNode");
        if (!root)
        {
            throw DBException("Tree root node pointer cannot be null");
        }
        return std::move(root);
    }

    bool dfsImpl_(TreeNode* node, const std::function<bool(TreeNode*)>& callback)
    {
        if (!callback(node))
        {
            return false;
        }

        for (auto& child : node->getChildren())
        {
            if (!dfsImpl_(child.get(), callback))
            {
                return false;
            }
        }

        return true;
    }

    bool dfsImpl_(const TreeNode* node, const std::function<bool(const TreeNode*)>& callback) const
    {
        if (!callback(node))
        {
            return false;
        }

        for (const auto& child : node->getChildren())
        {
            if (!dfsImpl_(child.get(), callback))
            {
                return false;
            }
        }

        return true;
    }

    std::unique_ptr<TreeNode> root_;
};

} // namespace simdb
