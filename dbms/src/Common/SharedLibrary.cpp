#include "SharedLibrary.h"

#include <dlfcn.h>

#include <boost/core/noncopyable.hpp>
#include <string>

#include "Exception.h"

namespace DB
{
namespace ErrorCodes
{
extern const int CANNOT_DLOPEN;
extern const int CANNOT_DLSYM;
} // namespace ErrorCodes

SharedLibrary::SharedLibrary(const std::string & path)
{
    handle = dlopen(path.c_str(), RTLD_LAZY);
    if (!handle)
        throw Exception(std::string("Cannot dlopen: ") + dlerror(), ErrorCodes::CANNOT_DLOPEN);
}

SharedLibrary::~SharedLibrary()
{
    if (handle && dlclose(handle))
        std::terminate();
}

void * SharedLibrary::getImpl(const std::string & name, bool no_throw)
{
    dlerror();

    auto res = dlsym(handle, name.c_str());

    if (char * error = dlerror())
    {
        if (no_throw)
            return nullptr;
        throw Exception(std::string("Cannot dlsym: ") + error, ErrorCodes::CANNOT_DLSYM);
    }

    return res;
}
} // namespace DB
