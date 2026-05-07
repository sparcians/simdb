#pragma once

#include "simdb/apps/argos/EnumTraits.hpp"
#include "simdb/utils/Demangle.hpp"

#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace simdb::collection {

class EnumDefinitions
{
public:
    struct EnumDefinition
    {
        EnumBackingKind backing_kind = EnumBackingKind::i32;
        std::map<int64_t, std::string> raw_to_name;
    };

    template <typename EnumT>
    void observe(const EnumT value, const std::string& enum_type_name = {})
    {
        //TODO cnyce: We should not require enum_type_name
        //TODO cnyce: We cannot have ostringstream in hot path
        //TODO cnyce: Skip stringification and various map lookups except for first-seen enum values
        static_assert(std::is_enum_v<EnumT>, "EnumDefinitions::observe requires an enum type");

        using enum_t = std::remove_cv_t<std::remove_reference_t<EnumT>>;
        using int_t = std::underlying_type_t<enum_t>;

        const std::string type_name =
            enum_type_name.empty() ? simdb::demangle_type<enum_t>() : enum_type_name;
        const int64_t raw = static_cast<int64_t>(static_cast<int_t>(value));

        std::ostringstream oss;
        oss << value;
        const std::string value_name = oss.str();

        auto& def = definitions_[type_name];
        def.backing_kind = getEnumBackingKind<int_t>();
        def.raw_to_name.emplace(raw, value_name);
    }

    std::unordered_map<std::string, EnumDefinition> getSnapshot() const
    {
        return definitions_;
    }

private:
    std::unordered_map<std::string, EnumDefinition> definitions_;
};

} // namespace simdb::collection
