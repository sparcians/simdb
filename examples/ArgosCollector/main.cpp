#include "RandUtils.hpp"
#include "SimDBTester.hpp"
#include "simdb/apps/AppManager.hpp"
#include "simdb/apps/argos/Collection.hpp"
#include "simdb/apps/argos/DataTypeHierarchy.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

/// This test shows how to use the SimDB data collection system for Argos.
TEST_INIT;

/// Call once per test function.
#define TEST_METHOD_INIT simdb::collection::CollectableBase::resetCIDs()

constexpr size_t RUN_TICKS = 1000;

enum InstType
{
    NO_OP,
    MEM,
    CSR,
    ILLEGAL,
    __N
};

// Template specializations
namespace simdb::collection {

template <>
struct EnumDescriptor<InstType>
{
    static std::vector<EnumMember> members()
    {
        return {{"NO_OP", 0},
                {"MEM", 1},
                {"CSR", 2},
                {"ILLEGAL", 3}};
    }
};

} // namespace simdb::collection

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
        auto type = static_cast<InstType>(rand() % InstType::__N);
        auto opcode = rand();

        static const char* mnemonics[] = {
            "add", "addi", "li", "b", "jlr"
        };
        auto mnemonic = mnemonics[rand() % 5];
        auto csr = type == InstType::CSR ? rand() % 256 : 0;
        auto last_inst = rand() % 1000 == 500;
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
        ARGOS_COLLECT(opcode,   &Instruction::getOpcode, "Opcode");
        ARGOS_COLLECT(mnemonic, &Instruction::getMnemonic, "Mnemonic");
        ARGOS_COLLECT(csr,      &Instruction::getCsr, "CSR number");
        ARGOS_COLLECT(last,     &Instruction::finishesSim, "Last instruction");
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

#define TEST_FILENAME std::string(__FUNCTION__) + ".test.out"
#define GOLDEN_FILENAME std::string(__FUNCTION__) + ".golden.out"

bool CompareFiles(const std::string& f1, const std::string& f2)
{
    std::ifstream file1(f1), file2(f2);

    if (!file1 || !file2) return false;

    std::string line1, line2;

    while (true) {
        bool r1 = static_cast<bool>(std::getline(file1, line1));
        bool r2 = static_cast<bool>(std::getline(file2, line2));

        if (r1 != r2) return false;       // different number of lines
        if (!r1) break;                   // both reached EOF
        if (line1 != line2) return false; // mismatch
    }

    return true;
}

#define TEST_OFSTREAM(varname) std::ofstream varname(TEST_FILENAME)

#define POST_TEST_VALIDATE(db_mgr, collection)                     \
    DumpCollection(db_mgr, TEST_FILENAME);                         \
    if (std::filesystem::exists(GOLDEN_FILENAME)) {                \
        EXPECT_TRUE(CompareFiles(TEST_FILENAME, GOLDEN_FILENAME)); \
    }                                                              \
    EXPECT_TRUE(collection.minifiersSawAllActions());

void TestScalarCollection()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    size_t heartbeat = 3;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
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

    TEST_OFSTREAM(fout);
    for (tick = 1; tick <= all_data.size(); ++tick)
    {
        auto idx = tick - 1;

        fout << "Collecting scalar values at tick " << tick << "\n";
        fout << "... pod: "   << all_data[idx].pod   << "\n";
        fout << "... str: "   << all_data[idx].str   << "\n";
        fout << "... itype: " << all_data[idx].itype << "\n";
        fout << "... flag: "  << all_data[idx].flag  << "\n";
        fout << "... inst: "  << *all_data[idx].inst << "\n";

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
    fout.close();
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection);
}

void TestEnabledLogic()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    size_t heartbeat = 3;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
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
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection);
}

void TestQuietLogic()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    size_t heartbeat = 3;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
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
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection);
}

void TestMultiClock()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    size_t heartbeat = 3;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
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
        root_pod->collect(rand());
        collection.sendCollectedDataToPipeline("root");

        if (tick % 2 == 0)
        {
            clk2_pod->collect(rand());
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
        foo_ = rand();
        bar_ = rand() * M_PI;
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
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection);
}

void TestContainers()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    size_t heartbeat = 100;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
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

    auto randomize = [&]()
    {
        auto contig_count = rand() % (capacity + 1);
        contig_q.clear();
        while (contig_q.size() < contig_count)
        {
            contig_q.push_back(Instruction::genRandom());
        }

        sparse_q.clear();
        sparse_q.resize(capacity);
        for (auto& item : sparse_q)
        {
            if (rand() % 8 == 0)
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
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection);
}

void TestPointers()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    size_t heartbeat = 3;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
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
        *intval = rand();
        inst->randomize();

        auto contig_count = rand() % (capacity + 1);
        contig_q->clear();
        while (contig_q->size() < contig_count)
        {
            contig_q->push_back(Instruction::genRandom());
        }

        sparse_q->clear();
        sparse_q->resize(capacity);
        for (auto& item : *sparse_q)
        {
            if (rand() % 8 == 0)
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

    app_mgrs.postSimLoopTeardown();
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection);
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

void TestMultiArgosCollectors()
{
    TEST_METHOD_INIT;

    uint64_t tick = 0;
    size_t heartbeat = 3;
    simdb::collection::Collection<uint64_t> collection(heartbeat);
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
    POST_TEST_VALIDATE(app_mgr.getDatabaseManager(), collection);
}

int main()
{
    system("rm -f *.test.out");

    TestScalarCollection();
    TestEnabledLogic();
    TestQuietLogic();
    TestMultiClock();
    TestFlatten();
    TestContainers();
    TestPointers();
    TestMultiArgosCollectors();

    // TODO cnyce: initial value/bytes
    // TODO cnyce: default disabled
    // TODO cnyce: reattach
    // TODO cnyce: iterators with isValid (non-std::vector)

    REPORT_ERROR;
    return ERROR_CODE;
}
