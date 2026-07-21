// <Dump.hpp> -*- C++ -*-

#pragma once

#include "simdb/sqlite/DatabaseManager.hpp"

#include <algorithm>
#include <deque>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace simdb {

namespace detail {
template <typename T> inline bool to_string(const std::optional<T>& opt, std::string& out)
{
    if (!opt.has_value())
    {
        return false;
    }

    if constexpr (std::is_same_v<T, std::string>)
    {
        out = opt.value();
    } else
    {
        out = std::to_string(opt.value());
    }
    return true;
}

inline void dumpTable(const std::string& table_name, const std::vector<std::string>& headers,
                      const std::vector<std::vector<std::string>>& row_strings)
{
    std::vector<size_t> col_sizes;
    for (const auto& h : headers)
    {
        col_sizes.push_back(h.size());
    }

    auto num_dashes = std::accumulate(col_sizes.begin(), col_sizes.end(), 0) + (col_sizes.size() * 4) + 10;

    for (size_t i = 0; i < row_strings.size(); ++i)
    {
        for (size_t j = 0; j < row_strings[i].size(); ++j)
        {
            const auto& s = row_strings[i][j];
            auto& size = col_sizes.at(j);
            size = std::max(size, s.size());
        }
    }

    // Give some extra space
    for (auto& size : col_sizes)
    {
        size += 4;
    }

    // Begin printing
    std::cout << std::string(num_dashes, '=') << "\n";
    std::cout << "Table: " << table_name << "\n";

    // Print headers
    for (size_t i = 0; i < headers.size(); ++i)
    {
        std::cout << std::setw(col_sizes[i]) << std::left << headers[i];
    }
    std::cout << "\n";

    // Print dashes
    std::cout << std::string(num_dashes, '-') << "\n";

    // Print rows
    if (row_strings.empty())
    {
        std::cout << "(table has no records)\n";
    } else
    {
        for (size_t i = 0; i < row_strings.size(); ++i)
        {
            for (size_t j = 0; j < row_strings[i].size(); ++j)
            {
                const auto& s = row_strings[i][j];
                std::cout << std::setw(col_sizes[j]) << std::left << s;
            }
            std::cout << "\n";
        }
    }

    std::cout << std::endl;
}
} // namespace detail

inline void dumpTable(DatabaseManager* db_mgr, const std::string& table_name)
{
    // TODO cnyce: Can we call sqlite3 directly and simply parse the dumped output on stdout?
    struct SelectedValueUnion
    {
        std::optional<int32_t> i;
        std::optional<uint32_t> I;
        std::optional<int64_t> q;
        std::optional<uint64_t> Q;
        std::optional<double> d;
        std::optional<std::string> s;

        SelectedValueUnion(SqlQuery* query, const Column* col) :
            SelectedValueUnion(query, col->getName(), col->getDataType())
        {
        }

        SelectedValueUnion(SqlQuery* query, const std::string& col_name, const SqlDataType dtype)
        {
            using dt = SqlDataType;
            switch (dtype)
            {
            case dt::int32_t:
                query->select(col_name.c_str(), i);
                break;
            case dt::uint32_t:
                query->select(col_name.c_str(), I);
                break;
            case dt::int64_t:
                query->select(col_name.c_str(), q);
                break;
            case dt::uint64_t:
                query->select(col_name.c_str(), Q);
                break;
            case dt::double_t:
                query->select(col_name.c_str(), d);
                break;
            case dt::string_t:
                query->select(col_name.c_str(), s);
                break;
            default:
                break;
            }
        }

        std::optional<std::string> stringify() const
        {
            std::string out;
            if (!detail::to_string(i, out) && !detail::to_string(I, out) && !detail::to_string(q, out) &&
                !detail::to_string(Q, out) && !detail::to_string(d, out) && !detail::to_string(s, out))
            {
                return std::nullopt;
            }
            return out;
        }
    };

    const auto& schema = db_mgr->getSchema();
    const auto& table = schema.getTable(table_name);
    const auto& columns = table.getColumns();

    std::vector<std::string> headers;
    std::deque<SelectedValueUnion> selects;

    auto query = db_mgr->createQuery(table_name.c_str());
    if (table.getPrimaryKey() == "Id")
    {
        headers.push_back("Id");
        selects.emplace_back(query.get(), "Id", SqlDataType::int32_t);
    }

    for (const auto& col : columns)
    {
        headers.push_back(col->getName());
        selects.emplace_back(query.get(), col.get());
    }

    std::vector<std::vector<std::string>> row_strings;

    auto results = query->getResultSet();
    while (results.getNextRecord())
    {
        std::vector<std::string> col_strings;
        for (const auto& col_result : selects)
        {
            auto s = col_result.stringify();
            if (s.has_value())
            {
                col_strings.push_back(s.value());
            } else
            {
                col_strings.push_back("<NULL>");
            }
        }
        row_strings.emplace_back(std::move(col_strings));
    }

    detail::dumpTable(table_name, headers, row_strings);
}

} // namespace simdb
