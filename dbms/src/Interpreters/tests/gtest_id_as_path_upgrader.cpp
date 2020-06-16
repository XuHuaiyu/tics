#include <Databases/IDatabase.h>
#include <Debug/MockTiDB.h>
#include <Interpreters/IDAsPathUpgrader.h>
#include <Interpreters/loadMetadata.h>
#include <Poco/Environment.h>
#include <Storages/registerStorages.h>
#include <test_utils/TiflashTestBasic.h>

namespace DB::tests
{

class IDAsPathUpgrader_test : public ::testing::Test
{
public:
    void SetUp() override
    {
        TiFlashTestEnv::setupLogger();
        try
        {
            registerStorages();
        }
        catch (DB::Exception &)
        {
            // Maybe another test has already registed, ignore exception here.
        }
    }

    void TearDown() override
    {
        auto ctx = TiFlashTestEnv::getContext();
        auto databases = ctx.getDatabases();
        for (auto database : databases)
        {
            database.second->shutdown();
            ctx.detachDatabase(database.first);
        }
    }

    // If you want to run these tests, you should set this envrionment variablle
    // For example:
    //     ALSO_RUN_WITH_TEST_DATA=1 ./dbms/gtests_dbms --gtest_filter='IDAsPath*'
    bool isEnabled() const { return (Poco::Environment::get("ALSO_RUN_WITH_TEST_DATA", "0") == "1"); }
};

TEST_F(IDAsPathUpgrader_test, ONCALL_1651)
try
{
    if (!isEnabled())
        return;

    // prepare a "test" database for upgrader
    MockTiDB::instance().newDataBase("test"); // id == 2

    // Generated by running these SQL on cluster version v3.1.0
    // > create table test.aaa(pk int);
    // > rename table test.aaa TO test.abc;
    // > CREATE TABLE test.employees(id INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    //     fname VARCHAR(25) NOT NULL,
    //     lname VARCHAR(25) NOT NULL,
    //     store_id INT NOT NULL,
    //     department_id INT NOT NULL)
    //     PARTITION BY RANGE(id)(PARTITION p0 VALUES LESS THAN(5),
    //         PARTITION p1 VALUES LESS THAN(10),
    //         PARTITION p2 VALUES LESS THAN(15),
    //         PARTITION p3 VALUES LESS THAN MAXVALUE);
    // > RENAME TABLE test.employees TO test.emp;
    // > RENAME TABLE test.emp TO test.emp_bak;
    const auto test_path = TiFlashTestEnv::findTestDataPath("oncall-1651");
    auto ctx = TiFlashTestEnv::getContext(DB::Settings(), test_path);

    IDAsPathUpgrader upgrader(ctx, false, {});
    ASSERT_TRUE(upgrader.needUpgrade());
    upgrader.doUpgrade();

    {
        // After upgrade, next time we don't need it.
        IDAsPathUpgrader checker_after_upgrade(ctx, false, {});
        ASSERT_FALSE(checker_after_upgrade.needUpgrade());
    }

    // load metadata should not throw any exception
    loadMetadata(ctx);

    ASSERT_TRUE(ctx.isDatabaseExist("db_2")); // "test"
    auto & storages = ctx.getTMTContext().getStorages();
    ASSERT_NE(storages.get(45), nullptr); // `test`.`abc`
    ASSERT_NE(storages.get(48), nullptr); // `test`.`emp_bak`
    ASSERT_NE(storages.get(49), nullptr);
    ASSERT_NE(storages.get(50), nullptr);
    ASSERT_NE(storages.get(51), nullptr);
    ASSERT_NE(storages.get(52), nullptr);
}
CATCH

TEST_F(IDAsPathUpgrader_test, FLASH_1136_4_0_0_rc)
try
{
    if (!isEnabled())
        return;

    // prepare a "test" database for upgrader
    MockTiDB::instance().newDataBase("test");    // id == 2
    MockTiDB::instance().newDataBase("test-db"); // id == 3

    // Generated by running these SQL on cluster version v4.0.0-rc
    // > create database `test-db`;
    // > create table `test-tbl` (pk int);
    // > cerate table `test`.`test-tbl` (pk int);
    const auto test_path = TiFlashTestEnv::findTestDataPath("flash-1136");
    auto ctx = TiFlashTestEnv::getContext(DB::Settings(), test_path);

    IDAsPathUpgrader upgrader(ctx, false, {});
    ASSERT_TRUE(upgrader.needUpgrade());
    upgrader.doUpgrade();

    {
        // After upgrade, next time we don't need it.
        IDAsPathUpgrader checker_after_upgrade(ctx, false, {});
        ASSERT_FALSE(checker_after_upgrade.needUpgrade());
    }

    // load metadata should not throw any exception
    loadMetadata(ctx);

    ASSERT_TRUE(ctx.isDatabaseExist("db_2"));
    ASSERT_TRUE(ctx.isDatabaseExist("db_3"));
    auto & storages = ctx.getTMTContext().getStorages();
    ASSERT_NE(storages.get(66), nullptr);  // `test-db`.`test-tbl`
    ASSERT_NE(storages.get(666), nullptr); // `test`.`test-tbl`
}
CATCH

TEST_F(IDAsPathUpgrader_test, FLASH_1136_3_1_0)
try
{
    if (!isEnabled())
        return;

    // prepare a "test" database for upgrader
    MockTiDB::instance().newDataBase("test");    // id == 2
    MockTiDB::instance().newDataBase("test-db"); // id == 3

    // Generated by running these SQL on cluster version v4.0.0-rc
    // > create database `test-db`;
    // > create table `test-tbl` (pk int);
    // > cerate table `test`.`test-tbl` (pk int);
    const auto test_path = TiFlashTestEnv::findTestDataPath("flash-1136-v3.1.0");
    auto ctx = TiFlashTestEnv::getContext(DB::Settings(), test_path);

    IDAsPathUpgrader upgrader(ctx, false, {});
    ASSERT_TRUE(upgrader.needUpgrade());
    upgrader.doUpgrade();

    {
        // After upgrade, next time we don't need it.
        IDAsPathUpgrader checker_after_upgrade(ctx, false, {});
        ASSERT_FALSE(checker_after_upgrade.needUpgrade());
    }

    // load metadata should not throw any exception
    loadMetadata(ctx);

    ASSERT_TRUE(ctx.isDatabaseExist("db_2"));
    ASSERT_TRUE(ctx.isDatabaseExist("db_3"));
    auto & storages = ctx.getTMTContext().getStorages();
    ASSERT_NE(storages.get(66), nullptr);  // `test-db`.`test-tbl`
    ASSERT_NE(storages.get(666), nullptr); // `test`.`test-tbl`
}
CATCH

} // namespace DB::tests
