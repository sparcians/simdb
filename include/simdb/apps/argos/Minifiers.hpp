// <Minifiers.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/DataTypeHierarchy.hpp"
#include "simdb/apps/argos/LifecycleAction.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace simdb::collection {

template <typename ContainerT, bool Sparse>
inline uint16_t getNumElements(const ContainerT& container)
{
    // TODO cnyce: Do we support collecting things like vector<int>?
    // We use "if (*it)" to match legacy behavior, but that stops
    // vector<int> from collecting actual values of 0. It looks like
    // the legacy behavior is to assume that queues always store
    // pointers (which is a reasonable assumption for simulators,
    // but not so much for general-purpose collection).
    static_assert(type_traits::is_any_pointer_v<typename ContainerT::value_type>);

    size_t count = 0;
    for (auto it = container.begin(), end = container.end(); it != end; ++it)
    {
        bool valid = false;
        if constexpr (type_traits::is_collectable_stl_v<ContainerT>)
        {
            if (*it)
            {
                valid = true;
            }
        }
        else
        {
            if (it.isValid())
            {
                valid = true;
            }
        }

        if (valid)
        {
            ++count;
        }
        else if (!Sparse)
        {
            break;
        }
    }

    if (count > UINT16_MAX)
    {
        throw DBException("Queue too large to collect; uint16_t exceeded");
    }
    return static_cast<uint16_t>(count);
}

template <typename ValueType>
class Minifier
{
    static_assert(!type_traits::is_collectable_stl_v<ValueType>);

public:
    Minifier(std::shared_ptr<DataTypeHierarchy<ValueType>> dtype_hierarchy,
             size_t heartbeat)
        : dtype_hierarchy_(dtype_hierarchy)
        , heartbeat_(heartbeat)
    {}

    void minifyAndAppend(StreamBuffer& buf, const ValueType& value)
    {
        struct FieldEvent
        {
            std::size_t bytes = 0;
            std::string field_name;
            std::string dtype_name;
            std::string value_repr;
        };
        class PendingFieldTraceSink final : public FieldTraceSink
        {
        public:
            void beginStructFields(std::string_view label, std::size_t total_bytes) override
            {
                label_ = std::string(label);
                total_bytes_ = total_bytes;
            }
            void endStructFields() override {}
            void recordFieldBytes(
                std::size_t num_bytes,
                std::string_view field_name,
                std::string_view dtype_name,
                std::string_view value_repr) override
            {
                events_.push_back(FieldEvent{
                    num_bytes,
                    std::string(field_name),
                    std::string(dtype_name),
                    std::string(value_repr)
                });
            }

            const std::string& label() const { return label_; }
            std::size_t totalBytes() const { return total_bytes_; }
            const std::vector<FieldEvent>& events() const { return events_; }

        private:
            std::string label_ = "struct fields";
            std::size_t total_bytes_ = 0;
            std::vector<FieldEvent> events_;
        };

        auto* tracer = simdb::utils::active_collection_byte_tracer();
        PendingFieldTraceSink field_sink;
        FieldTraceSink* field_trace_sink = nullptr;
        if constexpr (detail::has_argos_collector_v<ValueType>)
        {
            field_trace_sink = tracer ? &field_sink : nullptr;
        }

        StreamBuffer my_buffer(cur_extracted_bytes_, true, false);
        dtype_hierarchy_->writeBuffer(my_buffer, value, &expected_num_bytes_, field_trace_sink);

        if (!has_history_ || shouldWriteFull_() || last_sent_bytes_ != cur_extracted_bytes_)
        {
            const auto raw_action = static_cast<uint8_t>(MinifierAction::FULL);
            buf.appendValue(raw_action, "action", actionName_(MinifierAction::FULL));
            ++action_counts_[actionIndex_(MinifierAction::FULL)];
            if (tracer && field_trace_sink)
            {
                const auto group_label = std::string("struct ") + dtype_hierarchy_->getRoot().type_name + " fields";
                simdb::utils::ScopedCollectionTraceGroup fields_group(tracer, group_label, field_sink.totalBytes());
                for (const auto& ev : field_sink.events())
                {
                    std::string desc;
                    if (ev.dtype_name == "string")
                    {
                        desc = "field: " + ev.field_name + ", dtype: string, " + ev.value_repr;
                    }
                    else
                    {
                        desc = "field: " + ev.field_name + ", dtype: " + ev.dtype_name + ", value: " + ev.value_repr;
                    }
                    tracer->recordWrite(ev.bytes, desc);
                }
                if (!field_sink.events().empty())
                {
                    buf.append_without_byte_trace(cur_extracted_bytes_.data(), cur_extracted_bytes_.size());
                }
                else
                {
                    buf.append(cur_extracted_bytes_);
                }
            }
            else
            {
                buf.append(cur_extracted_bytes_);
            }
            last_sent_bytes_ = cur_extracted_bytes_;
            cycles_since_last_full_ = 0;
            has_history_ = true;
        }
        else
        {
            const auto raw_action = static_cast<uint8_t>(MinifierAction::CARRY);
            buf.appendValue(raw_action, "action", actionName_(MinifierAction::CARRY));
            ++action_counts_[actionIndex_(MinifierAction::CARRY)];
            ++cycles_since_last_full_;
        }
    }

    std::vector<size_t> getActionCounts() const
    {
        return std::vector<size_t>(action_counts_.begin(), action_counts_.end());
    }

    bool sawAllActions() const
    {
        return std::all_of(action_counts_.begin(), action_counts_.end(), [](size_t n) {
            return n > 0;
        });
    }

private:
    enum class MinifierAction : uint8_t
    {
        FULL = static_cast<uint16_t>(LifecycleAction::__FIRST_MINIFIER_ACTION),   // Value changed or we are at a heartbeat.
        CARRY       // Same value or not at a heartbeat.
    };

    static size_t actionIndex_(const MinifierAction action)
    {
        constexpr auto base = static_cast<size_t>(MinifierAction::FULL);
        return static_cast<size_t>(action) - base;
    }

    static const char* actionName_(const MinifierAction action) noexcept
    {
        switch (action)
        {
            case MinifierAction::FULL:
                return "FULL";
            case MinifierAction::CARRY:
                return "CARRY";
        }
        return "?";
    }

    std::shared_ptr<DataTypeHierarchy<ValueType>> dtype_hierarchy_;
    const size_t heartbeat_;
    size_t cycles_since_last_full_ = 0;
    bool has_history_ = false;
    std::array<size_t, 2> action_counts_{};
    std::vector<char> last_sent_bytes_;
    std::vector<char> cur_extracted_bytes_;
    ValidValue<size_t> expected_num_bytes_;

    /// Heartbeat is validated non-zero in \c Collection<TimeT> ctor. \c heartbeat_ == 1 ⇒ FULL every collect.
    bool shouldWriteFull_() const
    {
        return (cycles_since_last_full_ + 1) % heartbeat_ == 0;
    }
};

template <typename ContainerType>
class ContigContainerMinifier
{
    static_assert(!type_traits::is_any_pointer_v<ContainerType>);
    using ValueType = type_traits::remove_any_pointer_t<typename ContainerType::value_type>;

public:
    ContigContainerMinifier(std::shared_ptr<DataTypeHierarchy<ValueType>> dtype_hierarchy,
                            size_t heartbeat,
                            size_t expected_capacity,
                            const std::string& elem_path)
        : dtype_hierarchy_(dtype_hierarchy)
        , heartbeat_(heartbeat)
        , prev_bins_(expected_capacity)
        , curr_bins_(expected_capacity)
        , elem_path_(elem_path)
    {}

    void minifyAndAppend(StreamBuffer& buf, const ContainerType& container)
    {
        const auto curr_size = writeBins_(container, curr_bins_);
        const auto action = (!has_history_ || shouldWriteFull_())
            ? MinifierAction::FULL
            : getMinifierAction_(curr_bins_, curr_size, prev_bins_, prev_size_);

        writeAction_(buf, action, curr_size);

        if (prev_bins_.size() < curr_bins_.size())
        {
            prev_bins_.resize(curr_bins_.size());
        }
        for (size_t i = 0; i < curr_size; ++i)
        {
            prev_bins_[i] = curr_bins_[i];
        }
        prev_size_ = curr_size;

        if (action == MinifierAction::FULL)
        {
            cycles_since_last_full_ = 0;
            has_history_ = true;
        }
        else
        {
            ++cycles_since_last_full_;
        }
    }

    std::vector<size_t> getActionCounts() const
    {
        return std::vector<size_t>(action_counts_.begin(), action_counts_.end());
    }

    bool sawAllActions() const
    {
        return std::all_of(action_counts_.begin(), action_counts_.end(), [](size_t n) {
            return n > 0;
        });
    }

private:
    struct FieldEvent
    {
        std::size_t bytes = 0;
        std::string field_name;
        std::string dtype_name;
        std::string value_repr;
    };

    class PendingFieldTraceSink final : public FieldTraceSink
    {
    public:
        void beginStructFields(std::string_view, std::size_t total_bytes) override
        {
            total_bytes_ = total_bytes;
        }
        void endStructFields() override {}
        void recordFieldBytes(
            std::size_t num_bytes,
            std::string_view field_name,
            std::string_view dtype_name,
            std::string_view value_repr) override
        {
            events_.push_back(FieldEvent{
                num_bytes,
                std::string(field_name),
                std::string(dtype_name),
                std::string(value_repr)
            });
        }

        std::size_t totalBytes() const { return total_bytes_; }
        const std::vector<FieldEvent>& events() const { return events_; }

    private:
        std::size_t total_bytes_ = 0;
        std::vector<FieldEvent> events_;
    };

    enum class MinifierAction : uint8_t
    {
        FULL = static_cast<uint16_t>(LifecycleAction::__FIRST_MINIFIER_ACTION),   // Value changed or we are at a heartbeat.
        CARRY,      // Same value or not at a heartbeat.
        SWAP,       // One item changed.
        ARRIVE,     // One item arrived at the back of the container.
        DEPART,     // One item left the front of the container.
        BOOKENDS    // One arrived and one departed.
    };

    static size_t actionIndex_(const MinifierAction action)
    {
        constexpr auto base = static_cast<size_t>(MinifierAction::FULL);
        return static_cast<size_t>(action) - base;
    }

    uint16_t writeBins_(const ContainerType& container, std::vector<std::vector<char>>& bins)
    {
        auto num_elements = getNumElements<ContainerType, false>(container);
        if (num_elements > bins.size())
        {
            if (warn_on_size_)
            {
                std::cout << "WARNING! The collected object '" << elem_path_ << "' has grown beyond the "
                          << "expected capacity (given at construction) for collection. Expected "
                          << bins.size() << " but grew to " << container.size() << ". This is your "
                          << "first and last warning.\n";
                warn_on_size_ = false;
            }
            bins.resize(num_elements);
        }

        auto it = container.begin();
        size_t bin_idx = 0;
        while (bin_idx < num_elements)
        {
            writeBin_(*it++, bins[bin_idx], bin_idx);
            ++bin_idx;
        }
        return num_elements;
    }

    void writeBin_(const ValueType& bin_value, std::vector<char>& bin_buffer, const size_t bin_idx)
    {
        StreamBuffer buf(bin_buffer, true, false);
        auto* tracer = simdb::utils::active_collection_byte_tracer();
        if constexpr (detail::has_argos_collector_v<ValueType>)
        {
            if (tracer)
            {
                PendingFieldTraceSink field_sink;
                dtype_hierarchy_->writeBuffer(buf, bin_value, &expected_num_bytes_, &field_sink);
                if (curr_bin_field_events_.size() <= bin_idx)
                {
                    curr_bin_field_events_.resize(bin_idx + 1);
                }
                curr_bin_field_events_[bin_idx] = field_sink.events();
                return;
            }
        }

        dtype_hierarchy_->writeBuffer(buf, bin_value, &expected_num_bytes_);
    }

    template <typename BinType>
    std::enable_if_t<type_traits::is_any_pointer_v<BinType>, void>
    writeBin_(const BinType& ptr, std::vector<char>& bin_buffer, const size_t bin_idx)
    {
        assert(ptr != nullptr);
        writeBin_(*ptr, bin_buffer, bin_idx);
    }

    void appendBinWithTrace_(StreamBuffer& buf, const size_t bin_idx)
    {
        auto* tracer = simdb::utils::active_collection_byte_tracer();
        const auto& bytes = curr_bins_[bin_idx];
        if (!tracer || bin_idx >= curr_bin_field_events_.size() || curr_bin_field_events_[bin_idx].empty())
        {
            buf.append(bytes);
            return;
        }

        std::ostringstream group_label;
        group_label << "struct " << dtype_hierarchy_->getRoot().type_name << " fields (bin idx " << bin_idx << ")";
        simdb::utils::ScopedCollectionTraceGroup fields_group(tracer, group_label.str(), bytes.size());
        for (const auto& ev : curr_bin_field_events_[bin_idx])
        {
            std::string desc;
            if (ev.dtype_name == "string")
            {
                desc = "field: " + ev.field_name + ", dtype: string, " + ev.value_repr;
            }
            else
            {
                desc = "field: " + ev.field_name + ", dtype: " + ev.dtype_name + ", value: " + ev.value_repr;
            }
            tracer->recordWrite(ev.bytes, desc);
        }
        buf.append_without_byte_trace(bytes.data(), bytes.size());
    }

    static MinifierAction getMinifierAction_(
        const std::vector<std::vector<char>>& curr_bins,
        const uint16_t curr_size,
        const std::vector<std::vector<char>>& prev_bins,
        const uint16_t prev_size)
    {
        if (curr_size == prev_size)
        {
            size_t changed_idx = 0;
            size_t num_changes = 0;
            for (size_t i = 0; i < curr_size; ++i)
            {
                if (curr_bins[i] != prev_bins[i])
                {
                    changed_idx = i;
                    ++num_changes;
                    if (num_changes > 1)
                    {
                        break;
                    }
                }
            }

            if (num_changes == 0)
            {
                return MinifierAction::CARRY;
            }
            if (num_changes == 1)
            {
                (void)changed_idx;
                return MinifierAction::SWAP;
            }

            if (curr_size > 0)
            {
                bool shifted = true;
                for (size_t i = 0; i + 1 < curr_size; ++i)
                {
                    if (curr_bins[i] != prev_bins[i + 1])
                    {
                        shifted = false;
                        break;
                    }
                }
                if (shifted)
                {
                    return MinifierAction::BOOKENDS;
                }
            }
        }
        else if (curr_size == prev_size + 1)
        {
            bool prefix_match = true;
            for (size_t i = 0; i < prev_size; ++i)
            {
                if (curr_bins[i] != prev_bins[i])
                {
                    prefix_match = false;
                    break;
                }
            }
            if (prefix_match)
            {
                return MinifierAction::ARRIVE;
            }
        }
        else if (prev_size == curr_size + 1)
        {
            bool tail_match = true;
            for (size_t i = 0; i < curr_size; ++i)
            {
                if (curr_bins[i] != prev_bins[i + 1])
                {
                    tail_match = false;
                    break;
                }
            }
            if (tail_match)
            {
                return MinifierAction::DEPART;
            }
        }

        return MinifierAction::FULL;
    }

    /// Heartbeat is validated non-zero in \c Collection<TimeT> ctor. \c heartbeat_ == 1 ⇒ FULL every collect.
    bool shouldWriteFull_() const
    {
        return (cycles_since_last_full_ + 1) % heartbeat_ == 0;
    }

    static size_t findSwapIndex_(
        const std::vector<std::vector<char>>& curr_bins,
        const uint16_t curr_size,
        const std::vector<std::vector<char>>& prev_bins)
    {
        for (size_t i = 0; i < curr_size; ++i)
        {
            if (curr_bins[i] != prev_bins[i])
            {
                return i;
            }
        }
        return 0;
    }

    static const char* actionName_(const MinifierAction action) noexcept
    {
        switch (action)
        {
            case MinifierAction::FULL:
                return "FULL";
            case MinifierAction::CARRY:
                return "CARRY";
            case MinifierAction::SWAP:
                return "SWAP";
            case MinifierAction::ARRIVE:
                return "ARRIVE";
            case MinifierAction::DEPART:
                return "DEPART";
            case MinifierAction::BOOKENDS:
                return "BOOKENDS";
        }
        return "?";
    }

    void writeAction_(StreamBuffer& buf, const MinifierAction action, const uint16_t curr_size)
    {
        ++action_counts_[actionIndex_(action)];
        const auto raw_action = static_cast<uint8_t>(action);
        buf.appendValue(raw_action, "action", actionName_(action));
        switch (action)
        {
            case MinifierAction::CARRY:
            case MinifierAction::DEPART:
                return;

            case MinifierAction::SWAP:
            {
                const auto changed_idx = static_cast<uint16_t>(
                    findSwapIndex_(curr_bins_, curr_size, prev_bins_));
                buf.appendValue(changed_idx, "idx", std::to_string(changed_idx));
                appendBinWithTrace_(buf, changed_idx);
                return;
            }

            case MinifierAction::ARRIVE:
                appendBinWithTrace_(buf, curr_size - 1);
                return;

            case MinifierAction::BOOKENDS:
                appendBinWithTrace_(buf, curr_size - 1);
                return;

            case MinifierAction::FULL:
                buf.appendValue(curr_size, "size", std::to_string(curr_size));
                for (size_t i = 0; i < curr_size; ++i)
                {
                    appendBinWithTrace_(buf, i);
                }
                return;
        }
    }

    std::shared_ptr<DataTypeHierarchy<ValueType>> dtype_hierarchy_;
    const size_t heartbeat_;
    size_t cycles_since_last_full_ = 0;
    bool has_history_ = false;
    std::array<size_t, 6> action_counts_{};
    uint16_t prev_size_ = 0;
    std::vector<std::vector<char>> prev_bins_;
    std::vector<std::vector<char>> curr_bins_;
    std::vector<std::vector<FieldEvent>> curr_bin_field_events_;
    const std::string elem_path_;
    bool warn_on_size_ = true;
    ValidValue<size_t> expected_num_bytes_;
};

template <typename ContainerType>
class SparseContainerMinifier
{
    static_assert(!type_traits::is_any_pointer_v<ContainerType>);
    using ValueType = type_traits::remove_any_pointer_t<typename ContainerType::value_type>;
    using BinPair = std::pair<uint16_t, std::vector<char>>;

public:
    SparseContainerMinifier(std::shared_ptr<DataTypeHierarchy<ValueType>> dtype_hierarchy,
                            size_t heartbeat,
                            size_t expected_capacity,
                            const std::string& elem_path)
        : dtype_hierarchy_(dtype_hierarchy)
        , heartbeat_(heartbeat)
    {
        (void)elem_path;
        curr_pairs_.reserve(expected_capacity);
    }

    void minifyAndAppend(StreamBuffer& buf, const ContainerType& container)
    {
        writePairs_(container);

        uint16_t exchange_idx = 0;
        const auto action = (!has_history_ || shouldWriteFull_())
            ? MinifierAction::FULL
            : getMinifierAction_(exchange_idx);

        writeAction_(buf, action, exchange_idx);

        prev_bins_.clear();
        prev_bins_.reserve(curr_pairs_.size());
        for (const auto& [idx, bytes] : curr_pairs_)
        {
            prev_bins_.emplace(idx, bytes);
        }

        if (action == MinifierAction::FULL)
        {
            cycles_since_last_full_ = 0;
            has_history_ = true;
        }
        else
        {
            ++cycles_since_last_full_;
        }
    }

    std::vector<size_t> getActionCounts() const
    {
        return std::vector<size_t>(action_counts_.begin(), action_counts_.end());
    }

    bool sawAllActions() const
    {
        return std::all_of(action_counts_.begin(), action_counts_.end(), [](size_t n) {
            return n > 0;
        });
    }

private:
    struct FieldEvent
    {
        std::size_t bytes = 0;
        std::string field_name;
        std::string dtype_name;
        std::string value_repr;
    };

    class PendingFieldTraceSink final : public FieldTraceSink
    {
    public:
        void beginStructFields(std::string_view, std::size_t total_bytes) override
        {
            total_bytes_ = total_bytes;
        }
        void endStructFields() override {}
        void recordFieldBytes(
            std::size_t num_bytes,
            std::string_view field_name,
            std::string_view dtype_name,
            std::string_view value_repr) override
        {
            events_.push_back(FieldEvent{
                num_bytes,
                std::string(field_name),
                std::string(dtype_name),
                std::string(value_repr)
            });
        }

        std::size_t totalBytes() const { return total_bytes_; }
        const std::vector<FieldEvent>& events() const { return events_; }

    private:
        std::size_t total_bytes_ = 0;
        std::vector<FieldEvent> events_;
    };

    enum class MinifierAction : uint8_t
    {
        FULL = static_cast<uint16_t>(LifecycleAction::__FIRST_MINIFIER_ACTION),
        CARRY,
        EXCHANGE,
        REMOVE
    };

    static const char* actionName_(const MinifierAction action) noexcept
    {
        switch (action)
        {
            case MinifierAction::FULL:
                return "FULL";
            case MinifierAction::CARRY:
                return "CARRY";
            case MinifierAction::EXCHANGE:
                return "EXCHANGE";
            case MinifierAction::REMOVE:
                return "REMOVE";
        }
        return "?";
    }

    static size_t actionIndex_(const MinifierAction action)
    {
        constexpr auto base = static_cast<size_t>(MinifierAction::FULL);
        return static_cast<size_t>(action) - base;
    }

    void writePairs_(const ContainerType& container)
    {
        curr_pairs_.clear();
        curr_pair_field_events_.clear();
        uint16_t bin_idx = 0;
        for (auto it = container.begin(), end = container.end(); it != end; ++it, ++bin_idx)
        {
            bool valid = false;
            if constexpr (type_traits::is_collectable_stl_v<ContainerType>)
            {
                if (*it)
                {
                    valid = true;
                }
            }
            else
            {
                if (it.isValid())
                {
                    valid = true;
                }
            }

            if (!valid)
            {
                continue;
            }

            curr_pairs_.emplace_back(bin_idx, std::vector<char>{});
            writeBin_(*it, curr_pairs_.back().second, bin_idx);
        }
    }

    void writeBin_(const ValueType& bin_value, std::vector<char>& bin_buffer, const uint16_t bin_idx)
    {
        StreamBuffer buf(bin_buffer, true, false);
        auto* tracer = simdb::utils::active_collection_byte_tracer();
        if constexpr (detail::has_argos_collector_v<ValueType>)
        {
            if (tracer)
            {
                PendingFieldTraceSink field_sink;
                dtype_hierarchy_->writeBuffer(buf, bin_value, &expected_num_bytes_, &field_sink);
                curr_pair_field_events_[bin_idx] = field_sink.events();
                return;
            }
        }
        dtype_hierarchy_->writeBuffer(buf, bin_value, &expected_num_bytes_);
    }

    template <typename BinType>
    std::enable_if_t<type_traits::is_any_pointer_v<BinType>, void>
    writeBin_(const BinType& ptr, std::vector<char>& bin_buffer, const uint16_t bin_idx)
    {
        assert(ptr != nullptr);
        writeBin_(*ptr, bin_buffer, bin_idx);
    }

    void appendBinWithTrace_(StreamBuffer& buf, const uint16_t bin_idx, const std::vector<char>& bytes) const
    {
        auto* tracer = simdb::utils::active_collection_byte_tracer();
        auto field_it = curr_pair_field_events_.find(bin_idx);
        if (!tracer || field_it == curr_pair_field_events_.end() || field_it->second.empty())
        {
            buf.append(bytes);
            return;
        }

        std::ostringstream group_label;
        group_label << "struct " << dtype_hierarchy_->getRoot().type_name << " fields (bin idx " << bin_idx << ")";
        simdb::utils::ScopedCollectionTraceGroup fields_group(tracer, group_label.str(), bytes.size());
        for (const auto& ev : field_it->second)
        {
            std::string desc;
            if (ev.dtype_name == "string")
            {
                desc = "field: " + ev.field_name + ", dtype: string, " + ev.value_repr;
            }
            else
            {
                desc = "field: " + ev.field_name + ", dtype: " + ev.dtype_name + ", value: " + ev.value_repr;
            }
            tracer->recordWrite(ev.bytes, desc);
        }
        buf.append_without_byte_trace(bytes.data(), bytes.size());
    }

    MinifierAction getMinifierAction_(uint16_t& exchange_idx) const
    {
        size_t num_removed = 0;
        size_t num_upserted = 0;
        uint16_t removed_idx = 0;

        std::unordered_map<uint16_t, const std::vector<char>*> curr_map;
        curr_map.reserve(curr_pairs_.size());
        for (const auto& [idx, bytes] : curr_pairs_)
        {
            curr_map.emplace(idx, &bytes);
        }

        for (const auto& [idx, prev_bytes] : prev_bins_)
        {
            const auto curr_it = curr_map.find(idx);
            if (curr_it == curr_map.end())
            {
                removed_idx = idx;
                ++num_removed;
                if (num_removed > 1)
                {
                    return MinifierAction::FULL;
                }
                continue;
            }

            if (prev_bytes != *curr_it->second)
            {
                exchange_idx = idx;
                ++num_upserted;
                if (num_upserted > 1)
                {
                    return MinifierAction::FULL;
                }
            }
        }

        for (const auto& [idx, curr_bytes] : curr_pairs_)
        {
            if (prev_bins_.find(idx) == prev_bins_.end())
            {
                exchange_idx = idx;
                ++num_upserted;
                if (num_upserted > 1)
                {
                    return MinifierAction::FULL;
                }
            }
            (void)curr_bytes;
        }

        if (num_removed == 0 && num_upserted == 0)
        {
            return MinifierAction::CARRY;
        }
        if (num_removed == 0 && num_upserted == 1)
        {
            return MinifierAction::EXCHANGE;
        }
        if (num_removed == 1 && num_upserted == 0)
        {
            exchange_idx = removed_idx;
            return MinifierAction::REMOVE;
        }
        return MinifierAction::FULL;
    }

    void writeAction_(StreamBuffer& buf, MinifierAction action, const uint16_t exchange_idx) const
    {
        ++action_counts_[actionIndex_(action)];
        const auto raw_action = static_cast<uint8_t>(action);
        buf.appendValue(raw_action, "action", actionName_(action));
        switch (action)
        {
            case MinifierAction::CARRY:
                return;
            case MinifierAction::EXCHANGE:
            {
                buf.appendValue(exchange_idx, "idx", std::to_string(exchange_idx));
                for (const auto& [idx, bytes] : curr_pairs_)
                {
                    if (idx == exchange_idx)
                    {
                        appendBinWithTrace_(buf, idx, bytes);
                        return;
                    }
                }
                return;
            }
            case MinifierAction::REMOVE:
                buf.appendValue(exchange_idx, "idx", std::to_string(exchange_idx));
                return;
            case MinifierAction::FULL:
            {
                const auto size = static_cast<uint16_t>(curr_pairs_.size());
                buf.appendValue(size, "size", std::to_string(size));
                for (const auto& [idx, bytes] : curr_pairs_)
                {
                    buf.appendValue(idx, "idx", std::to_string(idx));
                    appendBinWithTrace_(buf, idx, bytes);
                }
                return;
            }
        }
    }

    /// Heartbeat is validated non-zero in \c Collection<TimeT> ctor. \c heartbeat_ == 1 ⇒ FULL every collect.
    bool shouldWriteFull_() const
    {
        return (cycles_since_last_full_ + 1) % heartbeat_ == 0;
    }

    std::shared_ptr<DataTypeHierarchy<ValueType>> dtype_hierarchy_;
    const size_t heartbeat_;
    size_t cycles_since_last_full_ = 0;
    bool has_history_ = false;
    mutable std::array<size_t, 4> action_counts_{};
    std::unordered_map<uint16_t, std::vector<char>> prev_bins_;
    std::vector<BinPair> curr_pairs_;
    std::unordered_map<uint16_t, std::vector<FieldEvent>> curr_pair_field_events_;
    ValidValue<size_t> expected_num_bytes_;
};

} // namespace simdb::collection
