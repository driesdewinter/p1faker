#include "service_discovery.h"

#include "logf.h"
#include "mutex_protected.h"

#include <thread>
#include <map>
#include <set>
#include <optional>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-client/lookup.h>
#include <avahi-common/thread-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>

using namespace service_discovery;

namespace {

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

struct registry {
    static auto lock() {
        static struct avahi_thread {
            avahi_thread() {
                m_p = avahi_threaded_poll_new();
                avahi_threaded_poll_start(m_p);
            }
            ~avahi_thread() {
                avahi_threaded_poll_stop(m_p);
                avahi_threaded_poll_free(m_p);
            }
            AvahiThreadedPoll* m_p;
        } thread;

        struct avahi_mutex {
            void lock() { avahi_threaded_poll_lock(thread.m_p); }
            void unlock() { avahi_threaded_poll_unlock(thread.m_p); }
        };
        static mutex_protected<registry, avahi_mutex> instance{avahi_threaded_poll_get(thread.m_p)};
        return instance.lock();
    }

    const AvahiPoll* m_poll;
    std::unique_ptr<AvahiClient, avahi_client_deleter> m_client;
    AvahiClientState m_client_state = AVAHI_CLIENT_CONNECTING;
    std::map<std::string, browser_info> m_browsers;

    struct client_cb {
        virtual void on_client_cb(AvahiClientState state) = 0;
    protected:
        virtual ~client_cb() {}
    };
    std::set<client_cb*> m_publishers;

    registry(const AvahiPoll* ppoll)
    : m_poll(ppoll) {
        int error = 0;

        m_client.reset(avahi_client_new(m_poll, AvahiClientFlags(0),
            [](AvahiClient* c, AvahiClientState state, void* userdata) {
                registry* reg = reinterpret_cast<registry*>(userdata);
                if (state == AVAHI_CLIENT_FAILURE)
                    logferror("avahi client failure: %s", avahi_strerror(avahi_client_errno(c)));
                reg->m_client_state = state;
                for (auto pub : reg->m_publishers)
                    pub->on_client_cb(reg->m_client_state);
            }, this, &error));
        if (not m_client)
            logferror("avahi_client_new() failed: %s", avahi_strerror(error));
    }
};

void on_resolve(
        AvahiServiceResolver*, AvahiIfIndex ifindex, AvahiProtocol protocol, AvahiResolverEvent event,
        const char* name, const char* type, const char* domain, const char * /*host_name*/,
        const AvahiAddress* address, uint16_t port,
        AvahiStringList* /*txt*/, AvahiLookupResultFlags, void* userdata) {
    auto reg = reinterpret_cast<registry*>(userdata);

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

void on_browse_result(
        AvahiServiceBrowser*, AvahiIfIndex ifindex, AvahiProtocol protocol, AvahiBrowserEvent event,
        const char* name, const char* type, const char* domain, AvahiLookupResultFlags, void* userdata) {
    auto reg = reinterpret_cast<registry*>(userdata);

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
                AVAHI_PROTO_UNSPEC, AvahiLookupFlags(0), on_resolve, &*reg));
    } else if (event == AVAHI_BROWSER_REMOVE) {
        logfdebug("Remove service %s for type %s (ifindex=%d, protocol=%d, domain=%s)", name, type, ifindex, protocol, domain);
        if (r.result)
            for (auto& s : b.subscribers)
                s->lost(*r.result);
        b.resolvers.erase(key);
    }
}



} // anonymous namespace

subscriber::subscriber(const std::string& type)
: m_type(type) {
    auto reg = registry::lock();
    auto& b = reg->m_browsers[type];
    if (reg->m_client and not b.browser) {
        b.browser.reset(avahi_service_browser_new(
            reg->m_client.get(), AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, type.c_str(), nullptr, AvahiLookupFlags(0),
            &on_browse_result, &*reg));
        if (not b.browser) {
            logferror("avahi_service_browser_new() failed: %s", avahi_strerror(avahi_client_errno(reg->m_client.get())));
            return;
        }
    }
    for (auto&& [name, r] : b.resolvers) {
        if (not r.result) continue;
        if (match(r.result->name))
            resolved(*r.result);
    }
    b.subscribers.push_back(this);
}

subscriber::~subscriber() {
    auto reg = registry::lock();
    auto& b = reg->m_browsers[m_type];
    std::erase(b.subscribers, this);
    if (b.subscribers.empty())
        reg->m_browsers.erase(m_type);
}

struct publisher::impl : registry::client_cb {
    std::string m_type;
    std::string m_name;
    uint16_t m_port;
    struct avahi_group_deleter { void operator()(AvahiEntryGroup* p) { avahi_entry_group_free(p); }};
    std::unique_ptr<AvahiEntryGroup, avahi_group_deleter> m_group;
    registry* m_reg; // need direct access from on_group_cb bypassing the lock

    impl(const std::string& type, const std::string& name, uint16_t port)
    : m_type(type), m_name(name), m_port(port) {
        auto reg = registry::lock();
        m_reg = &*reg;
        reg->m_publishers.insert(this);
        on_client_cb(reg->m_client_state);
    }

    ~impl() {
        auto reg = registry::lock();
        reg->m_publishers.erase(this);
        m_group.reset(); // with lock!
    }

    void on_group_cb(AvahiEntryGroupState state) {
        switch (state) {
            case AVAHI_ENTRY_GROUP_ESTABLISHED :
                logfdebug("Service '%s' successfully established", m_name);
                break;
            case AVAHI_ENTRY_GROUP_COLLISION :
                logferror("Service '%s' conflicts with other service on network. Giving up", m_name);
                break;
            case AVAHI_ENTRY_GROUP_FAILURE :
                logferror("Group entry failure for service '%s': %s", m_name, avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(m_group.get()))));
                break;
            case AVAHI_ENTRY_GROUP_UNCOMMITED:
            case AVAHI_ENTRY_GROUP_REGISTERING:
                ;
        }
    }

    void on_client_cb(AvahiClientState state) override {
        if (state == AVAHI_CLIENT_S_RUNNING) {
            m_group.reset(avahi_entry_group_new(m_reg->m_client.get(), [](AvahiEntryGroup*, AvahiEntryGroupState state, void* userdata) {
                reinterpret_cast<impl*>(userdata)->on_group_cb(state);
            }, this));
            int ret = avahi_entry_group_add_service(m_group.get(), AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, AvahiPublishFlags(0),
                    m_name.c_str(), m_type.c_str(), nullptr, nullptr, m_port, nullptr);
            if (ret < 0) {
                logferror("Failed to add service %s to group: %s", m_name, avahi_strerror(ret));
                return;
            }
            ret = avahi_entry_group_commit(m_group.get());
            if (ret < 0) {
                logferror("Failed to commit group for service %s: %s", m_name, avahi_strerror(ret));
                return;
            }
        } else {
            m_group.reset();
        }
    }
};

publisher::publisher(const std::string& type, const std::string& name, uint16_t port)
: m_pimpl(std::make_shared<impl>(type, name, port)) {}

