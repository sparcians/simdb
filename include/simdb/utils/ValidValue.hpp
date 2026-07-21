#pragma once

#include "simdb/Assert.hpp"
#include "simdb/Exceptions.hpp"
#include "simdb/utils/TypeTraits.hpp"
#include <iostream>
#include <utility>

namespace simdb {

template <typename T> class ValidValue
{
private:
    T value_{};
    bool valid_ = false;

public:
    ValidValue() = default;

    explicit ValidValue(const T& val) :
        value_(val),
        valid_(true)
    {
    }

    explicit ValidValue(T&& val) :
        value_(std::move(val)),
        valid_(true)
    {
    }

    ValidValue& operator=(const T& val)
    {
        value_ = val;
        valid_ = true;
        return *this;
    }

    ValidValue& operator=(T&& val)
    {
        value_ = std::move(val);
        valid_ = true;
        return *this;
    }

    const T& getValue() const
    {
        simdb_assert(valid_, "Invalid value - not set");
        return value_;
    }

    T& getValue()
    {
        simdb_assert(valid_, "Invalid value - not set");
        return value_;
    }

    operator const T&() const { return getValue(); }

    operator T&() { return getValue(); }

    bool isValid() const { return valid_; }

    void clearValid() { valid_ = false; }
};

} // namespace simdb

template <typename T> inline std::ostream& operator<<(std::ostream& os, const simdb::ValidValue<T>& vv)
{
    static_assert(simdb::type_traits::has_ostream_operator_v<T>);
    if (!vv.isValid())
    {
        return os << "<INVALID>";
    }
    return os << vv.getValue();
}
