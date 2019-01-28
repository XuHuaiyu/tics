#include <TiDB/TiDBService.h>

namespace DB
{

TiDBService::TiDBService(const std::string & service_ip_, const std::string & status_port_): service_ip(service_ip_),
    status_port(status_port_)
{
}

const std::string & TiDBService::serviceIp() const
{
    return service_ip;
}

const std::string & TiDBService::statusPort() const
{
    return status_port;
}

} // namespace DB
