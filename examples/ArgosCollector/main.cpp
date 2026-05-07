#include "RandUtils.hpp"
#include "SimDBTester.hpp"
#include "simdb/apps/AppManager.hpp"
#include "simdb/apps/argos/Collection.hpp"
#include "simdb/apps/argos/DataTypeHierarchy.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <list>
#include <random>
#include <sstream>
#include <string>
#include <vector>

/// This test shows how to use the SimDB data collection system for Argos.
TEST_INIT;

/// Call once per test function.
#define TEST_METHOD_INIT simdb::collection::CollectableBase::resetCIDs()

/// Helper to create test-specific byte traces for debugging.
#define ENABLE_BYTE_TRACER collection.enableByteTracer(__FUNCTION__ + std::string(".trace"), true);

constexpr size_t RUN_TICKS = 1000;

namespace {

#ifndef ARGOS_UI_SMOKE_DEFAULT
#define ARGOS_UI_SMOKE_DEFAULT 0
#endif

simdb::ValidValue<bool> ARGOS_UI_SMOKE_OVERRIDE;

bool shouldRunUiSmoke()
{
    if (ARGOS_UI_SMOKE_OVERRIDE.isValid())
    {
        return ARGOS_UI_SMOKE_OVERRIDE;
    }

    const char* env = std::getenv("ARGOS_UI_SMOKE");
    if (env != nullptr)
    {
        return std::string(env) == "1";
    }
    return ARGOS_UI_SMOKE_DEFAULT != 0;
}

std::mt19937_64& testRng()
{
    static std::mt19937_64 rng(std::random_device{}());
    return rng;
}

template <typename IntT>
IntT randomInt(const IntT min_inclusive, const IntT max_inclusive)
{
    std::uniform_int_distribution<IntT> dist(min_inclusive, max_inclusive);
    return dist(testRng());
}

double randomDouble(const double min_inclusive, const double max_inclusive)
{
    std::uniform_real_distribution<double> dist(min_inclusive, max_inclusive);
    return dist(testRng());
}

bool randomOneIn(const int denominator)
{
    return randomInt<int>(1, denominator) == 1;
}

} // namespace

enum InstType
{
    NO_OP,
    MEM,
    CSR,
    ILLEGAL,
    __N
};

std::ostream& operator<<(std::ostream& os, const InstType type)
{
    switch (type)
    {
    case NO_OP:   os << "NO_OP"; break;
    case MEM:     os << "MEM"; break;
    case CSR:     os << "CSR"; break;
    case ILLEGAL: os << "ILLEGAL"; break;
    case __N:     throw simdb::DBException("Invalid enum");
    }
    return os;
}

class Instruction
{
private:
    uint32_t uid_{nextUID_()};
    InstType type_ = InstType::NO_OP;
    uint64_t opcode_ = 0;
    std::string mnemonic_;
    uint32_t csr_ = 0;
    bool last_inst_ = 0;

    static uint32_t nextUID_()
    {
        static uint32_t uid = 100;
        return uid++;
    }

    template <typename T>
    void compareAndAdvance_(const char*& theirs, const T& mine, simdb::TinyStrings<>* tiny_strings) const
    {
        if constexpr (std::is_enum_v<T>)
        {
            using int_type = std::underlying_type_t<T>;
            if (*reinterpret_cast<const int_type*>(theirs) != mine)
            {
                throw simdb::DBException("Value mismatch");
            }
            theirs += sizeof(int_type);
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            using int_type = uint8_t;
            if (*reinterpret_cast<const int_type*>(theirs) != mine)
            {
                throw simdb::DBException("Value mismatch");
            }
            theirs += sizeof(int_type);
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            using int_type = uint32_t;
            if (*reinterpret_cast<const int_type*>(theirs) != tiny_strings->getStringID(mine))
            {
                throw simdb::DBException("Value mismatch");
            }
            theirs += sizeof(int_type);
        }
        else if constexpr (std::is_trivial_v<T> && std::is_standard_layout_v<T>)
        {
            if (*reinterpret_cast<const T*>(theirs) != mine)
            {
                throw simdb::DBException("Value mismatch");
            }
            theirs += sizeof(T);
        }
        else
        {
            static_assert(simdb::type_traits::always_false_v<T>);
        }
    }

    friend std::ostream& operator<<(std::ostream&, const Instruction&);

public:
    uint32_t getUID() const { return uid_; }
    InstType getType() const { return type_; }
    uint64_t getOpcode() const { return opcode_; }
    const std::string& getMnemonic() const { return mnemonic_; }
    uint32_t getCsr() const { return csr_; }
    bool finishesSim() const { return last_inst_; }

    Instruction(InstType type, uint64_t opcode, const std::string& mnemonic, uint32_t csr = 0, bool last_inst = false)
        : type_(type)
        , opcode_(opcode)
        , mnemonic_(mnemonic)
        , csr_(csr)
        , last_inst_(last_inst)
    {}

    Instruction(const Instruction&) = default;
    Instruction& operator=(const Instruction&) = default;

    static Instruction* newRandom()
    {
        auto type = static_cast<InstType>(randomInt<int>(0, static_cast<int>(InstType::__N) - 1));
        auto opcode = randomInt<uint64_t>(0, std::numeric_limits<uint32_t>::max());

        static const char* mnemonics[] = {
            "add", "addi", "li", "b", "jlr"
        };
        auto mnemonic = mnemonics[randomInt<int>(0, 4)];
        auto csr = type == InstType::CSR ? randomInt<uint32_t>(0, 255) : 0;
        auto last_inst = randomOneIn(1000);
        return new Instruction(type, opcode, mnemonic, csr, last_inst);
    }

    static std::shared_ptr<Instruction> genRandom()
    {
        return std::shared_ptr<Instruction>(newRandom());
    }

    static size_t getFixedNumBytes()
    {
        size_t bytes = 0;

        //InstType type_ = InstType::NO_OP;
        bytes += sizeof(std::underlying_type_t<InstType>);

        //uint64_t opcode_ = 0;
        bytes += sizeof(uint64_t);

        //std::string mnemonic_;
        //  --> strings as uint32_t in the DB (TinyStrings)
        bytes += sizeof(uint32_t);

        //uint32_t csr_ = 0;
        bytes += sizeof(uint32_t);

        //bool last_inst_ = 0;
        //  --> bools as uint8_t in the DB
        bytes += sizeof(uint8_t);

        return bytes;
    }

    void randomize()
    {
        *this = *genRandom();
    }

    class ArgosCollector : public simdb::collection::ArgosCollectorBase<Instruction>
    {
    public:
        ARGOS_COLLECT(uid,      &Instruction::getUID, "Unique ID");
        ARGOS_COLLECT(type,     &Instruction::getType, "Instruction type");
        ARGOS_COLLECT(opcode,   &Instruction::getOpcode, "Opcode", simdb::collection::HEX);
        ARGOS_COLLECT(mnemonic, &Instruction::getMnemonic, "Mnemonic");
        ARGOS_COLLECT(csr,      &Instruction::getCsr, "CSR number");
        ARGOS_COLLECT(last,     &Instruction::finishesSim, "Last instruction");
        ARGOS_COLOR_KEY(uid);
    };

    void compare(const char*& bytes, simdb::TinyStrings<>* tiny_strings) const
    {
        compareAndAdvance_(bytes, type_, tiny_strings);
        compareAndAdvance_(bytes, opcode_, tiny_strings);
        compareAndAdvance_(bytes, mnemonic_, tiny_strings);
        compareAndAdvance_(bytes, csr_, tiny_strings);
        compareAndAdvance_(bytes, last_inst_, tiny_strings);
    }
};

class InstGroup
{
public:
    class ArgosContainerCollector : public simdb::collection::ArgosFilteredCollector<Instruction>
    {
    public:
        ARGOS_FILTER(type, opcode, csr, last);
    };

    using iterator = typename std::vector<std::shared_ptr<Instruction>>::iterator;
    using const_iterator = typename std::vector<std::shared_ptr<Instruction>>::const_iterator;

    using size_type = size_t;
    using value_type = std::shared_ptr<Instruction>;

    explicit InstGroup(size_t count) : count_(count) {}

    iterator begin() { return insts_.begin(); }

    iterator end() { return insts_.end(); }

    const_iterator begin() const { return insts_.begin(); }

    const_iterator end() const { return insts_.end(); }

    void randomize()
    {
        insts_.resize(count_);
        for (auto& inst : insts_)
        {
            if (randomOneIn(8))
            {
                inst = Instruction::genRandom();
            }
            else
            {
                inst.reset();
            }
        }
    }

private:
    const size_t count_;
    std::vector<std::shared_ptr<Instruction>> insts_;
};

std::ostream& operator<<(std::ostream& os, const Instruction& inst)
{
    os << "itype: "     << inst.getType() << ", ";
    os << "opcode: "    << inst.getOpcode() << ", ";
    os << "mnemonic: "  << inst.getMnemonic() << ", ";
    os << "csr: "       << inst.getCsr() << ", ";
    os << "last inst: " << inst.finishesSim() << std::endl;
    return os;
}

struct AllData
{
    uint32_t pod;
    std::string str;
    InstType itype;
    bool flag;
    std::shared_ptr<Instruction> inst;
};

const char* instTypeToString(InstType t)
{
    switch (t)
    {
    case InstType::NO_OP:
        return "NO_OP";
    case InstType::MEM:
        return "MEM";
    case InstType::CSR:
        return "CSR";
    case InstType::ILLEGAL:
        return "ILLEGAL";
    default:
        return "UNKNOWN";
    }
}

void DumpCollection(simdb::DatabaseManager* db_mgr, const std::string& dump_file = "")
{
    const auto db_path = db_mgr->getDatabaseFilePath();

    std::ostringstream cmd;
    cmd << "python3 ./dump.py --db-file " << std::quoted(db_path);

    if (!dump_file.empty())
    {
        cmd << " --dump-file " << dump_file;
        cmd << " --append-dump-file";
    }

    const auto rc = std::system(cmd.str().c_str());
    EXPECT_EQUAL(rc, 0);
}

void CompareByteTraceWithPython(
    simdb::DatabaseManager* db_mgr,
    const std::string& sim_trace_file,
    const std::string& ui_trace_file)
{
    const auto db_path = db_mgr->getDatabaseFilePath();

    std::ostringstream cmd;
    cmd << "python3 ./trace_compare.py";
    cmd << " --db-file " << std::quoted(db_path);
    cmd << " --sim-trace-file " << std::quoted(sim_trace_file);
    cmd << " --ui-trace-file " << std::quoted(ui_trace_file);

    const auto rc = std::system(cmd.str().c_str());
    EXPECT_EQUAL(rc, 0);
}

void CompareValuesWithPython(
    simdb::DatabaseManager* db_mgr,
    const std::string& sim_trace_file)
{
    const auto db_path = db_mgr->getDatabaseFilePath();

    std::ostringstream cmd;
    cmd << "python3 ./value_compare.py";
    cmd << " --db-file " << std::quoted(db_path);
    cmd << " --sim-trace-file " << std::quoted(sim_trace_file);
    cmd << " --allow-sim-only-records";

    const auto rc = std::system(cmd.str().c_str());
    EXPECT_EQUAL(rc, 0);
}

void MaybeRunUiSmokeTest(simdb::DatabaseManager* db_mgr, const bool expect_pass = true)
{
    if (!shouldRunUiSmoke())
    {
        return;
    }

    const auto db_path = db_mgr->getDatabaseFilePath();
    std::ostringstream cmd;
    cmd << "python3 ./ui_smoke.py";
    cmd << " --db-file " << std::quoted(db_path);
    cmd << " --timeout-ms 1000";

    const auto rc = std::system(cmd.str().c_str());
    if (expect_pass)
    {
        EXPECT_EQUAL(rc, 0);
    }
    else
    {
        EXPECT_NOTEQUAL(rc, 0);
    }
}

void PostTestValidate(
    const std::string& test_name,
    simdb::DatabaseManager* db_mgr,
    const simdb::collection::CollectionBase& collection,
    bool compare_bytes = false,
    bool compare_values = false,
    bool expect_all_minifier_actions = true)
{
    DumpCollection(db_mgr);
    if (expect_all_minifier_actions)
    {
        EXPECT_TRUE(collection.minifiersSawAllActions());
    }

    if (compare_bytes)
    {
        CompareByteTraceWithPython(
            db_mgr,
            test_name + ".trace",
            test_name + ".ui.trace");
    }

    if (compare_values)
    {
        CompareValuesWithPython(db_mgr, test_name + ".trace");
    }

    MaybeRunUiSmokeTest(db_mgr);
}

#define POST_TEST_VALIDATE(db_mgr, collection, ...) \
    PostTestValidate(__FUNCTION__, db_mgr, collection, ##__VA_ARGS__)

void TestScalarCollection()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    size_t heartbeat = 3;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
    ENABLE_BYTE_TRACER
    collection.timestampWith(&tick);
    collection.addCollection("root", 1);

    // Collect a variety of scalars:
    //   - PODs
    //   - strings
    //   - enums
    //   - bools
    //   - structs

    auto pod_collector = collection.collectScalarManually<uint32_t>(
        "pod", "root");

    auto str_collector = collection.collectScalarManually<std::string>(
        "str", "root");

    auto enum_collector = collection.collectScalarManually<InstType>(
        "itype", "root");

    auto bool_collector = collection.collectScalarManually<bool>(
        "flag", "root");

    auto inst_collector = collection.collectScalarManually<Instruction>(
        "inst", "root");

    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::collection::CollectionPipeline>();

    auto& app_mgr = app_mgrs.createAppManager("test.db");
    app_mgr.enableApp<simdb::collection::CollectionPipeline>();

    app_mgr.parameterizeAppFactory<simdb::collection::CollectionPipeline>(&collection);
    app_mgrs.createEnabledApps();
    app_mgrs.createSchemas();
    app_mgrs.postInit(0, nullptr);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    std::vector<AllData> all_data = {
        {4u, "foo", InstType::MEM,     true,  Instruction::genRandom()},
        {5u, "bar", InstType::NO_OP,   false, Instruction::genRandom()},
        {6u, "fiz", InstType::MEM,     true,  Instruction::genRandom()},
        {7u, "biz", InstType::ILLEGAL, false, Instruction::genRandom()},
        {8u, "fuz", InstType::ILLEGAL, true,  Instruction::genRandom()},
        {9u, "buz", InstType::CSR,     false, Instruction::genRandom()}
    };
    // Force at least one CARRY for the struct minifier in this test:
    // tick 2 collects the same Instruction bytes as tick 1.
    all_data[1].inst = all_data[0].inst;

    for (tick = 1; tick <= all_data.size(); ++tick)
    {
        auto idx = tick - 1;
        pod_collector->collect(all_data[idx].pod);
        str_collector->collect(all_data[idx].str);
        enum_collector->collect(all_data[idx].itype);
        bool_collector->collect(all_data[idx].flag);
        inst_collector->collect(all_data[idx].inst);
    }

    // Deterministically force a FULL then CARRY for struct minifier coverage.
    // (Same tick is fine; PipelineStager deduplicates DB rows by CID, but minifier
    // still sees both collect() calls.)
    tick = 100;
    inst_collector->collect(all_data.back().inst);
    inst_collector->collect(all_data.back().inst);

    app_mgrs.postSimLoopTeardown();
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection, true, true);
}

void TestEnabledLogic()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    size_t heartbeat = 3;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
    ENABLE_BYTE_TRACER
    collection.timestampWith(&tick);
    collection.addCollection("root", 1);

    // The collection system does not know about the difference between
    // automatically-collected (backpointer) and manually-collected data.
    // To test enable/disable logic, we will use two auto-collectors and
    // enable/disable each of them appropriately to test all logic edge
    // cases.
    int32_t val1 = 0;
    auto val1_collector = collection.collectScalarWithAutoCollection<int32_t>(
        "val1", "root", &val1);

    int32_t val2 = 0;
    auto val2_collector = collection.collectScalarWithAutoCollection<int32_t>(
        "val2", "root", &val2);

    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::collection::CollectionPipeline>();

    auto& app_mgr = app_mgrs.createAppManager("test.db");
    app_mgr.enableApp<simdb::collection::CollectionPipeline>();

    app_mgr.parameterizeAppFactory<simdb::collection::CollectionPipeline>(&collection);
    app_mgrs.createEnabledApps();
    app_mgrs.createSchemas();
    app_mgrs.postInit(0, nullptr);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    // Tick 1
    // val1: collect 4
    // val2: collect 5
    tick = 1;
    val1 = 4;
    val2 = 5;
    collection.performAutoCollection("root");

    // Tick 2
    // val1: collect 5
    // val2: disable (re-enable within this heartbeat interval)
    tick = 2;
    val1 = 5;
    val2_collector->disable();
    collection.performAutoCollection("root");

    // Tick 3
    // val1: collect 6
    // val2: re-enable and collect same value as before
    tick = 3;
    val1 = 6;
    val2_collector->enable();
    collection.performAutoCollection("root");

    // Tick 4
    // val1: collect 7
    // val2: disable (re-enable in next heartbeat interval)
    tick = 4;
    val1 = 7;
    val2_collector->disable();
    collection.performAutoCollection("root");

    // Ticks 5-8
    // val1: collect 8,9,10,11
    // val2: leave disabled
    while (++tick < 9)
    {
        ++val1;
        collection.performAutoCollection("root");
    }

    // Tick 9
    // val1: collect 12
    // val2: re-enable and collect same value as before
    tick = 9;
    val1 = 12;
    val2_collector->enable();
    collection.performAutoCollection("root");

    // Tick 10
    // val1: collect 13
    // val2: disable (re-enable within this heartbeat interval)
    tick = 10;
    val1 = 13;
    val2_collector->disable();
    collection.performAutoCollection("root");

    // Tick 11
    // val1: collect 14
    // val2: re-enable and collect different value as before (was 5, now 6)
    tick = 11;
    val1 = 14;
    val2 = 6;
    val2_collector->enable();
    collection.performAutoCollection("root");

    // Tick 12
    // val1: collect 15
    // val2: disable (re-enable in next heartbeat interval)
    tick = 12;
    val1 = 15;
    val2_collector->disable();
    collection.performAutoCollection("root");

    // Ticks 13-16
    // val1: collect 16,17,18,19
    // val2: leave disabled
    while (++tick < 17)
    {
        ++val1;
        collection.performAutoCollection("root");
    }

    // Tick 17
    // val1: collect 20
    // val2: re-enable and collect different value as before (was 6, now 7)
    tick = 17;
    val1 = 20;
    val2 = 7;
    val2_collector->enable();
    collection.performAutoCollection("root");

    app_mgrs.postSimLoopTeardown();
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection, true, true);
}

void TestQuietLogic()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    size_t heartbeat = 3;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
    ENABLE_BYTE_TRACER
    collection.timestampWith(&tick);
    collection.addCollection("root", 1);

    auto val1_collector = collection.collectScalarManually<int32_t>("val1", "root");
    auto val2_collector = collection.collectScalarManually<int32_t>("val2", "root");

    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::collection::CollectionPipeline>();

    auto& app_mgr = app_mgrs.createAppManager("test.db");
    app_mgr.enableApp<simdb::collection::CollectionPipeline>();

    app_mgr.parameterizeAppFactory<simdb::collection::CollectionPipeline>(&collection);
    app_mgrs.createEnabledApps();
    app_mgrs.createSchemas();
    app_mgrs.postInit(0, nullptr);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    // Tick 1: both collected to establish last-seen bytes.
    tick = 1;
    val1_collector->collect(10);
    val2_collector->collect(20);

    // Tick 2: quiet val2 so heartbeat refreshes stop.
    tick = 2;
    val2_collector->quiet();
    val1_collector->collect(11);

    // Ticks 3-4: only val1 updates; val2 should stay quiet.
    tick = 3;
    val1_collector->collect(12);

    tick = 4;
    val1_collector->collect(13);

    // Tick 5: explicit collect on val2 should implicitly awaken.
    tick = 5;
    val1_collector->collect(14);
    val2_collector->collect(21);

    app_mgrs.postSimLoopTeardown();
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection, false, true);
}

void TestMultiClock()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    size_t heartbeat = 3;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
    ENABLE_BYTE_TRACER
    collection.timestampWith(&tick);
    collection.addCollection("root", 1); // Root clock, period 1
    collection.addCollection("clk2", 2); // Another clock, period 2

    auto root_pod = collection.collectScalarManually<uint32_t>(
        "pod1", "root");

    auto clk2_pod = collection.collectScalarManually<uint32_t>(
        "pod2", "clk2");

    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::collection::CollectionPipeline>();

    auto& app_mgr = app_mgrs.createAppManager("test.db");
    app_mgr.enableApp<simdb::collection::CollectionPipeline>();

    app_mgr.parameterizeAppFactory<simdb::collection::CollectionPipeline>(&collection);
    app_mgrs.createEnabledApps();
    app_mgrs.createSchemas();
    app_mgrs.postInit(0, nullptr);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    for (tick = 1; tick <= 100; ++tick)
    {
        root_pod->collect(randomInt<uint32_t>(0, std::numeric_limits<uint32_t>::max()));
        collection.sendCollectedDataToPipeline("root");

        if (tick % 2 == 0)
        {
            clk2_pod->collect(randomInt<uint32_t>(0, std::numeric_limits<uint32_t>::max()));
            collection.sendCollectedDataToPipeline("clk2");
        }
    }

    app_mgrs.postSimLoopTeardown();
}

// Thic class is used to ensure we can collect from non-standard smart pointers.
template <typename T>
class SharedPtr
{
public:
    explicit SharedPtr(T* obj = nullptr)
        : obj_(obj), ref_count_(obj ? new size_t(1) : nullptr) {}

    ~SharedPtr() { release_(); }

    SharedPtr(const SharedPtr& rhs)
        : obj_(rhs.obj_), ref_count_(rhs.ref_count_)
    {
        if (ref_count_)
        {
            ++(*ref_count_);
        }
    }

    SharedPtr& operator=(const SharedPtr& rhs)
    {
        if (this != &rhs)
        {
            release_();
            obj_ = rhs.obj_;
            ref_count_ = rhs.ref_count_;
            if (ref_count_) ++(*ref_count_);
        }
        return *this;
    }

    const T& operator*()  const { assert(obj_); return *obj_; }
    const T* operator->() const { assert(obj_); return obj_; }
    const T* get() const { return obj_; }

    T& operator*() { assert(obj_); return *obj_; }
    T* operator->() { assert(obj_); return obj_; }
    T* get() { return obj_; }

    explicit operator bool() const noexcept { return obj_ != nullptr; }

    bool operator==(std::nullptr_t) const noexcept { return obj_ == nullptr; }
    bool operator!=(std::nullptr_t) const noexcept { return obj_ != nullptr; }

    void reset(T* obj = nullptr)
    {
        release_();
        obj_ = obj;
        ref_count_ = obj ? new size_t(1) : nullptr;
    }

private:
    void release_()
    {
        if (ref_count_ && --(*ref_count_) == 0)
        {
            delete obj_;
            delete ref_count_;
        }
    }

    T* obj_ = nullptr;
    size_t* ref_count_ = nullptr;
};

namespace simdb::type_traits {

template <typename T>
struct is_any_pointer<SharedPtr<T>> : public std::true_type
{
};

template <typename T>
struct is_any_pointer<SharedPtr<T> const> : public std::true_type
{
};

template <typename T>
struct is_any_pointer<SharedPtr<T>&> : public std::true_type
{
};

template <typename T>
struct is_any_pointer<SharedPtr<T> const&> : public std::true_type
{
};

template <typename T>
struct remove_any_pointer<SharedPtr<T>>
{
    using type = T;
};

template <typename T>
struct remove_any_pointer<SharedPtr<T> const>
{
    using type = T;
};

template <typename T>
struct remove_any_pointer<SharedPtr<T>&>
{
    using type = T;
};

template <typename T>
struct remove_any_pointer<SharedPtr<T> const&>
{
    using type = T;
};

} // namespace simdb::type_traits

namespace simdb::collection::detail {
    template <typename T>
    struct argos_struct_nested_type<SharedPtr<T>>
    {
        using type = std::remove_cv_t<T>;
    };

    template <typename T>
    struct argos_struct_nested_type<const SharedPtr<T>>
    {
        using type = std::remove_cv_t<T>;
    };

    template <typename T>
    struct is_smart_pointer<SharedPtr<T>> : std::true_type {};
}

class Unit
{
    uint32_t uid_{nextUID_()};
    uint64_t foo_ = 0;
    double bar_ = 0;
    SharedPtr<Instruction> inst_{Instruction::newRandom()};

    static uint32_t nextUID_()
    {
        static uint32_t uid = UINT32_MAX - 100;
        return uid--;
    }

public:
    uint32_t getUID() const { return uid_; }
    uint64_t getFoo() const { return foo_; }
    double getBar() const { return bar_; }
    SharedPtr<Instruction> getInstPtr() const { return inst_; }

    void randomize()
    {
        foo_ = randomInt<uint64_t>(0, std::numeric_limits<uint32_t>::max());
        bar_ = randomDouble(0.0, static_cast<double>(std::numeric_limits<int32_t>::max()) * M_PI);
        inst_.reset(Instruction::newRandom());
    }

    class ArgosCollector : public simdb::collection::ArgosCollectorBase<Unit>
    {
        ARGOS_COLLECT(uid, &Unit::getUID);
        ARGOS_COLLECT(foo, &Unit::getFoo);
        ARGOS_COLLECT(bar, &Unit::getBar);
        ARGOS_FLATTEN(     &Unit::getInstPtr);
    };
};

void TestFlatten()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    size_t heartbeat = 3;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
    ENABLE_BYTE_TRACER
    collection.timestampWith(&tick);
    collection.addCollection("root", 1);

    auto collector = collection.collectScalarManually<Unit>("some_unit", "root");

    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::collection::CollectionPipeline>();

    auto& app_mgr = app_mgrs.createAppManager("test.db");
    app_mgr.enableApp<simdb::collection::CollectionPipeline>();

    app_mgr.parameterizeAppFactory<simdb::collection::CollectionPipeline>(&collection);
    app_mgrs.createEnabledApps();
    app_mgrs.createSchemas();
    app_mgrs.postInit(0, nullptr);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    Unit unit;
    for (tick = 1; tick <= 100; ++tick)
    {
        unit.randomize();
        collector->collect(unit);
    }

    // Force one FULL->CARRY pair for struct minifier coverage in this test.
    tick = 101;
    collector->collect(unit);
    collector->collect(unit);

    app_mgrs.postSimLoopTeardown();
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection, true);
}

void TestContainers()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    size_t heartbeat = 100;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
    ENABLE_BYTE_TRACER
    collection.timestampWith(&tick);
    collection.addCollection("root", 1);

    constexpr size_t capacity = 32;

    using ContigQ = std::vector<std::shared_ptr<Instruction>>;
    ContigQ contig_q;
    auto contig_collector = collection.collectContainerWithAutoCollection<ContigQ, false>(
        "contig", "root", &contig_q, capacity);

    using SparseQ = std::vector<SharedPtr<Instruction>>;
    SparseQ sparse_q;
    auto sparse_collector = collection.collectContainerWithAutoCollection<SparseQ, true>(
        "sparse", "root", &sparse_q, capacity);

    InstGroup inst_group(capacity);
    auto inst_group_collector = collection.collectContainerWithAutoCollection<InstGroup, true>(
        "instgroup_filtered", "root", &inst_group, capacity);

    inst_group_collector->disableActionTracking();

    auto randomize = [&]()
    {
        auto contig_count = randomInt<size_t>(0, capacity);
        contig_q.clear();
        while (contig_q.size() < contig_count)
        {
            contig_q.push_back(Instruction::genRandom());
        }

        sparse_q.clear();
        sparse_q.resize(capacity);
        for (auto& item : sparse_q)
        {
            if (randomOneIn(8))
            {
                item = SharedPtr<Instruction>(Instruction::newRandom());
            }
        }
        inst_group.randomize();
    };

    auto collect = [&]()
    {
        collection.performAutoCollection("root");
    };

    auto step = [&]()
    {
        randomize();
        collect();
    };

    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::collection::CollectionPipeline>();

    auto& app_mgr = app_mgrs.createAppManager("test.db");
    app_mgr.enableApp<simdb::collection::CollectionPipeline>();

    app_mgr.parameterizeAppFactory<simdb::collection::CollectionPipeline>(&collection);
    app_mgrs.createEnabledApps();
    app_mgrs.createSchemas();
    app_mgrs.postInit(0, nullptr);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    size_t NUM_TICKS = 50;
    for (tick = 1; tick <= NUM_TICKS; ++tick)
    {
        step();
    }

    // Deterministic action-coverage tail.
    auto collect_next_tick = [&]()
    {
        ++tick;
        collect();
    };

    // Baseline FULL
    contig_q.clear();
    contig_q.push_back(Instruction::genRandom());
    contig_q.push_back(Instruction::genRandom());
    contig_q.push_back(Instruction::genRandom());
    sparse_q.clear();
    sparse_q.resize(capacity);
    sparse_q[4] = SharedPtr<Instruction>(Instruction::newRandom());
    sparse_q[7] = SharedPtr<Instruction>(Instruction::newRandom());
    collect_next_tick();

    // CARRY
    collect_next_tick();

    // Contig SWAP + Sparse EXCHANGE
    contig_q[1] = Instruction::genRandom();
    sparse_q[4] = SharedPtr<Instruction>(Instruction::newRandom());
    collect_next_tick();

    // Contig ARRIVE
    contig_q.push_back(Instruction::genRandom());
    collect_next_tick();

    // Contig DEPART + Sparse REMOVE
    contig_q.erase(contig_q.begin());
    sparse_q[7] = SharedPtr<Instruction>();
    collect_next_tick();

    // Contig BOOKENDS
    contig_q.erase(contig_q.begin());
    contig_q.push_back(Instruction::genRandom());
    collect_next_tick();

    app_mgrs.postSimLoopTeardown();
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection, true, true);
}

void TestListContainers()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    constexpr size_t heartbeat = 100;
    constexpr size_t capacity = 16;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
    ENABLE_BYTE_TRACER
    collection.timestampWith(&tick);
    collection.addCollection("root", 1);

    using SharedList = std::list<std::shared_ptr<Instruction>>;
    auto shared_collector = collection.collectContainerManually<SharedList, false>(
        "list_shared_ptr_inst", "root", capacity);

    using ValueList = std::list<Instruction>;
    auto value_collector = collection.collectContainerManually<ValueList, false>(
        "list_inst", "root", capacity);

    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::collection::CollectionPipeline>();

    auto& app_mgr = app_mgrs.createAppManager("test.db");
    app_mgr.enableApp<simdb::collection::CollectionPipeline>();

    app_mgr.parameterizeAppFactory<simdb::collection::CollectionPipeline>(&collection);
    app_mgrs.createEnabledApps();
    app_mgrs.createSchemas();
    app_mgrs.postInit(0, nullptr);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    SharedList shared_list;
    ValueList value_list;

    auto mk_inst = [](InstType type, uint64_t opcode, const std::string& mnemonic, uint32_t csr = 0, bool last = false) {
        return Instruction(type, opcode, mnemonic, csr, last);
    };

    tick = 1;
    shared_list.clear();
    value_list.clear();
    shared_list.push_back(std::make_shared<Instruction>(InstType::NO_OP, 0x101, "addi", 0, false));
    shared_list.push_back(std::make_shared<Instruction>(InstType::MEM, 0x202, "ld", 0, false));
    value_list.push_back(mk_inst(InstType::NO_OP, 0x101, "addi", 0, false));
    value_list.push_back(mk_inst(InstType::MEM, 0x202, "ld", 0, false));
    shared_collector->collect(shared_list);
    value_collector->collect(value_list);

    tick = 2;
    shared_collector->collect(shared_list);
    value_collector->collect(value_list);

    tick = 3;
    shared_list.pop_front();
    shared_list.push_back(std::make_shared<Instruction>(InstType::CSR, 0x303, "csrrw", 12, false));
    value_list.pop_front();
    value_list.push_back(mk_inst(InstType::CSR, 0x303, "csrrw", 12, false));
    shared_collector->collect(shared_list);
    value_collector->collect(value_list);

    EXPECT_TRUE(shared_collector->traceSerializationRootTypeBytes() > 0);
    EXPECT_TRUE(value_collector->traceSerializationRootTypeBytes() > 0);

    app_mgrs.postSimLoopTeardown();
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection, true, true, false);
}

void TestMixedAutoManualLifecycle()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    constexpr size_t heartbeat = 100;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
    ENABLE_BYTE_TRACER
    collection.timestampWith(&tick);
    collection.addCollection("root", 1);

    int32_t manual_scalar = 10;
    auto manual_scalar_collector = collection.collectScalarManually<int32_t>(
        "manual_scalar", "root");

    int32_t auto_scalar = 20;
    auto auto_scalar_collector = collection.collectScalarWithAutoCollection<int32_t>(
        "auto_scalar", "root", &auto_scalar);

    constexpr size_t capacity = 32;
    using ContigQ = std::vector<std::shared_ptr<Instruction>>;
    ContigQ contig_q;
    auto contig_collector = collection.collectContainerWithAutoCollection<ContigQ, false>(
        "contig_mix", "root", &contig_q, capacity);

    using SparseQ = std::vector<SharedPtr<Instruction>>;
    SparseQ sparse_q;
    auto sparse_collector = collection.collectContainerManually<SparseQ, true>(
        "sparse_mix", "root", capacity);

    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::collection::CollectionPipeline>();

    auto& app_mgr = app_mgrs.createAppManager("test.db");
    app_mgr.enableApp<simdb::collection::CollectionPipeline>();

    app_mgr.parameterizeAppFactory<simdb::collection::CollectionPipeline>(&collection);
    app_mgrs.createEnabledApps();
    app_mgrs.createSchemas();
    app_mgrs.postInit(0, nullptr);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    auto collect_next_tick = [&]() {
        ++tick;
        manual_scalar_collector->collect(manual_scalar);
        sparse_collector->collect(sparse_q);
        collection.performAutoCollection("root");
    };

    // Baseline FULL for all collectables.
    contig_q.clear();
    contig_q.push_back(Instruction::genRandom());
    contig_q.push_back(Instruction::genRandom());
    contig_q.push_back(Instruction::genRandom());
    sparse_q.clear();
    sparse_q.resize(capacity);
    sparse_q[4] = SharedPtr<Instruction>(Instruction::newRandom());
    sparse_q[7] = SharedPtr<Instruction>(Instruction::newRandom());
    collect_next_tick();

    // CARRY for all minifiers.
    collect_next_tick();

    // Exercise quiet/awaken on manual scalar.
    manual_scalar_collector->quiet();
    collect_next_tick(); // QUIETED lifecycle
    manual_scalar_collector->collect(manual_scalar); // explicit collect awakens

    // Exercise disable/enable on auto scalar.
    auto_scalar_collector->disable();
    collect_next_tick(); // DISABLED lifecycle
    auto_scalar_collector->enable();

    // Value change after re-enable.
    auto_scalar += 1;
    collect_next_tick(); // ENABLED lifecycle + fresh payload

    // Contig SWAP + Sparse EXCHANGE
    contig_q[1] = Instruction::genRandom();
    sparse_q[4] = SharedPtr<Instruction>(Instruction::newRandom());
    collect_next_tick();

    // Contig ARRIVE
    contig_q.push_back(Instruction::genRandom());
    collect_next_tick();

    // Contig DEPART + Sparse REMOVE
    contig_q.erase(contig_q.begin());
    sparse_q[7] = SharedPtr<Instruction>();
    collect_next_tick();

    // Contig BOOKENDS
    contig_q.erase(contig_q.begin());
    contig_q.push_back(Instruction::genRandom());
    collect_next_tick();

    EXPECT_TRUE(contig_collector->minifierSawAllActions());
    EXPECT_TRUE(sparse_collector->minifierSawAllActions());

    app_mgrs.postSimLoopTeardown();
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection, true);
}

void TestPointers()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    // Large heartbeat so periodic FULL does not mask contig/sparse delta actions (SWAP, REMOVE,
    // etc.) in the deterministic tail; heartbeat 3 was forcing FULL on the same ticks as REMOVE.
    constexpr size_t heartbeat = 100;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
    ENABLE_BYTE_TRACER
    collection.timestampWith(&tick);
    collection.addCollection("root", 1);

    using Int = std::shared_ptr<int>;
    auto intval = std::make_shared<int>(4);
    collection.collectScalarWithAutoCollection<Int>(
        "int", "root", &intval);

    using Inst = SharedPtr<Instruction>;
    auto inst = SharedPtr<Instruction>(Instruction::newRandom());
    collection.collectScalarWithAutoCollection<Inst>(
        "inst", "root", &inst);

    constexpr size_t capacity = 32;

    using ContigQ = std::shared_ptr<std::vector<std::shared_ptr<Instruction>>>;
    ContigQ contig_q(new std::vector<std::shared_ptr<Instruction>>);
    collection.collectContainerWithAutoCollection<ContigQ, false>(
        "contig", "root", &contig_q, capacity);

    using SparseQ = SharedPtr<std::vector<SharedPtr<Instruction>>>;
    SparseQ sparse_q(new std::vector<SharedPtr<Instruction>>);
    collection.collectContainerWithAutoCollection<SparseQ, true>(
        "sparse", "root", &sparse_q, capacity);

    auto randomize = [&]()
    {
        *intval = randomInt<int>(0, std::numeric_limits<int>::max());
        inst->randomize();

        auto contig_count = randomInt<size_t>(0, capacity);
        contig_q->clear();
        while (contig_q->size() < contig_count)
        {
            contig_q->push_back(Instruction::genRandom());
        }

        sparse_q->clear();
        sparse_q->resize(capacity);
        for (auto& item : *sparse_q)
        {
            if (randomOneIn(8))
            {
                item = SharedPtr<Instruction>(Instruction::newRandom());
            }
        }
    };

    auto collect = [&]()
    {
        collection.performAutoCollection("root");
    };

    auto step = [&]()
    {
        randomize();
        collect();
    };

    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::collection::CollectionPipeline>();

    auto& app_mgr = app_mgrs.createAppManager("test.db");
    app_mgr.enableApp<simdb::collection::CollectionPipeline>();

    app_mgr.parameterizeAppFactory<simdb::collection::CollectionPipeline>(&collection);
    app_mgrs.createEnabledApps();
    app_mgrs.createSchemas();
    app_mgrs.postInit(0, nullptr);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    size_t NUM_TICKS = 50;
    for (tick = 1; tick <= NUM_TICKS; ++tick)
    {
        step();
    }

    // Deterministic minifier-action coverage tail for pointer-backed collectables.
    auto collect_next_tick = [&]()
    {
        ++tick;
        collect();
    };

    // Baseline state
    *intval = 123;
    inst = SharedPtr<Instruction>(Instruction::newRandom());
    contig_q->clear();
    contig_q->push_back(Instruction::genRandom());
    contig_q->push_back(Instruction::genRandom());
    contig_q->push_back(Instruction::genRandom());
    sparse_q->clear();
    sparse_q->resize(capacity);
    (*sparse_q)[5] = SharedPtr<Instruction>(Instruction::newRandom());
    (*sparse_q)[9] = SharedPtr<Instruction>(Instruction::newRandom());
    collect_next_tick(); // FULL (heartbeat)

    collect_next_tick(); // CARRY

    // Contig SWAP
    (*contig_q)[1] = Instruction::genRandom();
    collect_next_tick();

    collect_next_tick(); // FULL (heartbeat)

    // Contig ARRIVE
    contig_q->push_back(Instruction::genRandom());
    collect_next_tick();

    // Contig DEPART
    contig_q->erase(contig_q->begin());
    collect_next_tick();

    collect_next_tick(); // FULL (heartbeat)

    // Contig BOOKENDS
    contig_q->erase(contig_q->begin());
    contig_q->push_back(Instruction::genRandom());
    collect_next_tick();

    // Sparse EXCHANGE
    (*sparse_q)[5] = SharedPtr<Instruction>(Instruction::newRandom());
    collect_next_tick();

    // Sparse REMOVE
    (*sparse_q)[9] = SharedPtr<Instruction>();
    collect_next_tick();

    // Sparse minifier must observe REMOVE at least once; a scheduled FULL on the same collect as a
    // multi-slot diff can skip REMOVE counting. Re-seed to a single occupied bin, then clear it.
    std::fill(sparse_q->begin(), sparse_q->end(), SharedPtr<Instruction>());
    (*sparse_q)[0] = SharedPtr<Instruction>(Instruction::newRandom());
    collect_next_tick();
    collect_next_tick(); // CARRY
    (*sparse_q)[0] = SharedPtr<Instruction>();
    collect_next_tick(); // REMOVE (single slot)

    app_mgrs.postSimLoopTeardown();
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection, true);
}

class Outer
{
public:
    class InnerA
    {
    private:
        int val_ = 44;
    public:
        int getValue() const { return val_; }
        class ArgosCollector : public simdb::collection::ArgosCollectorBase<InnerA>
        {
        public:
            ARGOS_COLLECT(valA, &InnerA::getValue);
        };
    };

    class InnerB
    {
        private:
        int val_ = 55;
    public:
        int getValue() const { return val_; }
        class ArgosCollector : public simdb::collection::ArgosCollectorBase<InnerB>
        {
        public:
            ARGOS_COLLECT(valB, &InnerB::getValue);
        };
    };

    std::shared_ptr<InnerA> getA() const { return inner_a_; }
    std::shared_ptr<InnerB> getB() const { return inner_b_; }

    class ArgosCollector : public simdb::collection::ArgosCollectorBase<Outer>
    {
    public:
        ARGOS_FLATTEN(&Outer::getA);
        ARGOS_FLATTEN(&Outer::getB);
    };

private:
    std::shared_ptr<InnerA> inner_a_{std::make_shared<InnerA>()};
    std::shared_ptr<InnerB> inner_b_{std::make_shared<InnerB>()};
};

/// Nested ARGOS_FLATTEN reproducer: wire size for the outer root should include all
/// flattened leaves (Leaves inside Middle inside Top). If \c traceSerializationRootTypeBytes
/// under-counts, \c simdb_collection.trace.meta will show too small a payload and the Python
/// UI can mis-read DB blobs.
class LeavesReproWireBytes
{
    uint32_t foo_ = 1;
    double bar_ = 2.0;

public:
    uint32_t getFoo() const { return foo_; }
    double getBar() const { return bar_; }

    class ArgosCollector : public simdb::collection::ArgosCollectorBase<LeavesReproWireBytes>
    {
    public:
        ARGOS_COLLECT(foo, &LeavesReproWireBytes::getFoo);
        ARGOS_COLLECT(bar, &LeavesReproWireBytes::getBar);
    };
};

class MiddleReproWireBytes
{
    uint16_t mid_ = 3;
    LeavesReproWireBytes leaves_;

public:
    uint16_t getMid() const { return mid_; }
    const LeavesReproWireBytes& getLeaves() const { return leaves_; }

    class ArgosCollector : public simdb::collection::ArgosCollectorBase<MiddleReproWireBytes>
    {
    public:
        ARGOS_COLLECT(mid, &MiddleReproWireBytes::getMid);
        ARGOS_FLATTEN(&MiddleReproWireBytes::getLeaves);
    };
};

class TopOfBugReproWireBytes
{
    float top_ = 4.0F;
    MiddleReproWireBytes middle_;

public:
    float getTop() const { return top_; }
    const MiddleReproWireBytes& getMid() const { return middle_; }

    class ArgosCollector : public simdb::collection::ArgosCollectorBase<TopOfBugReproWireBytes>
    {
    public:
        ARGOS_COLLECT(top, &TopOfBugReproWireBytes::getTop);
        ARGOS_FLATTEN(&TopOfBugReproWireBytes::getMid);
    };
};

/// Regression / lock-down for nested \c ARGOS_FLATTEN wire-size accounting (see trace .meta).
void TestFlattenNestedStructWireBytesRepro()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    simdb::collection::Collection<uint64_t> collection(3);
    collection.timestampWith(&tick);
    collection.addCollection("root", 1);

    auto leaves_col = collection.collectScalarManually<LeavesReproWireBytes>("leaves_repro", "root");
    auto middle_col = collection.collectScalarManually<MiddleReproWireBytes>("middle_repro", "root");
    auto top_col = collection.collectScalarManually<TopOfBugReproWireBytes>("top_of_bug_repro", "root");

    // Expected serialized payload bytes (Argos layout): float + uint16 + uint32 + double.
    EXPECT_EQUAL(leaves_col->traceSerializationRootTypeBytes(), 12u);
    EXPECT_EQUAL(middle_col->traceSerializationRootTypeBytes(), 14u);
    EXPECT_EQUAL(top_col->traceSerializationRootTypeBytes(), 18u);
}

void TestMultiArgosCollectors()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    size_t heartbeat = 3;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
    ENABLE_BYTE_TRACER
    collection.timestampWith(&tick);
    collection.addCollection("root", 1);

    Outer outer1;
    auto a_collector = collection.collectScalarWithAutoCollection<Outer>("a", "root", &outer1);
    auto b_collector = collection.collectScalarManually<Outer>("b", "root");

    SharedPtr<Outer> outer2;
    auto c_collector = collection.collectScalarWithAutoCollection<SharedPtr<Outer>>("c", "root", &outer2);
    auto d_collector = collection.collectScalarManually<SharedPtr<Outer>>("d", "root");

    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::collection::CollectionPipeline>();

    auto& app_mgr = app_mgrs.createAppManager("test.db");
    app_mgr.enableApp<simdb::collection::CollectionPipeline>();

    app_mgr.parameterizeAppFactory<simdb::collection::CollectionPipeline>(&collection);
    app_mgrs.createEnabledApps();
    app_mgrs.createSchemas();
    app_mgrs.postInit(0, nullptr);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    size_t NUM_TICKS = 50;
    outer2 = SharedPtr<Outer>(new Outer());
    for (tick = 1; tick <= NUM_TICKS; ++tick)
    {
        collection.performAutoCollection("root");
    }

    // Explicitly exercise manual collectors with FULL->CARRY each.
    tick = NUM_TICKS + 1;
    b_collector->collect(outer1);
    d_collector->collect(outer2);
    ++tick;
    b_collector->collect(outer1);
    d_collector->collect(outer2);

    EXPECT_TRUE(a_collector->minifierSawAllActions());
    EXPECT_TRUE(b_collector->minifierSawAllActions());
    EXPECT_TRUE(c_collector->minifierSawAllActions());
    EXPECT_TRUE(d_collector->minifierSawAllActions());

    app_mgrs.postSimLoopTeardown();
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection, true);
}

void GenTraceForScalarInts()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    constexpr size_t heartbeat = 10;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
    ENABLE_BYTE_TRACER
    collection.timestampWith(&tick);
    collection.addCollection("root", 1);

    // This collectable is what we focus on in the trace
    auto collector = collection.collectScalarManually<unsigned int>("value", "root");

    // This collectable is used to ensure non-trivial gaps in collected data
    auto always_zero = collection.collectScalarManually<unsigned int>("zero", "root");

    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::collection::CollectionPipeline>();

    auto& app_mgr = app_mgrs.createAppManager("test.db");
    app_mgr.enableApp<simdb::collection::CollectionPipeline>();
    app_mgr.parameterizeAppFactory<simdb::collection::CollectionPipeline>(&collection);
    app_mgrs.createEnabledApps();
    app_mgrs.createSchemas();
    app_mgrs.postInit(0, nullptr);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    tick = 1;
    collector->collect(4);
    always_zero->collect(0);

    tick = 2;
    collector->collect(5);
    always_zero->collect(0);

    tick = 3;
    collector->disable();
    constexpr auto enabled_tick = 50;
    while (tick < enabled_tick)
    {
        always_zero->collect(0);
        ++tick;
    }

    tick = enabled_tick;
    collector->enable();
    always_zero->collect(0);

    tick = 51;
    collector->collect(6);
    always_zero->collect(0);

    tick = 52;
    collector->collect(7);
    always_zero->collect(0);

    tick = 53;
    collector->quiet();
    constexpr auto awakened_tick = 99;
    while (tick < awakened_tick)
    {
        always_zero->collect(0);
        ++tick;
    }

    tick = awakened_tick;
    collector->awaken();
    always_zero->collect(0);

    tick = 100;
    collector->collect(8);
    always_zero->collect(0);

    EXPECT_TRUE(collector->minifierSawAllActions());
    app_mgrs.postSimLoopTeardown();
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection, true);
}

void GenTraceForScalarStructs()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    constexpr size_t heartbeat = 10;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
    ENABLE_BYTE_TRACER
    collection.timestampWith(&tick);
    collection.addCollection("root", 1);

    // Focus collectable: scalar struct via shared_ptr
    auto collector = collection.collectScalarManually<std::shared_ptr<Instruction>>("value", "root");

    // Helper collectable to keep the DB active across long gaps
    auto always_zero = collection.collectScalarManually<unsigned int>("zero", "root");

    auto mk_inst = [](InstType type, uint64_t opcode, const std::string& mnemonic, uint32_t csr, bool last) {
        return std::make_shared<Instruction>(type, opcode, mnemonic, csr, last);
    };

    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::collection::CollectionPipeline>();

    auto& app_mgr = app_mgrs.createAppManager("test.db");
    app_mgr.enableApp<simdb::collection::CollectionPipeline>();
    app_mgr.parameterizeAppFactory<simdb::collection::CollectionPipeline>(&collection);
    app_mgrs.createEnabledApps();
    app_mgrs.createSchemas();
    app_mgrs.postInit(0, nullptr);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    tick = 1;
    auto base = mk_inst(InstType::NO_OP, 0x101, "addi", 0, false);
    collector->collect(base);
    always_zero->collect(0);

    tick = 2;
    collector->collect(base); // CARRY
    always_zero->collect(0);

    tick = 3;
    collector->disable();
    constexpr auto enabled_tick = 50;
    while (tick < enabled_tick)
    {
        always_zero->collect(0);
        ++tick;
    }

    tick = enabled_tick;
    collector->enable();
    always_zero->collect(0);

    tick = 51;
    collector->collect(mk_inst(InstType::CSR, 0x303, "csrrw", 12, false));
    always_zero->collect(0);

    tick = 52;
    collector->collect(mk_inst(InstType::ILLEGAL, 0x404, "bad", 0, true));
    always_zero->collect(0);

    tick = 53;
    collector->quiet();
    constexpr auto awakened_tick = 99;
    while (tick < awakened_tick)
    {
        always_zero->collect(0);
        ++tick;
    }

    tick = awakened_tick;
    collector->awaken();
    always_zero->collect(0);

    tick = 100;
    collector->collect(mk_inst(InstType::MEM, 0x505, "st", 0, false));
    always_zero->collect(0);

    EXPECT_TRUE(collector->minifierSawAllActions());
    app_mgrs.postSimLoopTeardown();
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection, true);
}

void GenTraceForContigContainers()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    constexpr size_t heartbeat = 10;
    constexpr size_t capacity = 8;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
    ENABLE_BYTE_TRACER
    collection.timestampWith(&tick);
    collection.addCollection("root", 1);

    using ContigQ = std::vector<std::shared_ptr<Instruction>>;
    auto collector = collection.collectContainerManually<ContigQ, false>("value", "root", capacity);
    auto always_zero = collection.collectScalarManually<unsigned int>("zero", "root");

    auto mk_inst = [](InstType type, uint64_t opcode, const std::string& mnemonic) {
        return std::make_shared<Instruction>(type, opcode, mnemonic, 0, false);
    };

    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::collection::CollectionPipeline>();

    auto& app_mgr = app_mgrs.createAppManager("test.db");
    app_mgr.enableApp<simdb::collection::CollectionPipeline>();
    app_mgr.parameterizeAppFactory<simdb::collection::CollectionPipeline>(&collection);
    app_mgrs.createEnabledApps();
    app_mgrs.createSchemas();
    app_mgrs.postInit(0, nullptr);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    ContigQ q;

    tick = 1;
    q = {mk_inst(InstType::NO_OP, 0x10, "addi"), mk_inst(InstType::MEM, 0x20, "ld")};
    collector->collect(q);
    always_zero->collect(0);

    tick = 2;
    // Keep identical to force CARRY.
    collector->collect(q);
    always_zero->collect(0);

    tick = 3;
    collector->disable();
    constexpr auto enabled_tick = 50;
    while (tick < enabled_tick)
    {
        always_zero->collect(0);
        ++tick;
    }

    tick = enabled_tick;
    collector->enable();
    always_zero->collect(0);

    tick = 51;
    q[1] = mk_inst(InstType::CSR, 0x21, "csrrs"); // SWAP
    collector->collect(q);
    always_zero->collect(0);

    tick = 52;
    q.push_back(mk_inst(InstType::MEM, 0x30, "st")); // ARRIVE
    collector->collect(q);
    always_zero->collect(0);

    tick = 53;
    q.erase(q.begin()); // DEPART-style
    collector->collect(q);
    always_zero->collect(0);

    tick = 54;
    q.erase(q.begin()); // BOOKENDS = depart front + arrive back
    q.push_back(mk_inst(InstType::NO_OP, 0x31, "addi"));
    collector->collect(q);
    always_zero->collect(0);

    tick = 55;
    collector->quiet();
    constexpr auto awakened_tick = 99;
    while (tick < awakened_tick)
    {
        always_zero->collect(0);
        ++tick;
    }

    tick = awakened_tick;
    collector->awaken();
    always_zero->collect(0);

    tick = 100;
    q = {mk_inst(InstType::ILLEGAL, 0x99, "bad"), mk_inst(InstType::NO_OP, 0x77, "addi")};
    collector->collect(q);
    always_zero->collect(0);

    EXPECT_TRUE(collector->minifierSawAllActions());
    app_mgrs.postSimLoopTeardown();
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection, true);
}

void GenTraceForSparseContainers()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    constexpr size_t heartbeat = 10;
    constexpr size_t capacity = 8;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
    ENABLE_BYTE_TRACER
    collection.timestampWith(&tick);
    collection.addCollection("root", 1);

    using SparseQ = std::vector<SharedPtr<Instruction>>;
    auto collector = collection.collectContainerManually<SparseQ, true>("value", "root", capacity);
    auto always_zero = collection.collectScalarManually<unsigned int>("zero", "root");

    auto mk_inst = [](InstType type, uint64_t opcode, const std::string& mnemonic) {
        return SharedPtr<Instruction>(new Instruction(type, opcode, mnemonic, 0, false));
    };

    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::collection::CollectionPipeline>();

    auto& app_mgr = app_mgrs.createAppManager("test.db");
    app_mgr.enableApp<simdb::collection::CollectionPipeline>();
    app_mgr.parameterizeAppFactory<simdb::collection::CollectionPipeline>(&collection);
    app_mgrs.createEnabledApps();
    app_mgrs.createSchemas();
    app_mgrs.postInit(0, nullptr);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    SparseQ q(capacity);

    tick = 1;
    q[1] = mk_inst(InstType::NO_OP, 0x11, "addi");
    q[5] = mk_inst(InstType::MEM, 0x55, "ld");
    collector->collect(q);
    always_zero->collect(0);

    tick = 2;
    // Keep identical to force CARRY.
    collector->collect(q);
    always_zero->collect(0);

    tick = 3;
    collector->disable();
    constexpr auto enabled_tick = 50;
    while (tick < enabled_tick)
    {
        always_zero->collect(0);
        ++tick;
    }

    tick = enabled_tick;
    collector->enable();
    always_zero->collect(0);

    tick = 51;
    q[5] = mk_inst(InstType::CSR, 0x56, "csrrw"); // EXCHANGE
    collector->collect(q);
    always_zero->collect(0);

    tick = 52;
    q[5] = SharedPtr<Instruction>(); // REMOVE
    collector->collect(q);
    always_zero->collect(0);

    tick = 53;
    q[2] = mk_inst(InstType::ILLEGAL, 0x66, "bad"); // EXCHANGE (insert)
    collector->collect(q);
    always_zero->collect(0);

    tick = 54;
    collector->quiet();
    constexpr auto awakened_tick = 99;
    while (tick < awakened_tick)
    {
        always_zero->collect(0);
        ++tick;
    }

    tick = awakened_tick;
    collector->awaken();
    always_zero->collect(0);

    tick = 100;
    q[1] = mk_inst(InstType::MEM, 0x88, "st");
    collector->collect(q);
    always_zero->collect(0);

    EXPECT_TRUE(collector->minifierSawAllActions());
    app_mgrs.postSimLoopTeardown();
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection, true);
}

void TestUiSmokeRejectsNonArgosDB()
{
    if (std::getenv("DISPLAY") == nullptr && std::getenv("WAYLAND_DISPLAY") == nullptr)
    {
        return;
    }

    simdb::Schema schema;
    using dt = simdb::SqlDataType;
    auto& tbl = schema.addTable("DummyMetadata");
    tbl.addColumn("Name", dt::string_t);
    tbl.addColumn("Value", dt::int32_t);

    simdb::DatabaseManager db_mgr("non_argos_ui_smoke.db", true);
    db_mgr.appendSchema(schema);
    db_mgr.INSERT(SQL_TABLE("DummyMetadata"), SQL_VALUES("foo", 1));

    MaybeRunUiSmokeTest(&db_mgr, false);
}

int main(int argc, char** argv)
{
    if (argc > 1)
    {
        if (std::string(argv[1]) == "fast")
        {
            ARGOS_UI_SMOKE_OVERRIDE = false;
        }
    }

    TestScalarCollection();
    TestEnabledLogic();
    TestQuietLogic();
    TestMultiClock();
    TestFlatten();
    TestFlattenNestedStructWireBytesRepro();
    TestContainers();
    TestListContainers();
    TestMixedAutoManualLifecycle();
    TestPointers();
    TestMultiArgosCollectors();
    TestUiSmokeRejectsNonArgosDB();

    // TODO cnyce: these tests are redundant
    GenTraceForScalarInts();
    GenTraceForScalarStructs();
    GenTraceForContigContainers();
    GenTraceForSparseContainers();

    // TODO cnyce: initial value/bytes
    // TODO cnyce: default disabled
    // TODO cnyce: reattach
    // TODO cnyce: iterators with isValid (non-std::vector)

    REPORT_ERROR;
    return ERROR_CODE;
}
