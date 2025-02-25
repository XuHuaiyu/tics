#include <Databases/DatabaseOrdinary.h>
#include <Databases/IDatabase.h>
#include <IO/ReadBufferFromFileDescriptor.h>
#include <IO/WriteBufferFromFileDescriptor.h>
#include <Interpreters/Context.h>
#include <Interpreters/executeQuery.h>
#include <Interpreters/loadMetadata.h>
#include <Poco/ConsoleChannel.h>
#include <Storages/StorageLog.h>
#include <Storages/System/attachSystemTables.h>
#include <common/DateLUT.h>

#include <iomanip>
#include <iostream>


using namespace DB;

int main(int, char **)
try
{
    Poco::AutoPtr<Poco::ConsoleChannel> channel = new Poco::ConsoleChannel(std::cerr);
    Poco::Logger::root().setChannel(channel);
    Poco::Logger::root().setLevel("trace");

    /// Pre-initialize the `DateLUT` so that the first initialization does not affect the measured execution speed.
    DateLUT::instance();

    Context context = Context::createGlobal();

    context.setPath("./");

    loadMetadata(context);

    DatabasePtr system = std::make_shared<DatabaseOrdinary>("system", "./metadata/system/", context);
    context.addDatabase("system", system);
    system->loadTables(context, nullptr, false);
    attachSystemTablesLocal(*context.getDatabase("system"));
    context.setCurrentDatabase("default");

    ReadBufferFromFileDescriptor in(STDIN_FILENO);
    WriteBufferFromFileDescriptor out(STDOUT_FILENO);

    executeQuery(in, out, /* allow_into_outfile = */ false, context, {});

    return 0;
}
catch (const Exception & e)
{
    std::cerr << e.what() << ", " << e.displayText() << std::endl
              << std::endl
              << "Stack trace:" << std::endl
              << e.getStackTrace().toString();
    return 1;
}
