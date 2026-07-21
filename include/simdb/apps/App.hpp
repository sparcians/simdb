// <App.hpp> -*- C++ -*-

#pragma once

#include <memory>
#include <vector>

/// Aside from its core SQLite functionality, SimDB provides a framework for
/// creating "apps" which get selectively enabled based on your simulation
/// configuration (e.g. command line options, config file, etc).
///
///   - Create your own data/metadata schema tables just for your app
///   - Use SimDB utilities to build async compression/transform/DB pipelines
///
/// Example applications:
///
///   - Logger that records simulation events
///   - Profiler that tracks performance metrics
///   - Pipeline collector
///   - CPU pipeline instrumentation/collection with front-end viewer (Argos)
///   - Backend for a live data visualization GUI / web interface
///   - Backend for post-sim analysis using simulation state replayers
///
/// Since SimDB is designed to be simulator-agnostic, apps also provide
/// a variety of hooks that allow you to run code at different stages of
/// the simulation lifecycle and to ensure that all apps in your simulator
/// are initialized and run in a consistent manner.
///
///   - defineSchema:   (static) declare the app's schema tables. Called via the
///                     app's AppFactory before any app instance is created.
///   - postInit:       after command-line / config parsing, before the
///                     simulation starts.
///   - createPipeline: create and configure the app's pipeline(s).
///   - preTeardown:    just before simulation teardown; push any pending data
///                     to your app's pipeline
///   - postTeardown:   after simulation; post-sim metadata DB writes occur here;
///                     all running apps' postTeardown methods are executed in a
///                     single safeTransaction automatically (don't worry about
///                     using safeTransaction explicitly)
///
/// The general paradigm is that your simulator has a single output database,
/// with 1-to-many apps that are all writing to it with their own custom schemas
/// and logic.

namespace simdb {

class DatabaseManager;
class Schema;

namespace pipeline {
class AsyncDatabaseAccessor;
class PipelineManager;
} // namespace pipeline

class AppManager;
class ThreadSafeLogger;

/*!
 * \class App
 *
 * \brief Base class for SimDB applications. Subclasses receive a
 *        DatabaseManager in the constructor and can append schemas, insert
 *        records, and create pipelines. Lifecycle hooks: postInit(),
 *        createPipeline(), preTeardown(), postTeardown(). Use AppManager
 *        to register, enable, and instantiate apps.
 */
class App
{
public:
    virtual ~App() = default;

    /// \brief Set the instance number (1-based; 0 = single-instance). Called by AppManager.
    void setInstance(size_t instance) { instance_ = instance; }

    /// \brief Get the instance number (0 if single-instance).
    size_t getInstance() const { return instance_; }

    /// \brief Hook called after command-line parsing, before simulation starts.
    virtual void postInit([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {}

    /// \brief Hook to create and configure this app's pipeline(s) on the given manager.
    virtual void createPipeline(pipeline::PipelineManager*) {}

    /// \brief Hook called before simulation teardown.
    virtual void preTeardown() {}

    /// \brief Hook called after simulation teardown (resource cleanup).
    virtual void postTeardown() {}

    /// \brief Return the app logger (set by AppManager); may be null.
    ThreadSafeLogger* getAppLogger() const { return app_logger_; }

    /// \brief Check if we are running in verbose mode.
    bool verbose() const { return verbose_; }

protected:
    void setAppLogger_(ThreadSafeLogger* logger) { app_logger_ = logger; }

private:
    /// Instance number for multi-instance apps (1-based).
    /// If zero, then this is a single-instance app.
    size_t instance_ = 0;

    /// Thread-safe loggers.
    ThreadSafeLogger* app_logger_ = nullptr;

    /// Verbose flag.
    bool verbose_ = false;

    friend class AppManager;
};

/*!
 * \class AppFactoryBase
 *
 * \brief Abstract factory for creating App instances and defining their schema.
 *        Each app type exposes a nested AppFactory that implements this interface;
 *        AppManager uses it to instantiate apps and register their tables.
 *
 * \note It is optional to define a custom AppFactory nested in your app class.
 *       If you don't, the default AppFactory will be used, which creates the app
 *       with the default constructor (DatabaseManager*).
 */
class AppFactoryBase
{
public:
    virtual ~AppFactoryBase() = default;

    /// \brief Create an App instance for the given DatabaseManager.
    virtual App* createApp(DatabaseManager*) = 0;

    /// \brief Define this app's schema (tables, columns) on the given Schema.
    virtual void defineSchema(Schema& schema) const = 0;
};

/*!
 * \class AppFactory
 *
 * \brief Default factory that creates AppT and delegates defineSchema to AppT::defineSchema.
 * \tparam AppT App subclass (must have defineSchema(Schema&) and a constructor taking DatabaseManager*).
 */
template <typename AppT> class AppFactory : public AppFactoryBase
{
public:
    App* createApp(DatabaseManager* db_mgr) override { return new AppT(db_mgr); }

    void defineSchema(Schema& schema) const override { AppT::defineSchema(schema); }
};

} // namespace simdb
