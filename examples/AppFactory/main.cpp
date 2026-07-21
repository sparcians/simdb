// clang-format off

#include "simdb/apps/AppManager.hpp"
#include "SimplePipeline.hpp"

// This example demonstrates how to create simdb::App subclasses
// using non-default factories. The reason to do this is if your
// app requires a constructor that is NOT what the default factory
// uses:
//
//     class MyApp : public simdb::App
//     {
//     public:
//         // Default factory assumes just DatabaseManager, so we
//         // need a different factory.
//         MyApp(simdb::DatabaseManager* db_mgr, int x, float y);
//     };

class MyApp : public SimplePipeline
{
public:
    static constexpr auto NAME = "my-app";

    MyApp(simdb::DatabaseManager* db_mgr, int x, float y)
        : SimplePipeline(db_mgr)
        , x_(x)
        , y_(y)
    {}

    static void defineSchema(simdb::Schema&)
    {
        // No schema for this app
    }

    int getX() const
    {
        return x_;
    }

    float getY() const
    {
        return y_;
    }

    class AppFactory : public simdb::AppFactoryBase
    {
    public:
        using AppT = MyApp;

        void parameterize(int x, float y)
        {
            ctor_args_ = std::make_pair(x, y);
        }

        AppT* createApp(simdb::DatabaseManager* db_mgr) override
        {
            const auto& [x, y] = ctor_args_;
            return new AppT(db_mgr, x, y);
        }

        void defineSchema(simdb::Schema& schema) const override
        {
            AppT::defineSchema(schema);
        }

    private:
        std::pair<int, float> ctor_args_;
    };

private:
    const int x_;
    const float y_;
};

/// Helper class for negative testing
class UnregisteredApp : public simdb::App
{
public:
    static constexpr auto NAME = "unregistered-app";
    UnregisteredApp(simdb::DatabaseManager*) {}
    static void defineSchema(simdb::Schema&) {}
};

/// Helper class for negative testing
class UnregisteredAppWithFactory : public simdb::App
{
public:
    static constexpr auto NAME = "unregistered-app";
    UnregisteredAppWithFactory(simdb::DatabaseManager*) {}
    static void defineSchema(simdb::Schema&) {}

    class AppFactory : public simdb::AppFactoryBase
    {
    public:
        simdb::App* createApp(simdb::DatabaseManager*) override
        {
            EXPECT_TRUE(false); // Should never get hit
            return nullptr;
        }

        // TODO cnyce: we should not make users write this code
        // so just have a class simdb::CustomAppFactory<MyApp>
        // in between the users' AppFactory and AppFactoryBase.
        void defineSchema(simdb::Schema&) const override
        {
            EXPECT_TRUE(false); // Should never get hit
        }

        void parameterize()
        {
            // Gets hit in calls to:
            // AppManager::parameterizeAppFactory<UnregisteredAppWithFactory>()
        }
    };
};

TEST_INIT;

int main()
{
    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<MyApp>();
    auto& app_mgr = app_mgrs.createAppManager("test.db");

    // Quick negative test: try to parameterize an unregistered app
    EXPECT_THROW(app_mgr.parameterizeAppFactory<UnregisteredApp>());
    EXPECT_THROW(app_mgr.parameterizeAppFactory<UnregisteredAppWithFactory>());
    EXPECT_THROW(app_mgr.parameterizeAppFactoryInstance<UnregisteredApp>(404));
    EXPECT_THROW(app_mgr.parameterizeAppFactoryInstance<UnregisteredAppWithFactory>(404));

    // Another negative test: try to parameterize a registered app that is not enabled
    EXPECT_THROW(app_mgr.parameterizeAppFactory<MyApp>(1/*x*/, 2.2/*y*/));
    EXPECT_THROW(app_mgr.parameterizeAppFactoryInstance<MyApp>(0 /*inst num*/, 1/*x*/, 2.2/*y*/));
    EXPECT_THROW(app_mgr.parameterizeAppFactoryInstance<MyApp>(0 /*inst num*/, 3/*x*/, 4.4/*y*/));

    // Enable 2 app instances
    app_mgr.enableApp(MyApp::NAME, 2);
    EXPECT_EQUAL(app_mgr.getEnabledAppInstances<MyApp>(), 2);

    // Configure app constructor calls before createEnabledApps()
    const int x1 = 45;
    const float y1 = 7.77;
    app_mgr.parameterizeAppFactoryInstance<MyApp>(0, x1, y1);

    const int x2 = 98;
    const float y2 = 3.14;
    app_mgr.parameterizeAppFactoryInstance<MyApp>(1, x2, y2);

    // Create the apps
    app_mgrs.createEnabledApps();

    // Validate
    auto app1 = app_mgr.getAppInstance<MyApp>(0);
    auto app2 = app_mgr.getAppInstance<MyApp>(1);
    EXPECT_EQUAL(app1->getX(), x1);
    EXPECT_EQUAL(app1->getY(), y1);
    EXPECT_EQUAL(app2->getX(), x2);
    EXPECT_EQUAL(app2->getY(), y2);

    // Create two MyApp instances, each one going to a different database
    app_mgrs.destroyAll();
    auto& app_mgr1 = app_mgrs.createAppManager("test1.db");
    auto& app_mgr2 = app_mgrs.createAppManager("test2.db");

    // Send one MyApp to each database
    app_mgr1.enableApp(MyApp::NAME);
    app_mgr2.enableApp(MyApp::NAME);

    // Parameterize the two apps
    app_mgr1.parameterizeAppFactoryInstance<MyApp>(0, x1, y1);
    app_mgr2.parameterizeAppFactoryInstance<MyApp>(0, x2, y2);

    // Create the two apps
    app_mgrs.createEnabledApps();

    // Validate the two apps
    app1 = app_mgr1.getAppInstance<MyApp>(0);
    app2 = app_mgr2.getAppInstance<MyApp>(0);
    EXPECT_NOTEQUAL(app1, app2);
    EXPECT_EQUAL(app1->getX(), x1);
    EXPECT_EQUAL(app1->getY(), y1);
    EXPECT_EQUAL(app2->getX(), x2);
    EXPECT_EQUAL(app2->getY(), y2);

    REPORT_ERROR;
    return ERROR_CODE;
}
