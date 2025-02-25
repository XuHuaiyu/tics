#include <Databases/IDatabase.h>
#include <Storages/System/attachSystemTables.h>

#include <Storages/System/StorageSystemAsynchronousMetrics.h>
#include <Storages/System/StorageSystemBuildOptions.h>
#include <Storages/System/StorageSystemClusters.h>
#include <Storages/System/StorageSystemColumns.h>
#include <Storages/System/StorageSystemDatabases.h>
#include <Storages/System/StorageSystemDictionaries.h>
#include <Storages/System/StorageSystemDTSegments.h>
#include <Storages/System/StorageSystemDTTables.h>
#include <Storages/System/StorageSystemEvents.h>
#include <Storages/System/StorageSystemFunctions.h>
#include <Storages/System/StorageSystemGraphite.h>
#include <Storages/System/StorageSystemMacros.h>
#include <Storages/System/StorageSystemMerges.h>
#include <Storages/System/StorageSystemMetrics.h>
#include <Storages/System/StorageSystemModels.h>
#include <Storages/System/StorageSystemNumbers.h>
#include <Storages/System/StorageSystemOne.h>
#include <Storages/System/StorageSystemParts.h>
#include <Storages/System/StorageSystemPartsColumns.h>
#include <Storages/System/StorageSystemProcesses.h>
#include <Storages/System/StorageSystemSettings.h>
#include <Storages/System/StorageSystemTables.h>


namespace DB
{

void attachSystemTablesLocal(IDatabase & system_database)
{
    system_database.attachTable("one", StorageSystemOne::create("one"));
    system_database.attachTable("numbers", StorageSystemNumbers::create("numbers", false));
    system_database.attachTable("numbers_mt", StorageSystemNumbers::create("numbers_mt", true));
    system_database.attachTable("databases", StorageSystemDatabases::create("databases"));
    system_database.attachTable("dt_tables", StorageSystemDTTables::create("dt_tables"));
    system_database.attachTable("dt_segments", StorageSystemDTSegments::create("dt_segments"));
    system_database.attachTable("tables", StorageSystemTables::create("tables"));
    system_database.attachTable("columns", StorageSystemColumns::create("columns"));
    system_database.attachTable("functions", StorageSystemFunctions::create("functions"));
    system_database.attachTable("events", StorageSystemEvents::create("events"));
    system_database.attachTable("settings", StorageSystemSettings::create("settings"));
    system_database.attachTable("build_options", StorageSystemBuildOptions::create("build_options"));
}

void attachSystemTablesServer(IDatabase & system_database)
{
    attachSystemTablesLocal(system_database);
    system_database.attachTable("parts", StorageSystemParts::create("parts"));
    system_database.attachTable("parts_columns", StorageSystemPartsColumns::create("parts_columns"));
    system_database.attachTable("processes", StorageSystemProcesses::create("processes"));
    system_database.attachTable("metrics", StorageSystemMetrics::create("metrics"));
    system_database.attachTable("merges", StorageSystemMerges::create("merges"));
    system_database.attachTable("dictionaries", StorageSystemDictionaries::create("dictionaries"));
    system_database.attachTable("models", StorageSystemModels::create("models"));
    system_database.attachTable("clusters", StorageSystemClusters::create("clusters"));
    system_database.attachTable("graphite_retentions", StorageSystemGraphite::create("graphite_retentions"));
    system_database.attachTable("macros", StorageSystemMacros::create("macros"));
}

void attachSystemTablesAsync(IDatabase & system_database, AsynchronousMetrics & async_metrics)
{
    system_database.attachTable("asynchronous_metrics", StorageSystemAsynchronousMetrics::create("asynchronous_metrics", async_metrics));
}

}
