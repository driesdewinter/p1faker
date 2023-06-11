#ifndef SRC_SERVICE_DISCOVERY_H_
#define SRC_SERVICE_DISCOVERY_H_

#include "mutex_protected.h"

#include <string>
#include <vector>
#include <boost/asio/ip/address.hpp>

namespace service_discovery {

class subscriber {
public:
    struct service {
        std::string name;
        int ifindex;
        boost::asio::ip::address address;
        uint16_t port;

        auto operator<=>(const service& r) const = default;
    };

    virtual bool match(std::string_view /*name*/) { return true; } // whether it should be resolved

    virtual void resolved(const service& v) {
        auto s = m_services.lock();
        s->push_back(v);
    }
    virtual void lost(const service& v) {
        auto s = m_services.lock();
        std::erase(*s, v);
    }

protected:
    subscriber(const std::string& type); // e.g. "_http._tcp"
    subscriber(const subscriber&) = delete;
    subscriber& operator=(const subscriber&) = delete;
    virtual ~subscriber();

    const std::string m_type;
    mutex_protected<std::vector<service>> m_services;
};
} // namespace

#endif /* SRC_SERVICE_DISCOVERY_H_ */
