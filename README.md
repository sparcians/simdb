# SimDB — High-Performance Simulation Database

SimDB is a high-performance, header-only C++17 module that unifies **concurrent data pipelines** with **SQLite** to power scalable simulation data engines, analysis tooling, and UI backends. It provides a clean, modular foundation suitable for both simple and complex features in large simulation codebases.

---

## Why choose SimDB?

### Unified Concurrent Pipelines + SQLite
SimDB helps you build performant concurrent pipelines for SQLite. Its native integration with SQLite yields pipelines without database contention and with automatic batch-processing of database work on a single database thread.

### High Performance
SimDB is designed for extremely high throughput suitable for concurrent / multi-threaded systems:

- Minimizes transaction overhead
- Eliminates database access contention
- Shares worker threads across multiple running apps for efficient scaling
- Supports move-only semantics for pipeline data, though you can copy data too

### A Clean Break for Complex Codebases
Large simulation projects often suffer from tangled data paths, unbounded coupling, or legacy “write everything to files” architectures. SimDB replaces these with a clean, unified, highly optimized pipeline, helping teams reach performant solutions more quickly and with fewer moving parts.

---

## What Makes the Implementation Unique

SimDB abstracts away the pitfalls and complexities of using a filesystem-backed database concurrently:

- **No user-facing transactions required** — Avoid slow, tiny transactions and implicit file locks; SimDB batches and routes operations automatically.
- **Retry-on-fail implicit transactions** — Database transactions are retried until successful in the event of locked tables or other access issues.
- **No manual concurrency management** — No need to coordinate access to SQLite or worry about schema-level lock contention. SimDB ensures safe, high-throughput execution under heavy parallel workloads.
- **Unified output: “one DB to rule them all”** — Consolidates all simulation output into a single, queryable SQLite database: simulation stats, trace data, UI updates, analysis results, etc.
- **Shared threading model for scalability** — Multiple applications can run concurrently with the same database, and threads are shared appropriately for optimal scaling. Thread sharing occurs implicitly, though you can request dedicated threads for an app if need be (except the database thread; there is only one).

---

## What You Can Build with SimDB

SimDB enables a wide range of high-performance simulation and analysis workflows:

- Fast and scalable simulation statistics engines
- UI backends for live dashboards or post-simulation analysis
- Simulation state replayer backends
- Aggregate analysis across hundreds or thousands of simulations

---

## Ideal For

- Simulation frameworks needing scalable, consistent data handling  
- CPU/GPU performance modelers requiring extremely fast, low-overhead pipelines  
- Researchers analyzing large batches of simulation experiments  
- UI/visualization tools with real-time or batch data access  
- Teams that need to refactor or decouple overly complex simulation infrastructure  

---

## Code Snippets: Pipeline Creation

SimDB pipelines are created by defining a series of stages with specific I/O data types, and binding stage input/output queues together. Here is an example of a simple one-way pipeline with a single input. Pipeline stages can have multiple inputs/outputs, though a one-way pipeline is used here for brevity.

For full pipeline documentation, see `docs/book/book.html`

```
// Stage 1: Accept a timestamp and a vector of stats and compress them
class CompressionStage : public simdb::pipeline::Stage
{
public:
    using ZlibIn  = std::pair<uint64_t, std::vector<double>>;
    using ZlibOut = std::pair<uint64_t, std::vector<char>>;

    CompressionStage()
    {
        addInPort_<ZlibIn>("input_data", input_queue_);
        addOutPort_<ZlibOut>("output_bytes", output_queue_);
    }

private:
    simdb::pipeline::PipelineAction run_(bool force) override
    {
        auto action = simdb::pipeline::PipelineAction::SLEEP;

        // Process input if we have any.
        ZlibIn input;
        if (input_queue_->try_pop(input))
        {
            ZlibOut output;

            // Carry over the timestamp (uint64_t, e.g. simulation tick/cycle).
            output.first = input.first;

            // Compress and send.
            simdb::compressData(input.second, output.second);
            output_queue_->emplace(std::move(output));

            // Inform SimDB that we should keep running this stage without sleeping.
            action = simdb::pipeline::PipelineAction::PROCEED;
        }

        return action;
    }

    // Stage I/O queues are created and assigned by SimDB automatically.
    simdb::ConcurrentQueue<ZlibIn>* input_queue_ = nullptr;
    simdb::ConcurrentQueue<ZlibOut>* output_queue_ = nullptr;
};

// Stage 2: Write compressed data to SQLite
// NOTE: It is assumed here that your simdb::App subclass is called "SimStatsCollector".
using DatabaseIn  = ZlibOut;

class DatabaseStage : public simdb::pipeline::DatabaseStage<SimStatsCollector>
{
public:
    DatabaseStage()
    {
        addInPort_<DatabaseIn>("input_bytes", input_queue_);
    }

private:
    simdb::pipeline::PipelineAction run_(bool force) override
    {
        // NOTE: DatabaseStages are always implicitly run inside batch
        // BEGIN TRANSACTION / COMMIT TRANSACTION blocks on a single DB
        // thread.
        auto action = simdb::pipeline::PipelineAction::SLEEP;

        // Process input if we have any.
        DatabaseIn input;
        if (input_queue_->try_pop(input))
        {
            // Get prepared statement for the INSERT.
            // NOTE: Your simdb::App subclass defines the schema in another method.
            auto inserter = getTableInserter_("SimStats");

            // INSERT INTO SimStats COLUMNS(Timestamp, StatsBlob) VALUES(123, bytes...)
            inserter->setColumnValue(0, input.first);
            inserter->setColumnValue(1, input.second);
            inserter->createRecord();

            // Inform SimDB that we should keep running this stage without sleeping.
            action = simdb::pipeline::PipelineAction::PROCEED;
        }

        return action;
    }

    // Stage I/O queues are created and assigned by SimDB automatically.
    simdb::ConcurrentQueue<DatabaseIn>* input_queue_ = nullptr;
};

// Example schema for the SimStatsCollector.
void SimStatsCollector::defineSchema(simdb::Schema& schema)
{
    using dt = simdb::SqlDataType;

    auto& tbl = schema.addTable("SimStats");
    tbl.addColumn("Timestamp", dt::uint64_t);
    tbl.addColumn("StatsBlob", dt::blob_t);
}

// Pipeline creation: create stages, create I/O ports, bind ports.
void SimStatsCollector::createPipeline(simdb::pipeline::PipelineManager* pipeline_mgr)
{
    auto pipeline = pipeline_mgr->createPipeline("sim-stats-collector", this);

    pipeline->addStage<CompressionStage>("compressor");
    pipeline->addStage<DatabaseStage>("db_writer");
    pipeline->noMoreStages();

    pipeline->bind("compressor.output_bytes", "db_writer.input_bytes");
    pipeline->noMoreBindings();

    // Save the input queue to the first stage. Pipeline head is of type:
    // simdb::ConcurrentQueue<ZlibIn>* pipeline_head_;
    pipeline_head_ = pipeline->getInPortQueue<ZlibIn>("compressor.input_data");
}

// Main entry point for new data.
void SimStatsCollector::send(std::vector<double>&& stats)
{
    // Assumed that your simulation has some API to get the sim time:
    uint64_t timestamp = sim_->getCurrentTime();

    // Package it up and send:
    ZlibIn payload = std::make_pair(timestamp, std::move(stats));
    pipeline_head_->emplace(std::move(payload));
}
```

## Code Snippets: SQLite Interface

Even though SimDB's main advantage is its implicit integration of pipelines with SQLite, you can still use SimDB just for its SQLite interface without using any pipelines.

### Database Creation

```
using dt = simdb::SqlDataType;

simdb::Schema schema;

auto& tbl = schema.addTable("SimStats");
tbl.addColumn("Timestamp", dt::uint64_t);
tbl.addColumn("StatsBlob", dt::blob_t);

// Optionally:
//   Create table indexes...
//   Assign column default values...
//   Disable auto-incrementing Id column...

simdb::DatabaseManager db_mgr("stats.db");

// You can call this method more than once if additional tables are needed later.
db_mgr.appendSchema(schema);

// SQLite connection is now open.
```

See test/sqlite/Schema/main.cpp

### Record INSERT

```
// Without prepared statements:
db_mgr.INSERT(
    SQL_TABLE("SimStats"),
    SQL_COLUMNS("Timestamp", "StatsBlob"),
    SQL_VALUES(timestamp, blob)
);

// With prepared statements:
auto inserter = db_mgr.prepareINSERT(
    SQL_TABLE("SimStats"),
    SQL_COLUMNS("Timestamp", "StatsBlob")
);

// Reuse over and over:
data_insert_stmt->setColumnValue(0, time1);
data_insert_stmt->setColumnValue(1, blob2);
data_insert_stmt->createRecord();

data_insert_stmt->setColumnValue(0, time2);
data_insert_stmt->setColumnValue(1, blob2);
data_insert_stmt->createRecord();
```

See test/sqlite/Insert/main.cpp

### Explicit BEGIN / COMMIT TRANSACTION

Put multiple database operations in a single transaction. This will keep retrying in the event of locked schema tables or other access issues until successful.

```
db_mgr.safeTransaction([&]()
{
    // Do all transaction work here.
});
```

### Record SELECT

```
// SELECT StatsBlob FROM SimStats
// WHERE Timestamp >= 100 AND Timestamp <= 200
auto query = db_mgr.createQuery("SimStats");
query->addConstraintForUInt64("Timestamp", simdb::Constraints::GREATER_EQUAL, 100);
query->addConstraintForUInt64("Timestamp", simdb::Constraints::LESS_EQUAL, 200);

std::vector<char> compressed;
query->select("StatsBlob", compressed);

auto results = query->getResultSet();
while (results.getNextRecord()) {
    std::vector<double> stats;
    simdb::decompressData(compressed, stats);
    // Process stats...
}
```

See test/sqlite/Query/main.cpp

---

## Requirements

SimDB is a header-only library with minimal dependencies. You will need the following packages:

### System packages (Ubuntu / Debian)

```
sudo apt-get update
sudo apt-get install -y \
    cmake \
    libsqlite3-dev \
    zlib1g-dev \
    clang-format-19 \
    build-essential
```

### Conda Installation (existing environment)

```
conda activate <your-env-name>
conda install -y \
    cmake \
    sqlite \
    zlib \
    clang-format \
    compilers
```

### Conda Installation (new environment)

```
conda create -n simdb -y \
    cmake \
    sqlite \
    zlib \
    clang-format \
    compilers
conda activate simdb
```

---

## Installing SimDB

### System Install

```
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local/ ..
sudo make install
```

### Conda Install

```
conda activate <your-env-name>
cmake --install . --prefix $CONDA_PREFIX
```

---

## Regression Tests

```
# Release
mkdir release
cd release
cmake -DCMAKE_BUILD_TYPE=Release ..
make simdb_regress

# Debug
mkdir debug
cd debug
cmake -DCMAKE_BUILD_TYPE=Debug ..
make simdb_regress
```

---

## Submission Guidelines

Before opening a PR:

- Run `make simdb_regress` for release and/or debug builds (Linux, MacOS)
- Format your code changes: `clang-format-19 -i $(git ls-files '*.cpp' '*.hpp')`
- Ensure self-contained headers: `python3 scripts/check-headers.py`

GitHub will run these checks too, and all must pass before merging your PR.
