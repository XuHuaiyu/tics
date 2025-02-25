#include <Common/ClickHouseRevision.h>
#include <Common/ErrorExporter.h>
#include <Common/TiFlashBuildInfo.h>
#include <Common/config.h>
#include <IO/WriteBufferFromFile.h>
#include <common/config_common.h>
#include <config_tools.h>

#include <iostream>
#include <string>
#include <utility> /// pair
#include <vector>

#if USE_TCMALLOC
#include <gperftools/malloc_extension.h>
#endif

#if ENABLE_CLICKHOUSE_SERVER
#include "Server.h"
#endif
#if ENABLE_CLICKHOUSE_LOCAL
#include "LocalServer.h"
#endif
#if ENABLE_TIFLASH_DTTOOL
#include <Server/DTTool/DTTool.h>
#endif
#include <Common/StringUtils/StringUtils.h>
#include <Server/DTTool/DTTool.h>

/// Universal executable for various clickhouse applications
#if ENABLE_CLICKHOUSE_SERVER
int mainEntryClickHouseServer(int argc, char ** argv);
#endif
#if ENABLE_CLICKHOUSE_CLIENT
int mainEntryClickHouseClient(int argc, char ** argv);
#endif
#if ENABLE_CLICKHOUSE_LOCAL
int mainEntryClickHouseLocal(int argc, char ** argv);
#endif
#if ENABLE_CLICKHOUSE_BENCHMARK
int mainEntryClickHouseBenchmark(int argc, char ** argv);
#endif
#if ENABLE_CLICKHOUSE_PERFORMANCE
int mainEntryClickHousePerformanceTest(int argc, char ** argv);
#endif
#if ENABLE_CLICKHOUSE_TOOLS
int mainEntryClickHouseExtractFromConfig(int argc, char ** argv);
int mainEntryClickHouseCompressor(int argc, char ** argv);
int mainEntryClickHouseFormat(int argc, char ** argv);
#endif
#if ENABLE_CLICKHOUSE_COPIER
int mainEntryClickHouseClusterCopier(int argc, char ** argv);
#endif

#if USE_EMBEDDED_COMPILER
int mainEntryClickHouseClang(int argc, char ** argv);
int mainEntryClickHouseLLD(int argc, char ** argv);
#endif

extern "C" void print_raftstore_proxy_version();

int mainEntryVersion(int, char **)
{
    TiFlashBuildInfo::outputDetail(std::cout);
    std::cout << std::endl;

    std::cout << "Raft Proxy" << std::endl;
    print_raftstore_proxy_version();
    return 0;
}

int mainExportError(int argc, char ** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage:" << std::endl;
        std::cerr << "\ttiflash errgen [DST]" << std::endl;
        return -1;
    }
    std::string dst_path = argv[1];
    DB::WriteBufferFromFile wb(dst_path);
    auto & registry = DB::TiFlashErrorRegistry::instance();
    auto all_errors = registry.allErrors();

    {
        // RAII
        DB::ErrorExporter exporter(wb);
        for (auto error : all_errors)
        {
            exporter.writeError(error);
        }
    }
    return 0;
}

namespace
{
using MainFunc = int (*)(int, char **);


/// Add an item here to register new application
std::pair<const char *, MainFunc> clickhouse_applications[] = {
#if ENABLE_CLICKHOUSE_LOCAL
    {"local", mainEntryClickHouseLocal},
#endif
#if ENABLE_CLICKHOUSE_CLIENT
    {"client", mainEntryClickHouseClient},
#endif
#if ENABLE_CLICKHOUSE_BENCHMARK
    {"benchmark", mainEntryClickHouseBenchmark},
#endif
#if ENABLE_CLICKHOUSE_SERVER
    {"server", mainEntryClickHouseServer},
#endif
#if ENABLE_CLICKHOUSE_PERFORMANCE
    {"performance-test", mainEntryClickHousePerformanceTest},
#endif
#if ENABLE_CLICKHOUSE_TOOLS
    {"extract-from-config", mainEntryClickHouseExtractFromConfig},
    {"compressor", mainEntryClickHouseCompressor},
    {"format", mainEntryClickHouseFormat},
#endif
#if ENABLE_CLICKHOUSE_COPIER
    {"copier", mainEntryClickHouseClusterCopier},
#endif
#if USE_EMBEDDED_COMPILER
    {"clang", mainEntryClickHouseClang},
    {"clang++", mainEntryClickHouseClang},
    {"lld", mainEntryClickHouseLLD},
#endif
#if ENABLE_TIFLASH_DTTOOL
    {"dttool", DTTool::mainEntryTiFlashDTTool},
#endif
    {"version", mainEntryVersion},
    {"errgen", mainExportError}};


int printHelp(int, char **)
{
    std::cerr << "Use one of the following commands:" << std::endl;
    for (auto & application : clickhouse_applications)
        std::cerr << "tiflash " << application.first << " [args] " << std::endl;
    return -1;
};


bool isClickhouseApp(const std::string & app_suffix, std::vector<char *> & argv)
{
    /// Use app if the first arg 'app' is passed (the arg should be quietly removed)
    if (argv.size() >= 2)
    {
        auto first_arg = argv.begin() + 1;

        /// 'tiflash --client ...' and 'tiflash client ...' are Ok
        if (*first_arg == "--" + app_suffix || *first_arg == app_suffix)
        {
            argv.erase(first_arg);
            return true;
        }
    }

    /// Use app if tiflash binary is run through symbolic link with name tiflash-app
    std::string app_name = "tiflash-" + app_suffix;
    return !argv.empty() && (app_name == argv[0] || endsWith(argv[0], "/" + app_name));
}

} // namespace


int main(int argc_, char ** argv_)
{
#if USE_EMBEDDED_COMPILER
    if (argc_ >= 2 && 0 == strcmp(argv_[1], "-cc1"))
        return mainEntryClickHouseClang(argc_, argv_);
#endif

#if USE_TCMALLOC
    /** Without this option, tcmalloc returns memory to OS too frequently for medium-sized memory allocations
      *  (like IO buffers, column vectors, hash tables, etc.),
      *  that lead to page faults and significantly hurts performance.
      */
    MallocExtension::instance()->SetNumericProperty("tcmalloc.aggressive_memory_decommit", false);
#endif

    std::vector<char *> argv(argv_, argv_ + argc_);

    /// Print a basic help if nothing was matched
    MainFunc main_func = printHelp;

    for (auto & application : clickhouse_applications)
    {
        if (isClickhouseApp(application.first, argv))
        {
            main_func = application.second;
            break;
        }
    }

    return main_func(static_cast<int>(argv.size()), argv.data());
}
