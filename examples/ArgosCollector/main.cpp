#include "RandUtils.hpp"
#include "SimDBTester.hpp"
#include "simdb/apps/AppManager.hpp"
#include "simdb/apps/argos/ArgosCollector.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"

#include <map>
#include <set>

TEST_INIT;

/// Call once per test function.
#define TEST_METHOD_INIT simdb::argos::CollectableBase::resetCIDs()

constexpr size_t RUN_TICKS = 1000;

std::mt19937_64& testRng()
{
    static std::mt19937_64 rng(std::random_device{}());
    return rng;
}

template <typename IntT>
IntT randomInt(const IntT min_inclusive = std::numeric_limits<IntT>::min(),
               const IntT max_inclusive = std::numeric_limits<IntT>::max())
{
    std::uniform_int_distribution<IntT> dist(min_inclusive, max_inclusive);
    return dist(testRng());
}

double randomDouble(const double min_inclusive = std::numeric_limits<double>::min(),
                    const double max_inclusive = std::numeric_limits<double>::max())
{
    std::uniform_real_distribution<double> dist(min_inclusive, max_inclusive);
    return dist(testRng());
}

bool randomBool(const int denominator = 2)
{
    assert(denominator >= 2);
    return randomInt<int>(1, denominator) == 1;
}

std::string randomString(size_t num_chars = 8)
{
    static constexpr char charset[] = "0123456789"
                                      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
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

enum InstType { NO_OP, MEM, CSR, ILLEGAL, __N };

std::ostream& operator<<(std::ostream& os, const InstType type)
{
    switch (type)
    {
    case NO_OP:
        os << "NO_OP";
        break;
    case MEM:
        os << "MEM";
        break;
    case CSR:
        os << "CSR";
        break;
    case ILLEGAL:
        os << "ILLEGAL";
        break;
    case __N:
        throw simdb::DBException("Invalid enum");
    }
    return os;
}

InstType randomEnum()
{
    return static_cast<InstType>(randomInt<int>(0, static_cast<int>(InstType::__N) - 1));
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

public:
    uint32_t getUID() const { return uid_; }
    InstType getType() const { return type_; }
    uint64_t getOpcode() const { return opcode_; }
    const std::string& getMnemonic() const { return mnemonic_; }
    uint32_t getCsr() const { return csr_; }
    bool finishesSim() const { return last_inst_; }

    Instruction(InstType type, uint64_t opcode, const std::string& mnemonic, uint32_t csr = 0, bool last_inst = false) :
        type_(type),
        opcode_(opcode),
        mnemonic_(mnemonic),
        csr_(csr),
        last_inst_(last_inst)
    {
    }

    Instruction(const Instruction&) = default;
    Instruction& operator=(const Instruction&) = default;

    static Instruction* newRandom()
    {
        auto type = static_cast<InstType>(randomInt<int>(0, static_cast<int>(InstType::__N) - 1));
        auto opcode = randomInt<uint64_t>(0, std::numeric_limits<uint32_t>::max());

        static const char* mnemonics[] = {"add", "addi", "li", "b", "jlr"};
        auto mnemonic = mnemonics[randomInt<int>(0, 4)];
        auto csr = type == InstType::CSR ? randomInt<uint32_t>(0, 255) : 0;
        auto last_inst = randomBool(100);
        return new Instruction(type, opcode, mnemonic, csr, last_inst);
    }

    static std::shared_ptr<Instruction> genRandom() { return std::shared_ptr<Instruction>(newRandom()); }

    using InstQueue = std::vector<std::shared_ptr<Instruction>>;

    static InstQueue genRandomQueue(size_t capacity, bool sparse)
    {
        const size_t target_size = randomInt<size_t>() % (capacity + 1);
        if (!sparse)
        {
            InstQueue q;
            q.reserve(target_size);
            while (q.size() < target_size)
            {
                q.push_back(genRandom());
            }
            return q;
        }

        InstQueue q(capacity);
        std::set<uint16_t> occupied_slots;
        while (occupied_slots.size() < target_size)
        {
            occupied_slots.insert(static_cast<uint16_t>(randomInt<size_t>(0, capacity - 1)));
        }
        for (const uint16_t slot : occupied_slots)
        {
            q[slot] = genRandom();
        }
        return q;
    }

    void writeToBuffer(simdb::StreamBuffer& buf, simdb::TinyStrings<>* tiny_strings) const
    {
        buf.append(getUID());

        // TODO cnyce: Enums should be written as their underlying int type, and
        // a string-int mapping should be put in the database. For now, just rely
        // on TinyStrings and treat enums as strings instead of ints.
        std::ostringstream oss;
        oss << getType();
        buf.append(tiny_strings->getStringID(oss.str()));

        buf.append(getOpcode());
        buf.append(tiny_strings->getStringID(getMnemonic()));
        buf.append(getCsr());
        buf.append(finishesSim());
    }
};

void TestScalarCollection()
{
    TEST_METHOD_INIT;

    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::argos::ArgosCollector>();
    auto& app_mgr = app_mgrs.createAppManager("scalars.db");

    // Setup...
    app_mgr.enableApp(simdb::argos::ArgosCollector::NAME);
    app_mgrs.createEnabledApps();
    auto argos_collector = app_mgr.getApp<simdb::argos::ArgosCollector>();
    argos_collector->setHeartbeat(3);

    auto int_collector = argos_collector->createScalarCollector<int32_t>("top.int32", "root");
    auto string_collector = argos_collector->createScalarCollector<std::string>("top.string", "root");
    auto enum_collector = argos_collector->createScalarCollector<InstType>("top.inst_type", "root");
    auto bool_collector = argos_collector->createScalarCollector<bool>("top.bool", "root");
    auto struct_collector = argos_collector->createScalarCollector<Instruction>("top.inst", "root");

    uint64_t tick = 0;
    argos_collector->timestampWith(&tick);
    argos_collector->addClock("root", 1);

    app_mgrs.createSchemas();
    app_mgrs.postInit(0, nullptr);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    // Simulate...
    auto tiny_strings = argos_collector->getTinyStrings();
    while (++tick <= RUN_TICKS)
    {
        auto ival = randomInt<int32_t>();
        int_collector->setScalarValue(ival);

        auto sval = randomString();
        string_collector->setScalarValue(sval);

        auto eval = randomEnum();
        enum_collector->setScalarValue(eval);

        auto bval = randomBool();
        bool_collector->setScalarValue(bval);

        auto inst = Instruction::genRandom();
        std::vector<char> inst_bytes;
        simdb::StreamBuffer buf(inst_bytes);
        inst->writeToBuffer(buf, tiny_strings);
        struct_collector->setScalarValueBytes(inst_bytes);

        // Note that this API call is optional, and will be automatically
        // called at the end of simulation (preTeardown). But for as long
        // as this method is not called, the collected data will build up
        // in memory.
        argos_collector->sendCollectedDataToPipeline();
    }

    // Finalize...
    app_mgrs.postSimLoopTeardown();
}

template <bool Sparse> void TestInstQueueContainerCollection()
{
    TEST_METHOD_INIT;

    std::string db_file;
    if constexpr (Sparse)
    {
        db_file = "sparse.db";
    } else
    {
        db_file = "contig.db";
    }

    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::argos::ArgosCollector>();
    auto& app_mgr = app_mgrs.createAppManager(db_file);

    // Setup...
    app_mgr.enableApp(simdb::argos::ArgosCollector::NAME);
    app_mgrs.createEnabledApps();
    auto argos_collector = app_mgr.getApp<simdb::argos::ArgosCollector>();
    argos_collector->setHeartbeat(3);

    constexpr size_t capacity = 8;
    auto queue_collector =
        argos_collector->createContainerCollector<Instruction, Sparse>("top.instq", "root", capacity);

    uint64_t tick = 0;
    argos_collector->timestampWith(&tick);
    argos_collector->addClock("root", 1);

    app_mgrs.createSchemas();
    app_mgrs.postInit(0, nullptr);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    // Simulate...
    auto tiny_strings = argos_collector->getTinyStrings();
    while (++tick <= RUN_TICKS)
    {
        const auto queue = Instruction::genRandomQueue(capacity, Sparse);
        if constexpr (Sparse)
        {
            std::map<uint16_t, std::vector<char>> sparse_bin_bytes;
            for (size_t i = 0; i < queue.size(); ++i)
            {
                if (!queue[i])
                {
                    continue;
                }
                std::vector<char> bytes;
                simdb::StreamBuffer buf(bytes);
                queue[i]->writeToBuffer(buf, tiny_strings);
                sparse_bin_bytes.emplace(static_cast<uint16_t>(i), std::move(bytes));
            }
            queue_collector->setSparseContainerBinBytes(sparse_bin_bytes);
        } else
        {
            std::vector<std::vector<char>> contig_bin_bytes;
            for (size_t i = 0; i < queue.size(); ++i)
            {
                contig_bin_bytes.emplace_back();
                auto& bytes = contig_bin_bytes.back();
                simdb::StreamBuffer buf(bytes);
                queue[i]->writeToBuffer(buf, tiny_strings);
            }
            queue_collector->setContigContainerBinBytes(contig_bin_bytes);
        }

        // Note that this API call is optional, and will be automatically
        // called at the end of simulation (preTeardown). But for as long
        // as this method is not called, the collected data will build up
        // in memory.
        argos_collector->sendCollectedDataToPipeline();
    }

    // Finalize...
    app_mgrs.postSimLoopTeardown();
}

void TestContigContainerCollection()
{
    TestInstQueueContainerCollection<false>();
}

void TestSparseContainerCollection()
{
    TestInstQueueContainerCollection<true>();
}

int main()
{
    TestScalarCollection();
    TestContigContainerCollection();
    TestSparseContainerCollection();

    REPORT_ERROR;
    return ERROR_CODE;
}
