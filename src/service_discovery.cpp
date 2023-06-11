#include "service_discovery.h"

#include "logf.h"
#include "mutex_protected.h"

#include <thread>
#include <map>
#include <optional>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>

using namespace service_discovery;

namespace {

struct registry {
    static auto lock() {
        static mutex_protected<registry> instance;
        return instance.lock();
    }

    struct avahi_simple_poll_deleter { void operator()(AvahiSimplePoll* p) { avahi_simple_poll_free(p); }};
    struct avahi_client_deleter { void operator()(AvahiClient* p) { avahi_client_free(p); }};
    struct avahi_service_browser_deleter { void operator()(AvahiServiceBrowser* p) { avahi_service_browser_free(p); }};
    struct avahi_service_resolver_deleter { void operator()(AvahiServiceResolver* p) { avahi_service_resolver_free(p); }};
    struct resolver_info {
        std::unique_ptr<AvahiServiceResolver, avahi_service_resolver_deleter> resolver;
        std::optional<subscriber::service> result;
    };
    struct service_key {
        int ifindex;
        int protocol;
        std::string name;
        std::string domain;

        auto operator<=>(const service_key& r) const = default;
    };
    struct browser_info {
        std::unique_ptr<AvahiServiceBrowser, avahi_service_browser_deleter> browser;
        std::vector<subscriber*> subscribers;
        std::map<service_key, resolver_info> resolvers;
    };

    std::unique_ptr<AvahiSimplePoll, avahi_simple_poll_deleter> m_simple_poll{avahi_simple_poll_new()};
    std::unique_ptr<AvahiClient, avahi_client_deleter> m_client;
    std::map<std::string, browser_info> m_browsers;

    std::thread m_thread;

    registry() {
        int error = 0;

        m_simple_poll.reset(avahi_simple_poll_new());
        if (not m_simple_poll) {
            logferror("avahi_simple_poll_new() failed.");
            return;
        }

        m_client.reset(avahi_client_new(avahi_simple_poll_get(m_simple_poll.get()), AvahiClientFlags(0),
            [](AvahiClient* c, AvahiClientState state, void*) {
                if (state == AVAHI_CLIENT_FAILURE)
                    logferror("avahi client failure: %s", avahi_strerror(avahi_client_errno(c)));
            }, nullptr, &error));
        if (not m_client) {
            logferror("avahi_client_new() failed: %s", avahi_strerror(error));
            return;
        }

        m_thread = std::thread{[this] {
            avahi_simple_poll_loop(m_simple_poll.get());
        }};
    }

    ~registry() {
        if (m_simple_poll)
            avahi_simple_poll_quit(m_simple_poll.get());
        if (m_thread.joinable())
            m_thread.join();
    }

    static void on_browse_result(
            AvahiServiceBrowser*, AvahiIfIndex ifindex, AvahiProtocol protocol, AvahiBrowserEvent event,
            const char* name, const char* type, const char* domain, AvahiLookupResultFlags, void*) {

        auto reg = lock();

        if (event == AVAHI_BROWSER_FAILURE)
            return logferror("avahi browser failure for type %s: %s", type, avahi_strerror(avahi_client_errno(reg->m_client.get())));
        if (not (event == AVAHI_BROWSER_NEW or event == AVAHI_BROWSER_REMOVE))
            return; // ignore uninteresting events

        auto& b = reg->m_browsers[type];
        service_key key{ifindex, protocol, name, domain};
        resolver_info& r = b.resolvers[key];

        if (event == AVAHI_BROWSER_NEW) {
            logfdebug("Found new service %s for type %s (ifindex=%d, protocol=%d, domain=%s)", name, type, ifindex, protocol, domain);
            r.resolver.reset(avahi_service_resolver_new(reg->m_client.get(),
                    ifindex, protocol, name, type, domain,
                    AVAHI_PROTO_UNSPEC, AvahiLookupFlags(0), &registry::on_resolve, nullptr));
        } else if (event == AVAHI_BROWSER_REMOVE) {
            logfdebug("Remove service %s for type %s (ifindex=%d, protocol=%d, domain=%s)", name, type, ifindex, protocol, domain);
            if (r.result)
                for (auto& s : b.subscribers)
                    s->lost(*r.result);
            b.resolvers.erase(key);
        }
    }

    static void on_resolve(
        AvahiServiceResolver*, AvahiIfIndex ifindex, AvahiProtocol protocol, AvahiResolverEvent event,
        const char* name, const char* type, const char* domain, const char * /*host_name*/,
        const AvahiAddress* address, uint16_t port,
        AvahiStringList* /*txt*/, AvahiLookupResultFlags, void*) {

        auto reg = lock();

        if (event == AVAHI_RESOLVER_FAILURE)
            return logferror("avahi resolver failure for service %s of type %s: %s", name, type, avahi_strerror(avahi_client_errno(reg->m_client.get())));
        if (not (event == AVAHI_RESOLVER_FOUND))
            return; // ignore unkown events

        auto& b = reg->m_browsers[type];
        service_key key{ifindex, protocol, name, domain};
        resolver_info& r = b.resolvers[key];

        if (event == AVAHI_RESOLVER_FOUND) [&] {
            subscriber::service result;
            result.ifindex = ifindex;
            result.name = name;
            if (address->proto == AVAHI_PROTO_INET) {
                result.address = boost::asio::ip::address{boost::asio::ip::address_v4{ntohl(address->data.ipv4.address)}};
            } else if (address->proto == AVAHI_PROTO_INET6) {
                boost::asio::ip::address_v6::bytes_type bytes;
                std::copy(&address->data.ipv6.address[0], &address->data.ipv6.address[16], bytes.begin());
                result.address = boost::asio::ip::address{boost::asio::ip::address_v6{bytes}};
            } else {
                return logferror("avahi resolver returned protocol %d for service %s of type %s", address->proto, name, type);
            }
            result.port = port;

            logfdebug("Resolved service %s for type %s (ifindex=%d, protocol=%d, domain=%s): %s:%d", name, type, ifindex, protocol, domain, result.address, result.port);

            if (r.result)
                for (auto& s : b.subscribers)
                    if (s->match(r.result->name))
                        s->lost(*r.result);
            r.result = result;
            if (r.result)
                for (auto& s : b.subscribers)
                    if (s->match(r.result->name))
                        s->resolved(*r.result);
        }();
    }

    void add_subscriber(const std::string& type, subscriber* that) {
        auto& b = m_browsers[type];
        if (m_client and not b.browser) {
            b.browser.reset(avahi_service_browser_new(
                m_client.get(), AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, type.c_str(), nullptr, AvahiLookupFlags(0),
                &registry::on_browse_result, nullptr));
            if (not b.browser)
                return logferror("avahi_service_browser_new() failed: %s", avahi_strerror(avahi_client_errno(m_client.get())));
        }
        for (auto&& [name, r] : b.resolvers) {
            if (not r.result) continue;
            if (that->match(r.result->name))
                that->resolved(*r.result);
        }
        b.subscribers.push_back(that);
    }

    void remove_subscriber(const std::string& type, subscriber* that) {
        auto& b = m_browsers[type];
        std::erase(b.subscribers, that);
        if (b.subscribers.empty())
            m_browsers.erase(type);
    }
};

} // anonymous namespace

subscriber::subscriber(const std::string& type)
: m_type(type) {
    registry::lock()->add_subscriber(m_type, this);
}

subscriber::~subscriber() {
    registry::lock()->remove_subscriber(m_type, this);
}
