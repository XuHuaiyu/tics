#include <Common/getFQDNOrHostName.h>
#include <Poco/Net/DNS.h>


namespace
{
std::string getFQDNOrHostNameImpl()
{
    try
    {
        return Poco::Net::DNS::thisHost().name();
    }
    catch (...)
    {
        return Poco::Net::DNS::hostName();
    }
}
} // namespace


const std::string & getFQDNOrHostName()
{
    static std::string result = getFQDNOrHostNameImpl();
    return result;
}
