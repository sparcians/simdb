// <Collectables.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/DataTypeHierarchy.hpp"
#include "simdb/apps/argos/PipelineStager.hpp"
#include "simdb/apps/argos/Minifiers.hpp"
#include "simdb/utils/Demangle.hpp"
#include "simdb/utils/TypeTraits.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace simdb::collection {

class DomainCollection;

/// Base class for all collectables.
class CollectableBase
{
public:
    virtual ~CollectableBase() = default;

    /// Get the unique ID for this collection point.
    uint16_t getID() const { return cid_; }

    /// \brief Connect to the CollectorPipeline's main input queue
    void connectToPipeline(PipelineStagerBase* stager)
    {
        stager_ = stager;
    }

    /// Enable collection
    void enable()
    {
        if (!stager_) [[unlikely]]
        {
            throw DBException("Pipeline was never opened!");
        }

        if (!enabled_)
        {
            if (initial_value_)
            {
                assert(getID() == initial_value_->getCID());
                stager_->stage(std::move(*initial_value_));
                initial_value_.reset();
            }
            enabled_ = true;
            stager_->onEnabledChanged(getID(), enabled_);
        }
    }

    /// Disable collection
    void disable()
    {
        if (!stager_) [[unlikely]]
        {
            throw DBException("Pipeline was never opened!");
        }

        if (enabled_)
        {
            enabled_ = false;
            stager_->onEnabledChanged(getID(), enabled_);
        }
    }

    /// Suppress heartbeat re-emission of previously seen bytes.
    void quiet()
    {
        if (!stager_) [[unlikely]]
        {
            throw DBException("Pipeline was never opened!");
        }

        if (!quiet_)
        {
            quiet_ = true;
            stager_->onQuietChanged(getID(), quiet_);
        }
    }

    /// Re-enable heartbeat re-emission of previously seen bytes.
    void awaken()
    {
        if (!stager_) [[unlikely]]
        {
            throw DBException("Pipeline was never opened!");
        }

        if (quiet_)
        {
            quiet_ = false;
            stager_->onQuietChanged(getID(), quiet_);
        }
    }

    /// Check enabled
    bool enabled() const
    {
        return enabled_;
    }

    /// Check whether heartbeat re-emission is suppressed.
    bool quieted() const
    {
        return quiet_;
    }

    /// Run auto-collection for this collectable
    virtual void autoCollect()
    {
        throw DBException("This collectable does not support auto-collection");
    }

    virtual void enableAutoCollect(bool enable = true)
    {
        if (enable)
        {
            throw DBException("This collectable does not support auto-collection");
        }
    }

    virtual bool autoCollecting() const
    {
        return false;
    }

    void forgetValue()
    {
        if (!stager_)
        {
            throw DBException("Pipeline not opened!");
        }
        stager_->forget(getID());
    }

    /// Demangled element type for scalars, or element demangle + \c _contig_capacityN / \c _sparse_capacityN for queues.
    virtual std::string collectableTypeNameForDb() const = 0;

    /// \brief Return true if this collectable exercised all minifier actions relevant to its type.
    virtual bool minifierSawAllActions() const = 0;

    /// For testing purposes only. DO NOT CALL IN PRODUCTION.
    static void resetCIDs()
    {
        nextCID_() = 0;
    }

protected:
    CollectableBase(DomainCollection* collection, size_t heartbeat)
        : collection_(collection)
        , heartbeat_(heartbeat)
    {}

    /// Get the heartbeat value for all collection points.
    size_t getHeartbeat_() const
    {
        return heartbeat_;
    }

    /// Stage collected bytes for pipeline processing.
    void stage_(CollectedData&& data)
    {
        if (!stager_) [[unlikely]]
        {
            throw DBException("Pipeline was never opened!");
        }
        stager_->stage(std::move(data));
    }

    void setInitialValue_(CollectedData&& initial)
    {
        if (stager_)
        {
            throw DBException("Cannot set collectable initial value after pipeline is opened");
        }
        initial_value_ = std::make_unique<CollectedData>(std::move(initial));
    }

private:
    /// Unique ID generator.
    static uint16_t& nextCID_()
    {
        static uint16_t counter = 0;
        if (counter == UINT16_MAX)
        {
            throw DBException("Max number of collectables exceeded (")
                << UINT16_MAX << ")";
        }
        ++counter;
        return counter;
    }

    /// Unique collectable ID
    const uint16_t cid_{nextCID_()};

    /// Collection object that owns 'this' collectable
    DomainCollection *const collection_;

    /// Heartbeat value for this collection point. This is the
    /// maximum number of cycles SimDB will attempt to perform
    /// "minification" on the data before it is forced to write
    /// the whole un-minified value to the database again. Note
    /// that minification is simply an implementation detail
    /// for performance.
    const size_t heartbeat_;

    /// \brief Enabled flag
    bool enabled_ = true;

    /// \brief Suppress heartbeat re-emission while true.
    bool quiet_ = false;

    /// \brief Main entry point into the pipeline
    PipelineStagerBase* stager_ = nullptr;

    /// \brief Captured initial bytes
    std::unique_ptr<CollectedData> initial_value_;

    /// \note Friendship needed to the enabled_ flag can be set
    friend class DomainCollection;
};

/// Template class for all scalar types (POD, struct-like, string, enum, bool)
template <typename ScalarT>
class ScalarCollector : public CollectableBase
{
public:
    using ValueType = type_traits::remove_any_pointer_t<ScalarT>;

    ScalarCollector(DomainCollection* collection,
                    size_t heartbeat,
                    std::shared_ptr<DataTypeHierarchy<ValueType>> dtype_hierarchy)
        : CollectableBase(collection, heartbeat)
        , dtype_hierarchy_(std::move(dtype_hierarchy))
        , minifier_(dtype_hierarchy_, heartbeat)
    {}

    std::string collectableTypeNameForDb() const override
    {
        if constexpr (std::is_same_v<ValueType, std::string>)
        {
            return "string";
        }
        else
        {
            return simdb::demangle_type<ValueType>();
        }
    }

    /// \brief On-demand collection, also called by auto-collecting subclass
    template <typename T = ScalarT>
    std::enable_if_t<!type_traits::is_any_pointer_v<T>, void>
    collect(const T& value)
    {
        if (!enabled())
        {
            return;
        }

        if (quieted())
        {
            awaken();
        }

        CollectedData collected(getID());
        if constexpr (detail::has_argos_collector_v<ValueType>)
        {
            minifier_.minifyAndAppend(collected.getBuffer(), value, getID());
        }
        else
        {
            collected.getBuffer() << LifecycleAction::__FIRST_MINIFIER_ACTION;
            dtype_hierarchy_->writeBuffer(collected.getBuffer(), value);
        }
        stage_(std::move(collected));
    }

    /// \brief Pointer-version of collect()
    template <typename T = ScalarT>
    std::enable_if_t<type_traits::is_any_pointer_v<T>, void>
    collect(const T& value)
    {
        if (value)
        {
            collect(*value);
        }
        else
        {
            disable();
        }
    }

    virtual void reattach(const ScalarT*)
    {
        throw DBException("Cannot reattach a manually-collected object");
    }

    bool hasMinifierActions() const
    {
        return detail::has_argos_collector_v<ValueType>;
    }

    std::vector<size_t> getMinifierActionCounts() const
    {
        if constexpr (detail::has_argos_collector_v<ValueType>)
        {
            return minifier_.getActionCounts();
        }
        return {};
    }

    bool minifierSawAllActions() const override
    {
        if constexpr (detail::has_argos_collector_v<ValueType>)
        {
            return minifier_.sawAllActions();
        }
        return true;
    }

private:
    std::shared_ptr<DataTypeHierarchy<ValueType>> dtype_hierarchy_;
    Minifier<ValueType> minifier_;
};

/// Same as ScalarCollector, but supports auto-collection using a backpointer
template <typename ScalarT>
class AutoScalarCollector : public ScalarCollector<ScalarT>
{
public:
    using ValueType = typename ScalarCollector<ScalarT>::ValueType;

    /// \brief Construct with a backpointer to the auto-collected scalar
    AutoScalarCollector(DomainCollection* collection,
                        size_t heartbeat,
                        std::shared_ptr<DataTypeHierarchy<ValueType>> dtype_hierarchy,
                        const ScalarT* scalar)
        : ScalarCollector<ScalarT>(collection, heartbeat, std::move(dtype_hierarchy))
        , scalar_(scalar)
    {}

    /// Run auto-collection for this collectable
    void autoCollect() override
    {
        if (auto_collecting_)
        {
            this->collect(*scalar_);
        }
    }

    void enableAutoCollect(bool enable = true) override
    {
        auto_collecting_ = enable;
    }

    bool autoCollecting() const
    {
        return auto_collecting_;
    }

    void reattach(const ScalarT* scalar) override
    {
        assert(scalar != nullptr);
        scalar_ = scalar;
    }

private:
    const ScalarT* scalar_;
    bool auto_collecting_ = true;
};

/// ContainerCollector base class which adds non-template metadata APIs
class ContainerCollectorBase : public CollectableBase
{
public:
    using CollectableBase::CollectableBase;
    virtual size_t getMaxContainerSizeCollected() const = 0;
};

/// Template class for all container types (vector, deque, etc.)
template <typename ContainerT, bool Sparse>
class ContainerCollector : public ContainerCollectorBase
{
public:
    using InnerContainerT = type_traits::remove_any_pointer_t<ContainerT>;
    using ValueType =
        typename type_traits::remove_any_pointer_t<typename InnerContainerT::value_type>;

    ContainerCollector(DomainCollection* collection,
                       size_t heartbeat,
                       size_t expected_capacity,
                       std::shared_ptr<DataTypeHierarchy<ValueType>> dtype_hierarchy)
        : ContainerCollectorBase(collection, heartbeat)
        , expected_capacity_(expected_capacity)
        , minifier_(
            std::move(dtype_hierarchy),
            heartbeat,
            expected_capacity,
            simdb::demangle_type<ValueType>())
    {}

    std::string collectableTypeNameForDb() const override
    {
        std::string base = simdb::demangle_type<ValueType>();
        if constexpr (Sparse)
        {
            return base + "_sparse_capacity" + std::to_string(expected_capacity_);
        }
        return base + "_contig_capacity" + std::to_string(expected_capacity_);
    }

    /// \brief On-demand collection, also called by auto-collecting subclass
    template <typename T = ContainerT>
    std::enable_if_t<!type_traits::is_any_pointer_v<T>, void>
    collect(const T& container)
    {
        if (!enabled())
        {
            return;
        }

        if (quieted())
        {
            awaken();
        }

        CollectedData collected(getID());
        auto num_elements = getNumElements<T, Sparse>(container);
        max_size_collected_ = std::max(max_size_collected_, num_elements);
        minifier_.minifyAndAppend(collected.getBuffer(), container, getID());

        stage_(std::move(collected));
    }

    /// \brief Pointer-version of collect()
    template <typename T = ContainerT>
    std::enable_if_t<type_traits::is_any_pointer_v<T>, void>
    collect(const T& container)
    {
        if (container)
        {
            collect(*container);
        }
        else
        {
            disable();
        }
    }

    size_t getMaxContainerSizeCollected() const override final
    {
        return max_size_collected_;
    }

    bool hasMinifierActions() const
    {
        return true;
    }

    std::vector<size_t> getMinifierActionCounts() const
    {
        return minifier_.getActionCounts();
    }

    bool minifierSawAllActions() const override
    {
        return minifier_.sawAllActions();
    }

    virtual void reattach(const ContainerT*)
    {
        throw DBException("Cannot reattach a manually-collected object");
    }

protected:
    const size_t expected_capacity_;

private:
    using MinifierType = std::conditional_t<
        Sparse,
        SparseContainerMinifier<InnerContainerT>,
        ContigContainerMinifier<InnerContainerT>>;
    MinifierType minifier_;
    uint16_t max_size_collected_ = 0;
};

/// \class AutoContainerCollector
/// \brief Container collectable that reads from a user-held const pointer for auto-collection (parallel to \ref AutoScalarCollector for scalars).
/// \tparam ContainerT Container type whose values are collected (vector, deque, etc.).
/// \tparam Sparse Reserved with \ref ContainerCollector for optional sparse-container semantics in future minification paths.
template <typename ContainerT, bool Sparse>
class AutoContainerCollector : public ContainerCollector<ContainerT, Sparse>
{
public:
    using ValueType = typename ContainerCollector<ContainerT, Sparse>::ValueType;

    /// \brief Construct with a backpointer to the auto-collected container
    AutoContainerCollector(DomainCollection* collection,
                           size_t heartbeat,
                           const ContainerT* container,
                           size_t expected_capacity,
                           std::shared_ptr<DataTypeHierarchy<ValueType>> dtype_hierarchy)
        : ContainerCollector<ContainerT, Sparse>(
            collection, heartbeat, expected_capacity,
            std::move(dtype_hierarchy))
        , container_(container)
    {}

    /// Run auto-collection for this collectable
    void autoCollect() override
    {
        if (auto_collecting_)
        {
            this->collect(*container_);
        }
    }

    void enableAutoCollect(bool enable = true) override
    {
        auto_collecting_ = enable;
    }

    bool autoCollecting() const
    {
        return auto_collecting_;
    }

    virtual void reattach(const ContainerT* container)
    {
        assert(container != nullptr);
        container_ = container;
    }

private:
    const ContainerT* container_;
    bool auto_collecting_ = true;
};

} // namespace simdb::collection
