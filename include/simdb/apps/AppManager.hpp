// <AppManager.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/App.hpp"
#include "simdb/pipeline/PipelineManager.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/utils/StreamFormatters.hpp"
#include "simdb/utils/ThreadSafeLogger.hpp"

#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#define PROFILE_APP_PHASE [[maybe_unused]] ScopedTimer timer(getDatabaseManager(), __FUNCTION__);

namespace simdb {

namespace utils {
template <typename, typename = void> struct has_nested_factory : std::false_type
{
};

template <typename T> struct has_nested_factory<T, std::void_t<typename T::AppFactory>> : std::true_type
{
};
} // namespace utils

class AppManager;
class AppManagers;

/*!
 * \class AppRegistrationBase
 *
 * \brief Abstract registration that adds an app type to an AppManager when
 *        registerApp() is called. Used by AppManagers to register app types
 *        before createAppManager().
 */
class AppRegistrationBase
{
public:
    virtual ~AppRegistrationBase() = default;
    /// \brief Register this app type with the given AppManager (e.g. enable it for later creation).
    virtual void registerApp(AppManager* app_manager) const = 0;
};

/// \brief Type-specific registration for AppT; created by AppManagers::registerApp<AppT>().
template <typename AppT> class AppRegistration : public AppRegistrationBase
{
private:
    AppRegistration() = default;
    void registerApp(AppManager* app_manager) const override;
    friend class AppManagers;
};

/*!
 * \class AppManager
 *
 * \brief Registers, enables, instantiates, and manages the lifecycle of SimDB
 *        apps for one database. Obtainfrom AppManagers::createAppManager() or
 *        AppManagers::getAppManager().
 */
class AppManager
{
public:
    /// \brief Return the associated DatabaseManager.
    DatabaseManager* getDatabaseManager() const { return db_mgr_; }

    /// \brief Parameterize an app factory. Must be called before createEnabledApps().
    /// Your app subclass must have a public nested class called "AppFactory",
    /// inheriting publicly from simdb::AppFactoryBase. Your nested AppFactory
    /// can have any signature it needs to accept all required app constructor
    /// arguments.
    ///
    ///   void parameterize(int x, float y)
    ///   {
    ///       ...
    ///   }
    ///
    /// For example:
    ///
    ///   class MyApp : public simdb::App
    ///   {
    ///   public:
    ///       MyApp(simdb::DatabaseManager* db_mgr, int x, float y);
    ///
    ///       class AppFactory : public simdb::AppFactoryBase
    ///       {
    ///       public:
    ///           void parameterize(int x, float y)
    ///           {
    ///               ctor_args_ = std::make_pair(x, y);
    ///           }
    ///
    ///           simdb::App* createApp(simdb::DatabaseManager* db_mgr) override
    ///           {
    ///               const auto & [x, y] = ctor_args_;
    ///               return new MyApp(db_mgr, x, y);
    ///           }
    ///
    ///           void defineSchema(simdb::Schema& schema) const override
    ///           {
    ///               ...
    ///           }
    ///
    ///       private:
    ///           std::pair<int,float> ctor_args_;
    ///       };
    ///   };
    ///
    /// Then do this:
    ///
    ///   // Parameterize before createEnabledApps()
    ///
    ///   // How to parameterize all instances of your app:
    ///   app_mgr.parameterizeAppFactory<MyApp>(1 /*x*/, 2.2 /*y*/);
    ///
    ///   // How to parameterize one instance of your app:
    ///   app_mgr.parameterizeAppFactoryInstance<MyApp>(1 /*inst num*/, 1
    ///   /*x*/, 2.2 /*y*/); app_mgr.parameterizeAppFactoryInstance<MyApp>(2
    ///   /*inst num*/, 3 /*x*/, 4.4 /*y*/);
    ///
    ///   // Assume have the AppManager by now:
    ///   app_mgr.enableApp(MyApp::NAME);
    ///   app_mgr.enableApp(MyApp::NAME, 2 /*inst count*/);
    ///
    ///   // Then continue and create the apps:
    ///   app_mgr.createEnabledApps();
    ///
    /// IMPORTANT: If you already configured specific app instance factories
    /// with parameterizeAppFactoryInstance(), this method will throw away
    /// all your factory configurations, and all instances will end up using
    /// the same factory that is being "globally" configured right now (unless
    /// you call parameterizeAppFactoryInstance() again). Only a warning will
    /// be issued.
    template <typename AppT, typename... Args> void parameterizeAppFactory([[maybe_unused]] Args&&... args)
    {
        if constexpr (utils::has_nested_factory<AppT>::value)
        {
            if (!enabled(AppT::NAME))
            {
                throw DBException("You need to call enableApp() before "
                                  "parameterizing factories.");
            }

            auto& app_factories = getDefaultAppFactories_();

            auto num_overwritten = app_factories[AppT::NAME].size();
            if (num_overwritten > 0)
            {
                std::cout << "WARNING: Throwing away " << num_overwritten << " app factor"
                          << (num_overwritten > 1 ? "ies " : "y") << " and creating a new one for all apps of type "
                          << AppT::NAME << ". Was this intentional?\n";
            }

            app_factories[AppT::NAME].clear();

            constexpr size_t global_instance_num = 0;
            auto factory = getNestedAppFactory_<AppT>(global_instance_num);
            factory->parameterize(std::forward<Args>(args)...);
        } else
        {
            throw DBException("No nested class 'AppFactory' exists for app '") << AppT::NAME << "'.";
        }
    }

    /// Parameterize an app factory. Call this after enableApp() but before
    /// createEnabledApps(). Your app subclass must have a public nested class
    /// called "AppFactory", inheriting publicly from simdb::AppFactoryBase.
    template <typename AppT, typename... Args>
    void parameterizeAppFactoryInstance(size_t instance_num, [[maybe_unused]] Args&&... args)
    {
        if constexpr (utils::has_nested_factory<AppT>::value)
        {
            if (!enabled(AppT::NAME))
            {
                throw DBException("You need to call enableApp() before "
                                  "parameterizing factories.");
            }

            std::cout << "Parameterizing '" << AppT::NAME << "' app, instance " << instance_num << "\n";
            auto factory = getNestedAppFactory_<AppT>(instance_num);
            factory->parameterize(std::forward<Args>(args)...);
        } else
        {
            throw DBException("No nested class 'AppFactory' exists for app '") << AppT::NAME << "'.";
        }
    }

    /// Enable verbose mode for all apps in this AppManager.
    void setVerbose(bool verbose = true)
    {
        verbose_ = verbose;
        for (auto app : getApps_())
        {
            app->verbose_ = verbose_;
        }
    }

    /// After parsing command line arguments or configuration files,
    /// enable an app by its name. This will allow the app to be instantiated
    /// and run during the simulation lifecycle.
    ///
    ///   class MyApp : public simdb::App
    ///   {
    ///   public:
    ///       static constexpr const char* NAME = "MyApp";
    ///       // ...
    ///   };
    ///
    ///   // Somewhere in your main function or config parsing code (meaning,
    ///   // after command line args are parsed and very early in the
    ///   simulation): simdb::AppManager::getInstance().enableApp(MyApp::NAME);
    void enableApp(const std::string& app_name, size_t num_instances = 1)
    {
        getEnabledApps_()[app_name] = num_instances;
    }

    template <typename AppT> void enableApp(size_t num_instances = 1)
    {
        static_assert(std::is_base_of<App, AppT>::value, "AppT must derive from App");
        enableApp(AppT::NAME, num_instances);
    }

    /// Check if your app is enabled (might not be instantiated yet).
    bool enabled(const std::string& app_name) const
    {
        const auto& enabled_apps = getEnabledApps_();
        return enabled_apps.find(app_name) != enabled_apps.end();
    }

    /// Check if your app is enabled (might not be instantiated yet).
    template <typename AppT> bool enabled() const
    {
        static_assert(std::is_base_of<App, AppT>::value, "AppT must derive from App");
        return enabled(AppT::NAME);
    }

    /// See how many instances of a particular app are enabled.
    size_t getEnabledAppInstances(const std::string& app_name) const
    {
        return enabled(app_name) ? getEnabledApps_().at(app_name) : 0;
    }

    /// See how many instances of a particular app are enabled.
    template <typename AppT> size_t getEnabledAppInstances() const { return getEnabledAppInstances(AppT::NAME); }

    /// Get an instantiated app. You may only call this method if AppT had
    /// exactly one instance configured:
    ///   AppManager::enableApp(AppT::NAME);
    ///   AppManager::enableApp(AppT::NAME, 1);
    ///
    /// For multi-instance apps, call getAppInstance<AppT>(instance_num)
    /// (zero-based). Note that for single-instance apps, you can still call
    /// getAppInstance<AppT>(0).
    template <typename AppT> AppT* getApp()
    {
        static_assert(std::is_base_of<App, AppT>::value, "AppT must derive from App");

        if (!enabled<AppT>())
        {
            return nullptr;
        }

        auto& enabled_apps = getEnabledApps_();
        if (enabled_apps.at(AppT::NAME) > 1)
        {
            throw DBException("Need to call getAppInstance<AppT>(instance_num) since ")
                << "the '" << AppT::NAME << "' app was configured to have " << enabled_apps.at(AppT::NAME)
                << " instances.";
        }

        // Look for instance name
        std::string instance_name = AppT::NAME + std::string("-0");
        auto it = apps_.find(instance_name);
        if (it != apps_.end())
        {
            auto app = dynamic_cast<AppT*>(it->second.get());
            if (!app)
            {
                throw DBException("App of type ") << AppT::NAME << " is not of type " << typeid(AppT).name();
            }
            return app;
        }

        return nullptr;
    }

    /// Get an instantiated app. Call this method for multi-instance apps.
    template <typename AppT> AppT* getAppInstance(size_t instance_num)
    {
        static_assert(std::is_base_of<App, AppT>::value, "AppT must derive from App");

        if (!enabled<AppT>())
        {
            return nullptr;
        }

        // Look for instance name
        std::string instance_name = AppT::NAME + std::string("-") + std::to_string(instance_num);
        auto it = apps_.find(instance_name);
        if (it != apps_.end())
        {
            auto app = dynamic_cast<AppT*>(it->second.get());
            if (!app)
            {
                throw DBException("App of type ") << AppT::NAME << " is not of type " << typeid(AppT).name();
            }
            return app;
        }

        return nullptr;
    }

    /// Optionally call this method after initializePipelines(), but before
    /// openPipelines(). This will reduce the number of non-database threads
    /// to the minimum across all app pipelines.
    ///
    /// Note that you can either call minimizeThreads() OR minimizeThreads(app1,
    /// app2, ...) but you cannot call both.
    void minimizeThreads()
    {
        if (!pipeline_mgr_)
        {
            throw DBException("Pipeline manager not set - did you call "
                              "initializePipelines()?");
        }
        pipeline_mgr_->minimizeThreads();
    }

    /// Optionally call this method after initializePipelines(), but before
    /// openPipelines(). This will share the minimum number of non-database
    /// threads across the given apps' pipelines.
    template <typename... Apps> void minimizeThreads(const App* app, Apps&&... rest)
    {
        if (!pipeline_mgr_)
        {
            throw DBException("Pipeline manager not set - did you call "
                              "initializePipelines()?");
        }
        pipeline_mgr_->minimizeThreads(app, std::forward<Apps>(rest)...);
    }

    /// \brief Set the order in which app lifecycle hooks are invoked.
    ///
    /// postInit(), preTeardown(), and postTeardown() are called in this order
    /// across apps. \p ordered_app_names lists base app names (e.g. \c "App2",
    /// \c "App1"); hooks run first for all instances of the first name, then
    /// the second, etc. Apps not listed are invoked after the listed apps, in
    /// map order. Call before createEnabledApps() (or at least before the
    /// hooks run). Pass an empty vector to clear ordering and use default
    /// (map) order.
    void setAppOrderingForHooks(const std::vector<std::string>& ordered_app_names)
    {
        hook_ordering_ = ordered_app_names;
    }

    /// \brief Set the order in which app lifecycle hooks are invoked (variadic).
    ///
    /// Same as setAppOrderingForHooks(vector) but takes a list of app names,
    /// e.g. setAppOrderingForHooks("App2", "App1") so App2's hooks run before
    /// App1's.
    template <typename... AppNames>
    void setAppOrderingForHooks(const std::string_view app_name, AppNames&&... app_names)
    {
        hook_ordering_.clear();
        hook_ordering_.reserve(1 + sizeof...(AppNames));
        hook_ordering_.emplace_back(app_name);
        if constexpr (sizeof...(AppNames) > 0)
        {
            (hook_ordering_.emplace_back(std::forward<AppNames>(app_names)), ...);
        }
    }

private:
    /// AppManagers are associated 1-to-1 with a DatabaseManager.
    AppManager(DatabaseManager* db_mgr, ThreadSafeLogger* app_logger = nullptr) :
        db_mgr_(db_mgr),
        app_logger_(app_logger)
    {
    }

    /// AppManager only to be instantiated by simdb::AppManagers
    friend class AppManagers;

    /// App registration only to be done by simdb::AppRegistration<AppT>
    template <typename AppT> friend class AppRegistration;

    /// Register an app as early as possible (doesn't create it yet).
    template <typename AppT> void registerApp_()
    {
        if constexpr (utils::has_nested_factory<AppT>::value)
        {
            // We don't need to create the nested factory object now.
            // When apps provide a nested factory, they are supposed
            // to call parameterizeApp<T>() / parameterizeAppInstance<T>()
            // before calling createEnabledApps().
            //
            // If they forget to parameterize their own custom-factory app,
            // we will know it since we won't have the factory. These factories
            // are created in the parameterize*() calls.
            return;
        } else
        {
            auto& app_factories = getDefaultAppFactories_();
            if (app_factories.find(AppT::NAME) != app_factories.end())
            {
                throw DBException("App already registered: ") << AppT::NAME;
            }

            constexpr size_t global_instance_num = 0;
            auto& factory = app_factories[AppT::NAME][global_instance_num];
            factory = std::make_shared<AppFactory<AppT>>();
        }
    }

    /// Get all Apps that belong to the given database.
    std::vector<App*> getApps_()
    {
        std::vector<App*> apps;
        for (const auto& [key, app] : apps_)
        {
            apps.push_back(app.get());
        }
        return apps;
    }

    /// Get apps in the order to use for lifecycle hooks (postInit, preTeardown, postTeardown).
    std::vector<App*> getAppsInHookOrder_()
    {
        if (hook_ordering_.empty())
        {
            return getApps_();
        }
        // Group by base app name (instance name is "BaseName-0", "BaseName-1", ...).
        std::map<std::string, std::vector<App*>> by_base;
        for (const auto& [instance_name, app] : apps_)
        {
            auto dash = instance_name.find_last_of('-');
            std::string base = (dash != std::string::npos) ? instance_name.substr(0, dash) : instance_name;
            by_base[base].push_back(app.get());
        }
        std::vector<App*> out;
        out.reserve(apps_.size());
        for (const std::string& base : hook_ordering_)
        {
            auto it = by_base.find(base);
            if (it != by_base.end())
            {
                for (App* app : it->second)
                {
                    out.push_back(app);
                }
            }
        }

        // Now append any apps that were not given explicit hook ordering
        for (auto& [_, app] : apps_)
        {
            if (std::find(out.begin(), out.end(), app.get()) == out.end())
            {
                out.push_back(app.get());
            }
        }

        auto apps = getApps_();
        assert(std::set<App*>(out.begin(), out.end()) == std::set<App*>(apps.begin(), apps.end()));
        return out;
    }

    /// Call after command line args and config files are parsed.
    void createEnabledApps_()
    {
        for (const auto& [app_name, num_instances] : getEnabledApps_())
        {
            for (size_t instance_num = 0; instance_num < num_instances; ++instance_num)
            {
                auto factory = getAppFactory_(app_name, instance_num, false /*do not create*/);
                if (!factory)
                {
                    continue;
                }

                App* app = factory->createApp(db_mgr_);
                app->app_logger_ = app_logger_;
                app->instance_ = instance_num;
                app->verbose_ = verbose_;
                std::string instance_name = app_name + std::string("-") + std::to_string(instance_num);
                apps_[instance_name] = std::unique_ptr<App>(app);
            }
        }
    }

    /// Create app-specific schemas for the instantiated apps.
    void createSchemas_()
    {
        PROFILE_APP_PHASE

        // TODO cnyce: We are currently creating the apps first, then creating the schemas.
        // There is no reason to do this in that order, and we should create the schemas
        // first. The doc (book) was written assuming schemas come first; leave the doc
        // that way and change the C++ code to match. Call out that you can freely write
        // to your tables in defineSchema right in your App's constructor. Then ensure
        // that the createEnabledApps call is inside a safeTransaction.
        db_mgr_->safeTransaction([&]() {
            for (const auto& [app_name, app] : apps_)
            {
                auto user_app_name = app_name;
                auto idx = user_app_name.find_last_of("-");
                assert(idx != std::string::npos);
                user_app_name = user_app_name.substr(0, idx);

                auto tmp_substr = app_name.substr(idx);
                auto instance_num = static_cast<size_t>(std::stoull(tmp_substr));
                AppFactoryBase* factory = getAppFactory_(user_app_name, instance_num);

                // TODO cnyce: We should consider automatically prepending "<app name>$"
                // before every table in the app schema. With a growing number of apps,
                // and SimDB living in open source, this will eventually trip someone
                // up from e.g. using a generic table like "SimMetadata" which collides.
                // You will have to document that the app name might have restrictions
                // that will match whatever restrictions SQLite puts on table names.
                Schema app_schema;
                factory->defineSchema(app_schema);
                db_mgr_->appendSchema(app_schema);
            }
        });
    }

    /// Call this after command line args and config files are parsed.
    void postInit_(int argc, char** argv)
    {
        PROFILE_APP_PHASE

        db_mgr_->safeTransaction([&]() {
            for (auto app : getAppsInHookOrder_())
            {
                app->postInit(argc, argv);
            }
        });
    }

    /// Call this once after postInit(). This will create all pipelines
    /// for all enabled apps, but it will not open the threads yet.
    void initializePipelines_()
    {
        PROFILE_APP_PHASE

        if (pipeline_mgr_)
        {
            throw DBException("Pipelines already open");
        }

        pipeline_mgr_ = std::make_unique<pipeline::PipelineManager>(db_mgr_);
        for (const auto& [app_name, app] : apps_)
        {
            app->createPipeline(pipeline_mgr_.get());
        }

        // Print final pipeline configurations.
        std::cout << "\nSimDB app pipeline configuration for database '" << db_mgr_->getDatabaseFilePath() << "':\n";
        for (auto pipeline : pipeline_mgr_->getPipelines())
        {
            std::cout << "---- Pipeline: " << pipeline->getName() << "\n";
            for (auto& [stage_name, stage] : pipeline->getOrderedStages())
            {
                std::cout << "------ Stage: " << stage_name << "\n";
            }
        }

        std::cout << std::endl;
    }

    /// Call this once after initializePipelines() (and after minimizeThreads()
    /// if you called that too).
    void openPipelines_()
    {
        PROFILE_APP_PHASE

        if (!pipeline_mgr_)
        {
            throw DBException("Pipeline manager not set - did you call "
                              "initializePipelines()?");
        }
        pipeline_mgr_->openPipelines();
    }

    /// This method is to be called after the main simulation loop ends.
    /// All running apps' pipelines will be flushed, and all threads
    /// will be torn down.
    void postSimLoopTeardown_()
    {
        PROFILE_APP_PHASE

        for (auto app : getAppsInHookOrder_())
        {
            app->preTeardown();
        }

        if (pipeline_mgr_)
        {
            pipeline_mgr_->postSimLoopTeardown();
        }

        db_mgr_->safeTransaction([&]() {
            for (auto app : getAppsInHookOrder_())
            {
                app->postTeardown();
            }

            // TODO cnyce: We should associate the DatabaseManager and
            // the TinyStrings so that it gets flushed automatically.
            // This will let us document that postSimLoopTeardown()
            // is the only thing you have to call in the event of
            // an exception/abort/terminate during simulation.
        });
    }

    /// Delete all instantiated apps. This may be needed since AppManager
    /// is a singleton and your simulator might want to call app destructors
    /// before the AppManager itself is destroyed on program exit.
    void destroyAllApps_()
    {
        pipeline_mgr_.reset();
        apps_.clear();
    }

    using app_factories_t = std::map<std::string,                                // App name
                                     std::map<size_t,                            // App instance
                                              std::shared_ptr<AppFactoryBase>>>; // Factory

    /// Get a map for all registered app factories.
    app_factories_t& getDefaultAppFactories_() { return default_app_factories_; }

    /// Get a map for all registered app factories.
    const app_factories_t& getDefaultAppFactories_() const { return default_app_factories_; }

    /// Factories for apps with default constructors.
    app_factories_t default_app_factories_;

    /// Factories for apps that have a nested AppFactory class.
    app_factories_t nested_app_factories_;

    /// Access an app factory.
    template <typename AppT>
    typename AppT::AppFactory* getNestedAppFactory_(size_t instance_num, bool create_if_needed = true)
    {
        if (!create_if_needed)
        {
            auto it = nested_app_factories_.find(AppT::NAME);
            if (it == nested_app_factories_.end())
            {
                return nullptr;
            }

            auto it2 = it->second.find(instance_num);
            if (it2 == it->second.end())
            {
                return nullptr;
            }

            auto factory = dynamic_cast<typename AppT::AppFactory*>(it2->second.get());
            if (!factory)
            {
                throw DBException("Failed to downcast app factory for '") << AppT::NAME << "'.";
            }

            return factory;
        }

        auto& factory = nested_app_factories_[AppT::NAME][instance_num];
        if (!factory)
        {
            factory = std::make_shared<typename AppT::AppFactory>();
        }

        return dynamic_cast<typename AppT::AppFactory*>(factory.get());
    }

    AppFactoryBase* getAppFactory_(const std::string& app_name, size_t instance_num, bool must_exist = true) const
    {
        auto app_factories = getAllAppFactories_();

        auto it = app_factories.find(app_name);
        if (it == app_factories.end())
        {
            if (must_exist)
            {
                throw DBException("No factory exists for app: ") << app_name;
            }
            return nullptr;
        }

        auto it2 = it->second.find(instance_num);
        if (it2 == it->second.end())
        {
            // If we looked for a specific app instance factory (non-zero)
            // and could not find it, look for a "global" factory with
            // instance_num 0 for this app.
            if (instance_num > 0)
            {
                constexpr size_t global_instance_num = 0;
                return getAppFactory_(app_name, global_instance_num, must_exist);
            }

            if (must_exist)
            {
                throw DBException("No factory exists for instance ") << instance_num << " for app: " << app_name;
            }
            return nullptr;
        }

        return it2->second.get();
    }

    /// Return a union of the global and local app factories.
    app_factories_t getAllAppFactories_() const
    {
        auto default_app_factories = getDefaultAppFactories_();
        return combineFactories_(default_app_factories, nested_app_factories_);
    }

    /// Merge two app_factories_t together, verifying that they
    /// have no overlap.
    app_factories_t combineFactories_(const app_factories_t& global, const app_factories_t& local) const
    {
        app_factories_t result = global;

        for (const auto& [app_name, local_instances] : local)
        {
            auto& result_instances = result[app_name];

            for (const auto& [instance_id, factory] : local_instances)
            {
                const auto [it, inserted] = result_instances.emplace(instance_id, factory);

                if (!inserted)
                {
                    throw std::logic_error("Duplicate AppFactory entry for app '" + app_name + "', instance " +
                                           std::to_string(instance_id));
                }
            }
        }

        return result;
    }

    /// Instantiated apps (implicitly enabled).
    /// Key is the App's NAME static member, or NAME-<instance>
    /// for apps with multiple instances (one-based).
    std::map<std::string, std::unique_ptr<App>> apps_;

    /// Optional order for lifecycle hooks (postInit, preTeardown, postTeardown).
    /// Empty means use default (map) order.
    std::vector<std::string> hook_ordering_;

    /// Enabled apps (may or may not be instantiated).
    /// Key is the App's NAME static member.
    /// Value is the number of instances to create.
    std::map<std::string, size_t>& getEnabledApps_() { return enabled_apps_; }

    /// Enabled apps (may or may not be instantiated).
    /// Key is the App's NAME static member.
    /// Value is the number of instances to create.
    const std::map<std::string, size_t>& getEnabledApps_() const { return enabled_apps_; }

    /// Enabled apps and their app instance count.
    std::map<std::string, size_t> enabled_apps_;

    /// Associated database.
    DatabaseManager* db_mgr_ = nullptr;

    /// All pipelines and threads are managed by PipelineManager.
    std::unique_ptr<pipeline::PipelineManager> pipeline_mgr_;

    /// App logger (thread-safe). Owned by AppManagers.
    ThreadSafeLogger* app_logger_ = nullptr;

    /// Verbose flag.
    bool verbose_ = false;

    /// RAII timer to measure the performance of various app setup/teardown
    /// phases.
    class ScopedTimer
    {
    public:
        ScopedTimer(const DatabaseManager* db_mgr, const std::string& block_name) :
            start_(std::chrono::high_resolution_clock::now()),
            block_name_(block_name)
        {
            auto db_filepath = db_mgr->getDatabaseFilePath();
            std::cout << "SimDB: Entering " << block_name << " for database: " << db_filepath << "\n";
        }

        ~ScopedTimer()
        {
            [[maybe_unused]] ios_format_saver fmt_saver(std::cout);

            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> dur = end - start_;
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(dur).count();
            if (us >= 1000000)
            {
                auto sec = (double)us / 1000000;
                std::cout << "SimDB: Completed " << block_name_ << " in ";
                std::cout << std::fixed << std::setprecision(2) << sec << " seconds.\n";
            } else if (us >= 1000)
            {
                auto milli = (double)us / 1000;
                std::cout << "SimDB: Completed " << block_name_ << " in ";
                std::cout << std::fixed << std::setprecision(0) << milli << " milliseconds.\n";
            } else
            {
                auto micro = (double)us;
                std::cout << "SimDB: Completed " << block_name_ << " in ";
                std::cout << std::fixed << std::setprecision(0) << micro << " microseconds.\n";
            }
        }

    private:
        std::chrono::high_resolution_clock::time_point start_;
        std::string block_name_;
    };
};

template <typename AppT> void AppRegistration<AppT>::registerApp(AppManager* app_manager) const
{
    app_manager->registerApp_<AppT>();
}

/*!
 * \class AppManagers
 *
 * \brief Singleton-like holder for all DatabaseManagers and AppManagers. Call
 *        registerApp<AppT>() for each app type before createAppManager(); then
 *        createAppManager(db_file), createEnabledApps(), createSchemas(),
 *        postInit(), initializePipelines(), openPipelines(). Use getAppManager()
 *        or getDatabaseManager() to access managers. Obtain from
 *        AppManagers::getInstance() or AppRegistrations::getInstance().
 */
class AppManagers
{
public:
    /// \brief Register an app type; must be called before createAppManager()
    /// \note You need to call this method for all apps you might end up
    ///       creating **before** calling createAppManager(). As soon as
    ///       createAppManager() is called the first time, you can no
    ///       longer call registerApp().
    template <typename AppT> void registerApp()
    {
        if (app_registration_locked_)
        {
            throw DBException("No more apps can be registered since createAppManager() ") << "has already been called.";
        }
        app_registrations_.emplace_back(new AppRegistration<AppT>());
    }

    /// If you want all apps to share a global thread-safe logger, call this
    /// method before createAppManager().
    ///
    /// Every line written to the logger will start with the given prefix.
    void useThreadSafeLogger(const std::string& prefix = "[simdb-log]")
    {
        if (!accepting_logger_requests_)
        {
            throw DBException("No longer accepting thread-safe logger requests");
        }
        app_logger_ = std::make_unique<ThreadSafeLogger>(prefix);
    }

    /// If you want all apps to share a global thread-safe file logger,
    /// call this method before createAppManager().
    ///
    /// Every line written to the logger will start with the given prefix.
    void useThreadSafeFileLogger(const std::string& filename)
    {
        if (!accepting_logger_requests_)
        {
            throw DBException("No longer accepting thread-safe logger requests");
        }
        app_logger_ = std::make_unique<ThreadSafeFileLogger>(filename);
    }

    /// Enable verbose mode for all apps in all AppManager's.
    void setVerbose(bool verbose = true)
    {
        verbose_ = verbose;
        for (auto& [app_mgr, _] : db_mgrs_by_app_mgr_)
        {
            app_mgr->setVerbose(verbose_);
        }
    }

    /// Create a new AppManager with a new database.
    ///
    /// Pass in new_db=true to overwrite existing database, or new_db=false to use
    /// the existing database on disk. If new_db=false and the db_file does not
    /// exist, throws an exception.
    ///
    /// Throws if an AppManager for this database already exists.
    AppManager& createAppManager(const std::string& db_file, bool new_db = true)
    {
        accepting_logger_requests_ = false;

        if (app_mgrs_by_db_file_.find(db_file) != app_mgrs_by_db_file_.end())
        {
            throw DBException("AppManager already exists for database: ") << db_file;
        }

        if (!new_db && !std::filesystem::exists(db_file))
        {
            throw DBException("Cannot reuse database since it doesn't exist: ") << db_file;
        }

        std::shared_ptr<DatabaseManager> db_mgr(new DatabaseManager(db_file, new_db));
        std::shared_ptr<AppManager> app_mgr(new AppManager(db_mgr.get(), app_logger_.get()));

        app_mgr->setVerbose(verbose_);

        db_mgrs_by_db_file_[db_file] = db_mgr;
        app_mgrs_by_db_file_[db_file] = app_mgr;

        app_mgrs_by_db_mgr_[db_mgr.get()] = app_mgr;
        db_mgrs_by_app_mgr_[app_mgr.get()] = db_mgr;

        for (const auto& app_reg : app_registrations_)
        {
            app_reg->registerApp(app_mgr.get());
        }
        app_registration_locked_ = true;
        return *app_mgrs_by_db_file_[db_file];
    }

    /// Get an existing AppManager with a database filename / filepath.
    /// Throws if createAppManager() was never called with this database.
    AppManager& getAppManager(const std::string& db_file)
    {
        if (app_mgrs_by_db_file_.find(db_file) == app_mgrs_by_db_file_.end())
        {
            throw DBException("AppManager does not exist for this database: ") << db_file;
        }
        return *app_mgrs_by_db_file_[db_file];
    }

    /// If there is only one AppManager, return it. Otherwise throw.
    AppManager& getAppManager()
    {
        if (app_mgrs_by_db_file_.size() == 1)
        {
            return *app_mgrs_by_db_file_.begin()->second;
        }

        throw DBException("Cannot call getAppManager() since there are ")
            << app_mgrs_by_db_file_.size() << " AppManager's. Must be only one.";
    }

    /// If there is only one AppManager, return its DatabaseManager. Otherwise
    /// throw.
    DatabaseManager& getDatabaseManager()
    {
        if (db_mgrs_by_db_file_.size() == 1)
        {
            return *db_mgrs_by_db_file_.begin()->second;
        }

        throw DBException("Cannot call getDatabaseManager() since there are ")
            << db_mgrs_by_db_file_.size() << " AppManager's. Must be only one.";
    }

    /// Get a DatabaseManager for the given DB file. Throws if not found.
    DatabaseManager& getDatabaseManager(const std::string& db_file)
    {
        auto it = db_mgrs_by_db_file_.find(db_file);
        if (it == db_mgrs_by_db_file_.end())
        {
            throw DBException("DatabaseManager does not exist for file '") << db_file << "'.";
        }
        return *it->second;
    }

    /// Get a mapping from all active AppManager's and their associated
    /// DatabaseManager's.
    std::vector<std::pair<AppManager*, DatabaseManager*>> getAllManagers()
    {
        std::vector<std::pair<AppManager*, DatabaseManager*>> mgrs;
        for (auto& [db_file, db_mgr] : db_mgrs_by_db_file_)
        {
            auto it = app_mgrs_by_db_file_.find(db_file);
            if (it == app_mgrs_by_db_file_.end())
            {
                continue;
            }

            auto& app_mgr = it->second;
            mgrs.push_back(std::make_pair(app_mgr.get(), db_mgr.get()));
        }
        return mgrs;
    }

    /// Call after command line args and config files are parsed.
    void createEnabledApps()
    {
        for (auto& [app_mgr, _] : getAllManagers())
        {
            app_mgr->createEnabledApps_();
        }
    }

    /// Create app-specific schemas for the instantiated apps.
    void createSchemas()
    {
        for (auto& [app_mgr, _] : getAllManagers())
        {
            app_mgr->createSchemas_();
        }
    }

    /// Call this after command line args and config files are parsed.
    void postInit(int argc, char** argv)
    {
        for (auto& [app_mgr, _] : getAllManagers())
        {
            app_mgr->postInit_(argc, argv);
        }
    }

    /// Call this once after postInit(). This will create all pipelines
    /// for all enabled apps, but it will not open the threads yet.
    void initializePipelines()
    {
        for (auto& [app_mgr, _] : getAllManagers())
        {
            app_mgr->initializePipelines_();
        }
    }

    /// Call this once after initializePipelines() (and after minimizeThreads()
    /// if you called that too).
    void openPipelines()
    {
        for (auto& [app_mgr, _] : getAllManagers())
        {
            app_mgr->openPipelines_();
        }
    }

    /// Call postSimLoopTeardown() on all AppManager's. This destroys all Apps
    /// and AppManager's. It does NOT destroy the DatabaseManager's. Once this
    /// method is called, you have to access the DatabaseManager's using the
    /// 'getDatabaseManager(db_file)' API.
    void postSimLoopTeardown()
    {
        for (auto& [_, app_mgr] : app_mgrs_by_db_file_)
        {
            app_mgr->postSimLoopTeardown_();
        }

        destroy_(false);
    }

    /// Destroy every AppManager and DatabaseManager we have.
    void destroyAll() { destroy_(true); }

private:
    void destroy_(bool destroy_all)
    {
        /// Destroy all AppManagers, DatabaseManagers, and Apps
        for (auto& [_, app_mgr] : app_mgrs_by_db_file_)
        {
            app_mgr->destroyAllApps_();
        }

        // Destroy AppManager's first in case they need to touch
        // the database in their destructors.
        app_mgrs_by_db_mgr_.clear();
        app_mgrs_by_db_file_.clear();

        // Clear DatabaseManager maps. Hang onto db_mgrs_by_db_file_
        // so 'getDatabaseManager(db_file)' is still available.
        db_mgrs_by_app_mgr_.clear();

        // Destroy DatabaseManagers
        if (destroy_all)
        {
            db_mgrs_by_db_file_.clear();
        }
    }

    std::vector<std::unique_ptr<AppRegistrationBase>> app_registrations_;
    bool app_registration_locked_ = false;

    std::map<std::string, std::shared_ptr<DatabaseManager>> db_mgrs_by_db_file_;
    std::map<std::string, std::shared_ptr<AppManager>> app_mgrs_by_db_file_;
    std::map<DatabaseManager*, std::shared_ptr<AppManager>> app_mgrs_by_db_mgr_;
    std::map<AppManager*, std::shared_ptr<DatabaseManager>> db_mgrs_by_app_mgr_;

    std::unique_ptr<ThreadSafeLogger> app_logger_;
    bool accepting_logger_requests_ = true;
    bool verbose_ = false;
};

/*!
 * \class AppRegistrations
 *
 * \brief Thin wrapper that exposes only registerApp<AppT>() on an AppManagers
 *        instance. Use when you want to restrict API surface to registration only.
 */
class AppRegistrations
{
public:
    /// \brief Construct with the AppManagers instance to wrap.
    explicit AppRegistrations(AppManagers* app_managers) :
        app_managers_(app_managers)
    {
    }

    /// \brief Register an app type (forwards to AppManagers::registerApp<AppT>()).
    template <typename AppT> void registerApp() { app_managers_->registerApp<AppT>(); }

private:
    AppManagers* app_managers_ = nullptr;
};

} // namespace simdb
