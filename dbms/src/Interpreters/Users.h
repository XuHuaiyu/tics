#pragma once

#include <Core/Types.h>

#include <memory>
#include <unordered_set>
#include <vector>


namespace Poco
{
namespace Net
{
class IPAddress;
}

namespace Util
{
class AbstractConfiguration;
}
} // namespace Poco


namespace DB
{


/// Allow to check that address matches a pattern.
class IAddressPattern
{
public:
    virtual bool contains(const Poco::Net::IPAddress & addr) const = 0;
    virtual ~IAddressPattern() {}
};


class AddressPatterns
{
private:
    using Container = std::vector<std::shared_ptr<IAddressPattern>>;
    Container patterns;

public:
    bool contains(const Poco::Net::IPAddress & addr) const;
    void addFromConfig(const String & config_elem, Poco::Util::AbstractConfiguration & config);
};


/** User and ACL.
  */
struct User
{
    String name;

    /// Required password. Could be stored in plaintext or in SHA256.
    String password;
    String password_sha256_hex;

    String profile;
    String quota;

    AddressPatterns addresses;

    /// List of allowed databases.
    using DatabaseSet = std::unordered_set<std::string>;
    DatabaseSet databases;

    User(const String & name_);
    User(const String & name_, const String & config_elem, Poco::Util::AbstractConfiguration & config);

    constexpr static const char * DEFAULT_USER_NAME = "default";
    static const User & getDefaultUser();
};


} // namespace DB
