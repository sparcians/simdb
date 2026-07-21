// clang-format off

#include "BlobReplayer.hpp"
#include "SimDBTester.hpp"
#include "simdb/apps/AppManager.hpp"
#include "simdb/apps/argos/ArgosCollector.hpp"
#include "simdb/apps/argos/StreamBuffer.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/utils/Demangle.hpp"

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>

TEST_INIT;

/// Call once per test function.
#define TEST_METHOD_INIT simdb::argos::EntryPoint::resetCIDs()

std::mt19937_64& testRng()
{
    static std::mt19937_64 rng(std::random_device{}());
    return rng;
}

void reseedTestRng(uint64_t seed)
{
    testRng().seed(seed);
}

template <typename IntT>
IntT randomInt(const IntT min_inclusive = std::numeric_limits<IntT>::min(),
               const IntT max_inclusive = std::numeric_limits<IntT>::max())
{
    std::uniform_int_distribution<IntT> dist(min_inclusive, max_inclusive);
    return dist(testRng());
}

template <typename FloatT>
double randomFloat(const FloatT min_inclusive = std::numeric_limits<FloatT>::min(),
                   const FloatT max_inclusive = std::numeric_limits<FloatT>::max())
{
    std::uniform_real_distribution<FloatT> dist(min_inclusive, max_inclusive);
    return dist(testRng());
}

bool randomBool()
{
    return randomInt<int>() % 2 == 0;
}

std::string randomString(size_t num_chars = 8)
{
    static constexpr char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                      "abcdefghijklmnopqrstuvwxyz";

    constexpr size_t charset_size = sizeof(charset) - 1; // exclude null terminator

    std::string result;
    result.reserve(num_chars);

    for (size_t i = 0; i < num_chars; ++i)
    {
        result += charset[randomInt<size_t>(0, charset_size - 1)];
    }

    return result;
}

enum class UnsignedEnum : uint32_t { __MIN__ = 0, RED = __MIN__, GREEN = 1, BLUE = 2, __MAX__, __INVALID__ = 100};
enum class SignedEnum   : int32_t  { __MIN__ = -1, RED = __MIN__, GREEN = 0, BLUE = 1, __MAX__, __INVALID__ = 100};

static std::map<UnsignedEnum, std::string> unsigned_enum_map{
    {UnsignedEnum::RED, "RED"},
    {UnsignedEnum::GREEN, "GREEN"},
    {UnsignedEnum::BLUE, "BLUE"},
    {UnsignedEnum::__INVALID__, "INVALID"}
};

static std::map<SignedEnum, std::string> signed_enum_map{
    {SignedEnum::RED, "RED"},
    {SignedEnum::GREEN, "GREEN"},
    {SignedEnum::BLUE, "BLUE"},
    {SignedEnum::__INVALID__, "INVALID"}
};

std::ostream& operator<<(std::ostream& os, const UnsignedEnum e)
{
    const auto iter = unsigned_enum_map.find(e);
    if (iter != unsigned_enum_map.end())
    {
        return os << iter->second;
    }
    return os << static_cast<int32_t>(e);
}

std::ostream& operator<<(std::ostream& os, const SignedEnum e)
{
    const auto iter = signed_enum_map.find(e);
    if (iter != signed_enum_map.end())
    {
        return os << iter->second;
    }
    return os << static_cast<int32_t>(e);
}

template <typename E>
E randomEnum()
{
    switch (randomInt<int>(0, 2))
    {
    case 0:
        return E::RED;
    case 1:
        return E::GREEN;
    default:
        return E::BLUE;
    }
}

template <typename E>
E readEnum(argos_test::ByteBuffer& buf)
{
    using underlying_t = std::underlying_type_t<E>;
    return static_cast<E>(buf.read<underlying_t>());
}

struct MultipleTypes
{
    int32_t i = 0;
    std::string s;
    bool b = false;
    UnsignedEnum ue = UnsignedEnum::RED;
    SignedEnum se = SignedEnum::RED;

    bool operator==(const MultipleTypes& other) const
    {
        return i == other.i && s == other.s && b == other.b && ue == other.ue && se == other.se;
    }

    static MultipleTypes random()
    {
        return {randomInt<int32_t>(), randomString(), randomBool(), randomEnum<UnsignedEnum>(), randomEnum<SignedEnum>()};
    }

    void writeToBuffer(simdb::argos::StreamBuffer& buf) const
    {
        buf.append(i);
        buf.append(s);
        buf.append(b);
        buf.append(ue);
        buf.append(se);
    }

    static MultipleTypes readFromBuffer(argos_test::ByteBuffer& buf, const std::unordered_map<uint32_t, std::string>& strings)
    {
        MultipleTypes val;
        val.i = buf.read<int32_t>();
        const auto sid = buf.read<uint32_t>();
        const auto str_iter = strings.find(sid);
        if (str_iter == strings.end())
        {
            throw simdb::DBException("Unknown TinyString ID during replay: " + std::to_string(sid));
        }
        val.s = str_iter->second;
        val.b = static_cast<bool>(buf.read<uint8_t>());
        val.ue = readEnum<UnsignedEnum>(buf);
        val.se = readEnum<SignedEnum>(buf);
        return val;
    }
};

std::ostream& operator<<(std::ostream& os, const MultipleTypes& val)
{
    return os << "MultipleTypes{i=" << val.i << ",s=" << val.s << ",b=" << std::boolalpha << val.b
              << ",ue=" << val.ue << ",se=" << val.se << "}";
}

using ScalarValue = std::variant<int32_t, std::string, bool, UnsignedEnum, SignedEnum, MultipleTypes>;
using ContigValue = std::vector<MultipleTypes>;
using SparseValue = std::map<uint16_t, MultipleTypes>;
using CollectableValue = std::variant<ScalarValue, ContigValue, SparseValue>;

constexpr size_t kContainerCapacity = 8;

std::ostream& operator<<(std::ostream& os, const ScalarValue& val)
{
    std::visit([&os](const auto& v) { os << v; }, val);
    return os;
}

std::ostream& operator<<(std::ostream& os, const ContigValue& val)
{
    os << "[";
    for (size_t i = 0; i < val.size(); ++i)
    {
        if (i != 0)
        {
            os << ",";
        }
        os << val[i];
    }
    return os << "]";
}

std::ostream& operator<<(std::ostream& os, const SparseValue& val)
{
    os << "{";
    bool first = true;
    for (const auto& [idx, elem] : val)
    {
        if (!first)
        {
            os << ",";
        }
        first = false;
        os << idx << ":" << elem;
    }
    return os << "}";
}

std::ostream& operator<<(std::ostream& os, const CollectableValue& val)
{
    std::visit([&os](const auto& v) { os << v; }, val);
    return os;
}

template <typename ScalarT>
std::vector<char> getScalarBytes(const ScalarT& val, simdb::TinyStrings<>* tiny_strings)
{
    if constexpr (std::is_same_v<ScalarT, std::string>)
    {
        auto sid = tiny_strings->getStringID(val);
        return getScalarBytes(sid, tiny_strings);
    }
    else
    {
        static_assert(std::is_trivial_v<ScalarT> && std::is_standard_layout_v<ScalarT>);
        std::vector<char> bytes;
        simdb::argos::StreamBuffer buf(bytes);
        buf.append(val);
        return bytes;
    }
}

std::vector<char> getScalarBytes(const MultipleTypes& val, simdb::TinyStrings<>* tiny_strings)
{
    std::vector<char> bytes;
    simdb::argos::StreamBuffer buf(bytes, tiny_strings);
    val.writeToBuffer(buf);
    return bytes;
}

std::vector<char> scalarValueToBytes(const ScalarValue& val, simdb::TinyStrings<>* tiny_strings)
{
    return std::visit([&](const auto& v) { return getScalarBytes(v, tiny_strings); }, val);
}

std::unordered_map<uint32_t, std::string> loadStringTable(simdb::DatabaseManager* db_mgr)
{
    std::unordered_map<uint32_t, std::string> strings;
    auto query = db_mgr->createQuery("TinyStringIDs");

    std::string string_value;
    uint32_t string_id = 0;
    query->select("StringValue", string_value);
    query->select("StringID", string_id);

    auto results = query->getResultSet();
    while (results.getNextRecord())
    {
        strings.emplace(string_id, string_value);
    }
    return strings;
}

ScalarValue deserializeScalarValue(const std::string& encoded_type,
                                   argos_test::ByteBuffer& buf,
                                   const std::unordered_map<uint32_t, std::string>& strings)
{
    if (encoded_type == "int")
    {
        return buf.read<int32_t>();
    }
    if (encoded_type == "string")
    {
        const auto sid = buf.read<uint32_t>();
        const auto str_iter = strings.find(sid);
        if (str_iter == strings.end())
        {
            throw simdb::DBException("Unknown TinyString ID during replay: " + std::to_string(sid));
        }
        return str_iter->second;
    }
    if (encoded_type == "bool")
    {
        return static_cast<bool>(buf.read<uint8_t>());
    }
    if (encoded_type == "UnsignedEnum")
    {
        return readEnum<UnsignedEnum>(buf);
    }
    if (encoded_type == "SignedEnum")
    {
        return readEnum<SignedEnum>(buf);
    }
    if (encoded_type == "MultipleTypes")
    {
        return MultipleTypes::readFromBuffer(buf, strings);
    }
    throw simdb::DBException("Unsupported scalar encoded type: " + encoded_type);
}

ScalarValue deserializeScalarPayload(const std::string& encoded_type,
                                     const std::vector<char>& payload,
                                     const std::unordered_map<uint32_t, std::string>& strings)
{
    argos_test::ByteBuffer buf(payload);
    return deserializeScalarValue(encoded_type, buf, strings);
}

void writeMultipleTypesMetadata(simdb::DatabaseManager* db_mgr)
{
    const auto struct_schema_id = db_mgr->INSERT(SQL_TABLE("DataTypeSchemas"), SQL_VALUES("MultipleTypes"))->getId();

    auto node_inserter = db_mgr->prepareINSERT(SQL_TABLE("DataTypeNodes"));
    node_inserter->createRecordWithColValues(struct_schema_id, "i", "int", "");
    node_inserter->createRecordWithColValues(struct_schema_id, "s", "string", "");
    node_inserter->createRecordWithColValues(struct_schema_id, "b", "bool", "");
    node_inserter->createRecordWithColValues(struct_schema_id, "ue", "UnsignedEnum", "");
    node_inserter->createRecordWithColValues(struct_schema_id, "se", "SignedEnum", "");

    auto collected_enums = db_mgr->prepareINSERT(SQL_TABLE("CollectedEnums"));
    auto unsigned_enum_id =
        collected_enums->createRecordWithColValues("UnsignedEnum", simdb::demangle_type<uint32_t>());
    auto signed_enum_id = collected_enums->createRecordWithColValues("SignedEnum", simdb::demangle_type<int32_t>());

    auto enum_members = db_mgr->prepareINSERT(SQL_TABLE("EnumMembers"));
    for (const auto& [e, name] : unsigned_enum_map)
    {
        auto e_str = std::to_string(static_cast<long>(e));
        enum_members->createRecordWithColValues(unsigned_enum_id, name, e_str);
    }

    for (const auto& [e, name] : signed_enum_map)
    {
        auto e_str = std::to_string(static_cast<long>(e));
        enum_members->createRecordWithColValues(signed_enum_id, name, e_str);
    }
}

std::function<ScalarValue(simdb::TinyStrings<>*)> makeRandomScalarValueFn_(size_t palette_index)
{
    switch (palette_index)
    {
    case 0:
        return [](simdb::TinyStrings<>*) { return ScalarValue{randomInt<int32_t>()}; };
    case 1:
        return [](simdb::TinyStrings<>*) { return ScalarValue{randomString()}; };
    case 2:
        return [](simdb::TinyStrings<>*) { return ScalarValue{randomBool()}; };
    case 3:
        return [](simdb::TinyStrings<>*) { return ScalarValue{randomEnum<UnsignedEnum>()}; };
    case 4:
        return [](simdb::TinyStrings<>*) { return ScalarValue{randomEnum<SignedEnum>()}; };
    case 5:
        return [](simdb::TinyStrings<>*) { return ScalarValue{MultipleTypes::random()}; };
    default:
        throw simdb::DBException("Invalid scalar palette index");
    }
}

MultipleTypes deserializeBinValue(const std::vector<char>& payload,
                                     const std::unordered_map<uint32_t, std::string>& strings)
{
    return std::get<MultipleTypes>(deserializeScalarPayload("MultipleTypes", payload, strings));
}

ContigValue contigBinsToValue(const std::vector<std::vector<char>>& bins,
                              const std::unordered_map<uint32_t, std::string>& strings)
{
    ContigValue value;
    value.reserve(bins.size());
    for (const auto& bin : bins)
    {
        value.push_back(deserializeBinValue(bin, strings));
    }
    return value;
}

SparseValue sparseBinsToValue(const std::map<uint16_t, std::vector<char>>& bins,
                              const std::unordered_map<uint32_t, std::string>& strings)
{
    SparseValue value;
    for (const auto& [idx, bin] : bins)
    {
        value.emplace(idx, deserializeBinValue(bin, strings));
    }
    return value;
}

bool collectableHasPayload(const CollectableValue& value)
{
    if (std::holds_alternative<ScalarValue>(value))
    {
        return true;
    }
    if (const auto* contig = std::get_if<ContigValue>(&value))
    {
        return !contig->empty();
    }
    if (const auto* sparse = std::get_if<SparseValue>(&value))
    {
        return !sparse->empty();
    }
    return false;
}

ContigValue makeRandomContig()
{
    const size_t num_bins = randomInt<size_t>(0, kContainerCapacity);
    ContigValue value;
    value.reserve(num_bins);
    for (size_t i = 0; i < num_bins; ++i)
    {
        value.push_back(MultipleTypes::random());
    }
    return value;
}

SparseValue makeRandomSparse()
{
    const size_t num_bins = randomInt<size_t>(0, kContainerCapacity);
    SparseValue value;
    std::set<uint16_t> occupied;
    while (occupied.size() < num_bins)
    {
        occupied.insert(static_cast<uint16_t>(randomInt<size_t>(0, kContainerCapacity - 1)));
    }
    for (const uint16_t idx : occupied)
    {
        value.emplace(idx, MultipleTypes::random());
    }
    return value;
}

struct LiveState
{
    bool closed = false;
    bool has_data = false;
    std::optional<CollectableValue> logical;
    std::optional<CollectableValue> latent;
};

enum class CollectableKind { Scalar, Contig, Sparse };

struct ActiveCollectable
{
    CollectableKind kind = CollectableKind::Scalar;
    std::string encoded_type;
    std::string bin_element_type;
    size_t palette_index = 0;
    simdb::argos::EntryPoint* entry_point = nullptr;
};

void stageCollectable(const ActiveCollectable& active, const CollectableValue& value, simdb::TinyStrings<>* tiny_strings)
{
    switch (active.kind)
    {
    case CollectableKind::Scalar:
    {
        auto bytes = scalarValueToBytes(std::get<ScalarValue>(value), tiny_strings);
        active.entry_point->setScalarValueBytes(std::move(bytes));
        break;
    }
    case CollectableKind::Contig:
    {
        const auto& contig = std::get<ContigValue>(value);
        std::vector<std::vector<char>> bins;
        bins.reserve(contig.size());
        for (const auto& elem : contig)
        {
            bins.push_back(getScalarBytes(elem, tiny_strings));
        }
        active.entry_point->setContigContainerBinBytes(std::move(bins));
        break;
    }
    case CollectableKind::Sparse:
    {
        const auto& sparse = std::get<SparseValue>(value);
        std::map<uint16_t, std::vector<char>> bins;
        for (const auto& [idx, elem] : sparse)
        {
            bins.emplace(idx, getScalarBytes(elem, tiny_strings));
        }
        active.entry_point->setSparseContainerBinBytes(std::move(bins));
        break;
    }
    }
}

namespace {

int randomRoll()
{
    return randomInt<int>(0, 99);
}

bool maybeClose(LiveState& state, simdb::argos::EntryPoint* ep)
{
    if (!state.closed && state.has_data && randomRoll() < 6)
    {
        state.latent = state.logical;
        state.logical.reset();
        state.closed = true;
        state.has_data = false;
        ep->closeRecord();
        return true;
    }
    return false;
}

bool maybeReopen(LiveState& state, const ActiveCollectable& active, simdb::TinyStrings<>* tiny_strings)
{
    if (!state.closed)
    {
        return false;
    }

    if (!(state.latent.has_value() && randomRoll() < 8))
    {
        return true;
    }

    state.closed = false;
    state.logical = state.latent;
    state.has_data = collectableHasPayload(*state.logical);
    stageCollectable(active, *state.logical, tiny_strings);
    return true;
}

struct ActionPolicy
{
    virtual ~ActionPolicy() = default;
    virtual void apply(LiveState& state, const ActiveCollectable& active, simdb::TinyStrings<>* tiny_strings) = 0;
};

class ScalarActionPolicy final : public ActionPolicy
{
public:
    void apply(LiveState& state, const ActiveCollectable& active, simdb::TinyStrings<>* tiny_strings) override
    {
        auto* ep = active.entry_point;

        if (state.closed)
        {
            (void)maybeReopen(state, active, tiny_strings);
            return;
        }

        if (maybeClose(state, ep))
        {
            return;
        }

        if (randomRoll() > 70)
        {
            return;
        }

        const auto scalar = makeRandomScalarValueFn_(active.palette_index)(tiny_strings);
        state.logical = CollectableValue{scalar};
        state.has_data = true;
        stageCollectable(active, *state.logical, tiny_strings);
    }
};

class ContigActionPolicy final : public ActionPolicy
{
public:
    void apply(LiveState& state, const ActiveCollectable& active, simdb::TinyStrings<>* tiny_strings) override
    {
        auto* ep = active.entry_point;

        if (state.closed)
        {
            (void)maybeReopen(state, active, tiny_strings);
            return;
        }

        if (maybeClose(state, ep))
        {
            return;
        }

        const auto roll = randomRoll();
        if (roll > 70)
        {
            return;
        }

        ContigValue contig;
        if (state.logical && std::holds_alternative<ContigValue>(*state.logical))
        {
            contig = std::get<ContigValue>(*state.logical);
        }

        if (!state.has_data || roll < 40)
        {
            contig = makeRandomContig();
        } else if (roll < 75)
        {
            if (contig.size() > 1)
            {
                contig[1] = MultipleTypes::random();
            } else
            {
                contig = makeRandomContig();
            }
        } else if (contig.size() > 1)
        {
            contig.erase(contig.begin() + 1);
        } else
        {
            contig = makeRandomContig();
        }

        state.logical = CollectableValue{std::move(contig)};
        state.has_data = collectableHasPayload(*state.logical);
        stageCollectable(active, *state.logical, tiny_strings);
    }
};

class SparseActionPolicy final : public ActionPolicy
{
public:
    void apply(LiveState& state, const ActiveCollectable& active, simdb::TinyStrings<>* tiny_strings) override
    {
        auto* ep = active.entry_point;

        if (state.closed)
        {
            (void)maybeReopen(state, active, tiny_strings);
            return;
        }

        if (maybeClose(state, ep))
        {
            return;
        }

        const auto roll = randomRoll();
        if (roll > 70)
        {
            return;
        }

        SparseValue sparse;
        if (state.logical && std::holds_alternative<SparseValue>(*state.logical))
        {
            sparse = std::get<SparseValue>(*state.logical);
        }

        if (!state.has_data || roll < 40)
        {
            sparse = makeRandomSparse();
        } else if (roll < 70)
        {
            for (auto& [idx, elem] : sparse)
            {
                (void)idx;
                elem = MultipleTypes::random();
            }
        } else if (sparse.size() > 1)
        {
            auto iter = sparse.begin();
            std::advance(iter, 1);
            sparse.erase(iter);
        } else
        {
            sparse = makeRandomSparse();
        }

        state.logical = CollectableValue{std::move(sparse)};
        state.has_data = collectableHasPayload(*state.logical);
        stageCollectable(active, *state.logical, tiny_strings);
    }
};

class SimulationEngine
{
public:
    void seedInitial(uint16_t cid, CollectableValue value)
    {
        auto& state = live_[cid];
        state.logical = std::move(value);
        state.has_data = collectableHasPayload(*state.logical);
        state.closed = false;
        state.latent.reset();
    }

    void applyRandomAction(const ActiveCollectable& active, simdb::TinyStrings<>* tiny_strings)
    {
        auto& state = live_[active.entry_point->getID()];
        switch (active.kind)
        {
        case CollectableKind::Scalar:
            scalar_policy_.apply(state, active, tiny_strings);
            break;
        case CollectableKind::Contig:
            contig_policy_.apply(state, active, tiny_strings);
            break;
        case CollectableKind::Sparse:
            sparse_policy_.apply(state, active, tiny_strings);
            break;
        }
    }

    const LiveState& at(uint16_t cid) const { return live_.at(cid); }

private:
    std::map<uint16_t, LiveState> live_;
    ScalarActionPolicy scalar_policy_;
    ContigActionPolicy contig_policy_;
    SparseActionPolicy sparse_policy_;
};

} // namespace

struct ScalarSpec
{
    std::string path;
    std::string encoded_type;
};

struct ContainerSpec
{
    std::string path;
    std::string encoded_type;
    std::string bin_element_type;
    size_t capacity = kContainerCapacity;
    bool sparse = false;
};

/// Reconstructs live collectable values while replaying blobs (python DataExtractionHandler style).
class DataExtractionHandler : public argos_test::BlobHandler
{
public:
    explicit DataExtractionHandler(const std::unordered_map<uint16_t, std::string>& encoded_type_by_cid,
                                   const std::unordered_map<uint32_t, std::string>& strings) :
        encoded_type_by_cid_(encoded_type_by_cid),
        strings_(strings)
    {
    }

    void handleScalarClosed(argos_test::BlobContext& context) override
    {
        values_by_cid_.erase(context.current_cid);
    }

    void handleScalarCarried(argos_test::BlobContext&) override {}

    void handleScalarFullDump(argos_test::BlobContext& context, const std::vector<char>& payload) override
    {
        values_by_cid_[context.current_cid] = decodeScalar_(context.current_cid, payload);
    }

    void handleContigContainerClosed(argos_test::BlobContext& context) override
    {
        values_by_cid_.erase(context.current_cid);
    }

    void handleContigContainerCarried(argos_test::BlobContext&) override {}

    void handleContigContainerFullDump(argos_test::BlobContext& context,
                                       const std::vector<std::vector<char>>& bins) override
    {
        values_by_cid_[context.current_cid] = CollectableValue{contigBinsToValue(bins, strings_)};
    }

    void handleContigContainerSwap(argos_test::BlobContext& context, uint16_t bin_idx,
                                   const std::vector<char>& payload) override
    {
        auto& contig = contigAt_(context.current_cid);
        if (bin_idx >= contig.size())
        {
            throw simdb::DBException("Contig swap index out of range");
        }
        contig[bin_idx] = deserializeBinValue(payload, strings_);
    }

    void handleContigContainerMultiSwap(argos_test::BlobContext& context, const std::vector<uint16_t>& indices,
                                        const std::vector<std::vector<char>>& payloads) override
    {
        assert(indices.size() == payloads.size());
        for (size_t i = 0; i < indices.size(); ++i)
        {
            handleContigContainerSwap(context, indices[i], payloads[i]);
        }
    }

    void handleContigContainerArrival(argos_test::BlobContext& context, const std::vector<char>& payload) override
    {
        contigAt_(context.current_cid).push_back(deserializeBinValue(payload, strings_));
    }

    void handleContigContainerDeparture(argos_test::BlobContext& context) override
    {
        auto& contig = contigAt_(context.current_cid);
        if (contig.empty())
        {
            throw simdb::DBException("Contig departure on empty container");
        }
        contig.erase(contig.begin());
    }

    void handleContigContainerBookends(argos_test::BlobContext& context, const std::vector<char>& payload) override
    {
        handleContigContainerDeparture(context);
        handleContigContainerArrival(context, payload);
    }

    void handleContigContainerMimo(argos_test::BlobContext& context, uint8_t depart_count, uint8_t arrive_count,
                                   const std::vector<std::vector<char>>& arrive_payloads) override
    {
        (void)arrive_count;
        for (uint8_t i = 0; i < depart_count; ++i)
        {
            (void)i;
            handleContigContainerDeparture(context);
        }
        for (const auto& payload : arrive_payloads)
        {
            handleContigContainerArrival(context, payload);
        }
    }

    void handleSparseContainerClosed(argos_test::BlobContext& context) override
    {
        values_by_cid_.erase(context.current_cid);
    }

    void handleSparseContainerCarried(argos_test::BlobContext&) override {}

    void handleSparseContainerFullDump(argos_test::BlobContext& context,
                                       const std::map<uint16_t, std::vector<char>>& bins) override
    {
        values_by_cid_[context.current_cid] = CollectableValue{sparseBinsToValue(bins, strings_)};
    }

    void handleSparseContainerExchangedBin(argos_test::BlobContext& context, uint16_t bin_idx,
                                           const std::vector<char>& payload) override
    {
        sparseAt_(context.current_cid)[bin_idx] = deserializeBinValue(payload, strings_);
    }

    void handleSparseContainerMultiSwap(argos_test::BlobContext& context, const std::vector<uint16_t>& indices,
                                        const std::vector<std::vector<char>>& payloads) override
    {
        assert(indices.size() == payloads.size());
        for (size_t i = 0; i < indices.size(); ++i)
        {
            handleSparseContainerExchangedBin(context, indices[i], payloads[i]);
        }
    }

    void handleSparseContainerRemovedBin(argos_test::BlobContext& context, uint16_t bin_idx) override
    {
        sparseAt_(context.current_cid).erase(bin_idx);
    }

    void handleSparseContainerAddedBin(argos_test::BlobContext& context, uint16_t bin_idx,
                                       const std::vector<char>& payload) override
    {
        sparseAt_(context.current_cid)[bin_idx] = deserializeBinValue(payload, strings_);
    }

    void handleSparseContainerMultiRemovedBins(argos_test::BlobContext& context,
                                               const std::vector<uint16_t>& indices) override
    {
        for (const auto bin_idx : indices)
        {
            handleSparseContainerRemovedBin(context, bin_idx);
        }
    }

    const std::unordered_map<uint16_t, CollectableValue>& getValuesByCid() const { return values_by_cid_; }

private:
    CollectableValue decodeScalar_(uint16_t cid, const std::vector<char>& payload) const
    {
        return CollectableValue{deserializeScalarPayload(encoded_type_by_cid_.at(cid), payload, strings_)};
    }

    ContigValue& contigAt_(uint16_t cid)
    {
        auto iter = values_by_cid_.find(cid);
        if (iter == values_by_cid_.end() || !std::holds_alternative<ContigValue>(iter->second))
        {
            throw simdb::DBException("Contig replay state missing for cid " + std::to_string(cid));
        }
        return std::get<ContigValue>(iter->second);
    }

    SparseValue& sparseAt_(uint16_t cid)
    {
        auto iter = values_by_cid_.find(cid);
        if (iter == values_by_cid_.end() || !std::holds_alternative<SparseValue>(iter->second))
        {
            throw simdb::DBException("Sparse replay state missing for cid " + std::to_string(cid));
        }
        return std::get<SparseValue>(iter->second);
    }

    const std::unordered_map<uint16_t, std::string>& encoded_type_by_cid_;
    const std::unordered_map<uint32_t, std::string>& strings_;
    std::unordered_map<uint16_t, CollectableValue> values_by_cid_;
};

/// Compares replayed per-tick state against simulator truth.
class TruthValidationHandler final : public DataExtractionHandler
{
public:
    TruthValidationHandler(const std::unordered_map<uint16_t, std::string>& encoded_type_by_cid,
                           const std::unordered_map<uint32_t, std::string>& strings,
                           const std::map<uint64_t, std::map<uint16_t, CollectableValue>>& truth_by_tick,
                           const std::vector<uint16_t>& tracked_cids) :
        DataExtractionHandler(encoded_type_by_cid, strings),
        truth_by_tick_(truth_by_tick),
        tracked_cids_(tracked_cids)
    {
    }

    void snapshotTick(argos_test::BlobContext& context) override
    {
        const auto tick_truth_iter = truth_by_tick_.find(context.current_tick);
        if (tick_truth_iter == truth_by_tick_.end())
        {
            return;
        }

        const auto& truth_at_tick = tick_truth_iter->second;
        const auto& replayed = getValuesByCid();

        for (const auto cid : tracked_cids_)
        {
            const auto truth_iter = truth_at_tick.find(cid);
            if (truth_iter == truth_at_tick.end())
            {
                continue;
            }

            const auto replay_iter = replayed.find(cid);
            if (replay_iter == replayed.end())
            {
                continue;
            }

            EXPECT_EQUAL(replay_iter->second, truth_iter->second);
        }
    }

private:
    const std::map<uint64_t, std::map<uint16_t, CollectableValue>>& truth_by_tick_;
    const std::vector<uint16_t>& tracked_cids_;
};

class Harness
{
public:
    Harness(const std::string& db_file, size_t heartbeat = 10)
        : db_file_(db_file)
        , heartbeat_(heartbeat)
    {
    }

    void createScalars(size_t count)
    {
        static constexpr struct PaletteEntry
        {
            const char* suffix;
            const char* encoded_type;
        } kPalette[] = {
            {"int", "int"},
            {"string", "string"},
            {"bool", "bool"},
            {"uenum", "UnsignedEnum"},
            {"senum", "SignedEnum"},
            {"struct", "MultipleTypes"},
        };
        constexpr size_t kPaletteSize = sizeof(kPalette) / sizeof(kPalette[0]);

        scalar_specs_.clear();
        scalar_specs_.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            const auto& entry = kPalette[i % kPaletteSize];
            const size_t instance = i / kPaletteSize;

            ScalarSpec spec;
            spec.path = "top.scalar." + std::string(entry.suffix) + "_" + std::to_string(instance);
            spec.encoded_type = entry.encoded_type;
            scalar_specs_.push_back(std::move(spec));
        }
    }

    void createContigs(size_t count)
    {
        contig_specs_.clear();
        contig_specs_.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            ContainerSpec spec;
            spec.path = "top.contig.exhaustive_" + std::to_string(i);
            spec.bin_element_type = "MultipleTypes";
            spec.encoded_type = spec.bin_element_type + "_contig_capacity" + std::to_string(kContainerCapacity);
            spec.capacity = kContainerCapacity;
            spec.sparse = false;
            contig_specs_.push_back(std::move(spec));
        }
    }

    void createSparses(size_t count)
    {
        sparse_specs_.clear();
        sparse_specs_.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            ContainerSpec spec;
            spec.path = "top.sparse.exhaustive_" + std::to_string(i);
            spec.bin_element_type = "MultipleTypes";
            spec.encoded_type = spec.bin_element_type + "_sparse_capacity" + std::to_string(kContainerCapacity);
            spec.capacity = kContainerCapacity;
            spec.sparse = true;
            sparse_specs_.push_back(std::move(spec));
        }
    }

    void runSimulation(uint64_t end_sim_time)
    {
        setupAppManagers_();
        createEntryPoints_();
        openPipelines_();
        simulate_(end_sim_time);
        teardown_();
        writeMultipleTypesMetadata(db_mgr_);
        postSimValidate_();
    }

private:
    CollectableValue makeInitialValue_(const ActiveCollectable& active, simdb::TinyStrings<>* tiny_strings) const
    {
        switch (active.kind)
        {
        case CollectableKind::Scalar:
            return CollectableValue{makeRandomScalarValueFn_(active.palette_index)(tiny_strings)};
        case CollectableKind::Contig:
            return CollectableValue{makeRandomContig()};
        case CollectableKind::Sparse:
            return CollectableValue{makeRandomSparse()};
        }
        throw simdb::DBException("Unknown collectable kind");
    }

    void recordTruth_(uint64_t tick, uint16_t cid, const LiveState& state)
    {
        if (state.closed || !state.logical.has_value())
        {
            return;
        }
        truth_by_tick_[tick][cid] = *state.logical;
    }

    void registerActive_(ActiveCollectable active)
    {
        const auto cid = active.entry_point->getID();
        tracked_cids_.push_back(cid);
        encoded_type_by_cid_.emplace(cid, active.encoded_type);
        bin_element_type_by_cid_.emplace(cid, active.bin_element_type);
        active_.push_back(std::move(active));
    }

    void setupAppManagers_()
    {
        app_mgrs_.registerApp<simdb::argos::ArgosCollector>();
        auto& app_mgr = app_mgrs_.createAppManager(db_file_);
        db_mgr_ = app_mgr.getDatabaseManager();

        app_mgr.enableApp(simdb::argos::ArgosCollector::NAME);
        app_mgrs_.createEnabledApps();

        argos_collector_ = app_mgr.getApp<simdb::argos::ArgosCollector>();
        argos_collector_->setHeartbeat(heartbeat_);
        argos_collector_->timestampWith(&tick_);
        argos_collector_->addClock("root", 1);
    }

    void createEntryPoints_()
    {
        active_.clear();
        tracked_cids_.clear();
        encoded_type_by_cid_.clear();
        bin_element_type_by_cid_.clear();

        for (size_t i = 0; i < scalar_specs_.size(); ++i)
        {
            const auto& spec = scalar_specs_[i];
            ActiveCollectable active;
            active.kind = CollectableKind::Scalar;
            active.encoded_type = spec.encoded_type;
            active.bin_element_type = spec.encoded_type;
            active.palette_index = i % 6;
            active.entry_point = argos_collector_->createScalarCollector(spec.path, "root", spec.encoded_type);
            registerActive_(std::move(active));
        }

        for (const auto& spec : contig_specs_)
        {
            ActiveCollectable active;
            active.kind = CollectableKind::Contig;
            active.encoded_type = spec.encoded_type;
            active.bin_element_type = spec.bin_element_type;
            active.entry_point = argos_collector_->createContainerCollector(spec.path, "root", spec.encoded_type);
            registerActive_(std::move(active));
        }

        for (const auto& spec : sparse_specs_)
        {
            ActiveCollectable active;
            active.kind = CollectableKind::Sparse;
            active.encoded_type = spec.encoded_type;
            active.bin_element_type = spec.bin_element_type;
            active.entry_point = argos_collector_->createContainerCollector(spec.path, "root", spec.encoded_type);
            registerActive_(std::move(active));
        }
    }


    void openPipelines_()
    {
        app_mgrs_.createSchemas();
        app_mgrs_.postInit(0, nullptr);
        app_mgrs_.initializePipelines();
        app_mgrs_.openPipelines();
    }

    void simulate_(uint64_t end_sim_time)
    {
        auto* tiny_strings = argos_collector_->getTinyStrings();
        SimulationEngine engine;

        while (++tick_ <= end_sim_time)
        {
            for (const auto& active : active_)
            {
                const auto cid = active.entry_point->getID();

                if (tick_ == 1)
                {
                    const auto initial = makeInitialValue_(active, tiny_strings);
                    engine.seedInitial(cid, initial);
                    stageCollectable(active, initial, tiny_strings);
                } else
                {
                    engine.applyRandomAction(active, tiny_strings);
                }

                recordTruth_(tick_, cid, engine.at(cid));
            }
        }
    }

    void teardown_()
    {
        app_mgrs_.postSimLoopTeardown();
    }

    void postSimValidate_()
    {
        const auto strings = loadStringTable(db_mgr_);
        argos_test::CollectableRegistry registry(db_mgr_);

        const auto deserialize_bin = [&](uint16_t cid, argos_test::ByteBuffer& buf) -> std::vector<char> {
            const auto start = buf.tell();
            (void)deserializeScalarValue(bin_element_type_by_cid_.at(cid), buf, strings);
            return buf.slice(start, buf.tell());
        };

        TruthValidationHandler handler(encoded_type_by_cid_, strings, truth_by_tick_, tracked_cids_);
        argos_test::BlobIterator iterator(db_mgr_, registry);
        iterator.iterate(handler, deserialize_bin);
    }

    const std::string db_file_;
    const size_t heartbeat_;
    simdb::AppManagers app_mgrs_;
    simdb::argos::ArgosCollector* argos_collector_ = nullptr;
    simdb::DatabaseManager* db_mgr_ = nullptr;
    uint64_t tick_ = 0;
    std::vector<ScalarSpec> scalar_specs_;
    std::vector<ContainerSpec> contig_specs_;
    std::vector<ContainerSpec> sparse_specs_;
    std::vector<ActiveCollectable> active_;
    std::vector<uint16_t> tracked_cids_;
    std::unordered_map<uint16_t, std::string> encoded_type_by_cid_;
    std::unordered_map<uint16_t, std::string> bin_element_type_by_cid_;
    std::map<uint64_t, std::map<uint16_t, CollectableValue>> truth_by_tick_;
};

namespace {

constexpr uint64_t kHarnessSeed = 0xC0FFEEULL;

std::filesystem::path findRepoRoot()
{
    auto path = std::filesystem::current_path();
    for (int depth = 0; depth < 12; ++depth)
    {
        if (std::filesystem::exists(path / "test" / "argos-integ" / "compare.py"))
        {
            return path;
        }
        if (!path.has_parent_path() || path == path.parent_path())
        {
            break;
        }
        path = path.parent_path();
    }
    throw simdb::DBException("Could not locate repo root (test/argos/compare.py)");
}

std::string pythonArgosCmd(const std::filesystem::path& repo_root, const std::string& inner_cmd)
{
    std::ostringstream cmd;
    cmd << "cd " << (repo_root / "test" / "argos-integ").string()
        << " && PYTHONPATH=" << (repo_root / "python" / "argos").string() << " python3 " << inner_cmd;
    return cmd.str();
}

bool compareAvailable(const std::filesystem::path& repo_root)
{
    return std::system(pythonArgosCmd(repo_root, "-c \"from viewer.model.data_retriever import DataRetriever\"").c_str()) == 0;
}

void runCompare(const std::filesystem::path& repo_root, const std::filesystem::path& baseline_db,
                const std::filesystem::path& test_db)
{
    const auto baseline = std::filesystem::absolute(baseline_db).string();
    const auto test = std::filesystem::absolute(test_db).string();
    const auto cmd = pythonArgosCmd(repo_root, "compare.py " + baseline + " " + test);

    const int rc = std::system(cmd.c_str());
    EXPECT_EQUAL(rc, 0);
    if (rc != 0)
    {
        std::ostringstream msg;
        msg << "compare.py failed (exit=" << rc << ") comparing " << baseline_db.filename().string() << " vs "
            << test_db.filename().string() << "\nCommand: " << cmd;
        throw simdb::DBException(msg.str());
    }
}

void runHarness(const std::filesystem::path& db_path, size_t heartbeat, uint64_t end_sim_time)
{
    TEST_METHOD_INIT;
    reseedTestRng(kHarnessSeed);
    std::filesystem::remove(db_path);

    Harness harness(db_path.string(), heartbeat);
    harness.createScalars(10);
    harness.createContigs(10);
    harness.createSparses(10);
    harness.runSimulation(end_sim_time);
}

class LifecycleReplayHandler final : public DataExtractionHandler
{
public:
    LifecycleReplayHandler(uint16_t cid, const std::unordered_map<uint16_t, std::string>& encoded_type_by_cid,
                           const std::unordered_map<uint32_t, std::string>& strings) :
        DataExtractionHandler(encoded_type_by_cid, strings),
        cid_(cid)
    {
    }

    void snapshotTick(argos_test::BlobContext& context) override
    {
        const auto iter = getValuesByCid().find(cid_);
        if (iter == getValuesByCid().end())
        {
            snapshots_[context.current_tick] = std::nullopt;
            return;
        }

        snapshots_[context.current_tick] = std::get<int32_t>(std::get<ScalarValue>(iter->second));
    }

    std::optional<int32_t> valueAt(uint64_t tick) const
    {
        const auto iter = snapshots_.find(tick);
        if (iter == snapshots_.end())
        {
            return std::nullopt;
        }
        return iter->second;
    }

private:
    uint16_t cid_;
    std::map<uint64_t, std::optional<int32_t>> snapshots_;
};

void testCloseCollectLifecycle()
{
    TEST_METHOD_INIT;

    const auto db_path = std::filesystem::current_path() / "lifecycle.db";
    std::filesystem::remove(db_path);

    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::argos::ArgosCollector>();
    auto& app_mgr = app_mgrs.createAppManager(db_path.string());
    auto* db_mgr = app_mgr.getDatabaseManager();
    app_mgr.enableApp(simdb::argos::ArgosCollector::NAME);
    app_mgrs.createEnabledApps();

    auto* collector = app_mgr.getApp<simdb::argos::ArgosCollector>();
    collector->setHeartbeat(10);
    uint64_t tick = 0;
    collector->timestampWith(&tick);
    collector->addClock("root", 1);

    auto* ep = collector->createScalarCollector("top.lifecycle", "root", "int");
    const auto cid = ep->getID();
    auto* tiny_strings = collector->getTinyStrings();

    app_mgrs.createSchemas();
    app_mgrs.postInit(0, nullptr);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    ActiveCollectable active;
    active.kind = CollectableKind::Scalar;
    active.encoded_type = "int";
    active.bin_element_type = "int";
    active.entry_point = ep;

    tick = 100;
    stageCollectable(active, CollectableValue{static_cast<int32_t>(tick)}, tiny_strings);
    tick = 150;
    stageCollectable(active, CollectableValue{static_cast<int32_t>(tick)}, tiny_strings);
    tick = 200;
    ep->closeRecord();
    tick = 250;
    stageCollectable(active, CollectableValue{static_cast<int32_t>(tick)}, tiny_strings);
    tick = 300;
    ep->closeRecord();
    tick = 350;

    app_mgrs.postSimLoopTeardown();

    const auto strings = loadStringTable(db_mgr);
    argos_test::CollectableRegistry registry(db_mgr);
    const std::unordered_map<uint16_t, std::string> encoded_type_by_cid{{cid, "int"}};

    const auto deserialize_bin = [&](uint16_t replay_cid, argos_test::ByteBuffer& buf) -> std::vector<char> {
        const auto start = buf.tell();
        (void)deserializeScalarValue("int", buf, strings);
        (void)replay_cid;
        return buf.slice(start, buf.tell());
    };

    LifecycleReplayHandler handler(cid, encoded_type_by_cid, strings);
    argos_test::BlobIterator iterator(db_mgr, registry);
    iterator.iterate(handler, deserialize_bin);

    const auto expectAbsent = [&](uint64_t at_tick) {
        const auto value = handler.valueAt(at_tick);
        EXPECT_EQUAL(value.has_value(), false);
    };

    const auto expectPresent = [&](uint64_t at_tick, int32_t expected) {
        const auto value = handler.valueAt(at_tick);
        EXPECT_EQUAL(value.has_value(), true);
        EXPECT_EQUAL(value.value(), expected);
    };

    expectPresent(100, 100);
    expectPresent(150, 150);
    expectAbsent(200);
    expectPresent(250, 250);
    expectAbsent(300);
    expectAbsent(350);
}

} // namespace

constexpr uint64_t DEFAULT_END_SIM_TIME = 1000;

void testEverything(uint64_t end_sim_time)
{
    const auto repo_root = findRepoRoot();
    const auto db_dir = std::filesystem::current_path();
    const auto hb1_db = db_dir / "hb1.db";
    const auto hb3_db = db_dir / "hb3.db";
    const auto hb10_db = db_dir / "hb10.db";

    runHarness(hb1_db, 1, end_sim_time);
    runHarness(hb3_db, 3, end_sim_time);
    runHarness(hb10_db, 10, end_sim_time);

    if (!compareAvailable(repo_root))
    {
        throw simdb::DBException(
            "python3 could not import viewer.model.data_retriever; "
            "ensure python3 is on PATH and run from the SimDB repo");
    }

    runCompare(repo_root, hb1_db, hb3_db);
    runCompare(repo_root, hb1_db, hb10_db);
}

int main(int argc, char** argv)
{
    TEST_METHOD_INIT;

    uint64_t end_sim_time = DEFAULT_END_SIM_TIME;
    if (argc > 1)
    {
        end_sim_time = static_cast<uint64_t>(std::stoull(argv[1]));
    }

    testEverything(end_sim_time);
    testCloseCollectLifecycle();

    REPORT_ERROR;
    return ERROR_CODE;
}
