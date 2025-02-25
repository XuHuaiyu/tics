#pragma once

#include <Common/Stopwatch.h>
#include <Storages/IManageableStorage.h>

namespace DB
{
class GCManager
{
public:
    GCManager(Context & context)
        : global_context{context.getGlobalContext()}
        , log(&Poco::Logger::get("GCManager")){};

    ~GCManager() = default;

    bool work();

private:
    Context & global_context;

    TableID next_table_id = InvalidTableID;

    Poco::Logger * log;
};
} // namespace DB
