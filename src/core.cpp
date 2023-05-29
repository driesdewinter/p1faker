#include "core.h"

#include "mutex_protected.h"
#include "config.h"
#include "logf.h"
#include "settings.h"
#include "www.h"

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include <map>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>

using namespace std::literals::chrono_literals;

using namespace core;

namespace
{
struct registry
{
    static auto lock()
    {
        static mutex_protected<registry> instance;
        return instance.lock();
    }

    std::map<int, producer*> producers;
    std::map<int, policy*> policies;
    std::map<int, consumer*> consumers;
    config::param<double> standard_voltage{"standard_voltage", 230.0};
    settings::param<int> active_policy{"active_policy", 0};
    budget current_budget;

private:
    registry() {}

    friend class mutex_protected<registry>;
};

template<typename T>
int next_id(const std::map<int, T>& map) {
    return map.empty() ? 0 : map.rbegin()->first + 1;
}

auto curcap_rpc = www::rpc::get("curcap", [] {
    auto reg = registry::lock();
    return int(reg->current_budget.current * reg->standard_voltage.get() * int(phase::count));
});

auto policies_rpc = www::rpc::get("policies", [] {
    auto reg = registry::lock();
    nlohmann::json policies;
    for (auto&& [index, ptr] : reg->policies)
    {
        policies.push_back({
            {"index", index},
            {"name", ptr->name()},
            {"icon", ptr->icon()},
            {"label", ptr->label()},
            {"description", ptr->description()}
        });
    }
    return policies;
});

struct sigsuppress_type
{
    sigsuppress_type()
    {
        sigaddset(&sigset, SIGTERM);
        sigaddset(&sigset, SIGINT);
        sigprocmask(SIG_BLOCK, &sigset, nullptr);
    }
    sigset_t sigset = {};
} sigsuppress;

} // anonymous namespace

producer::producer(std::string_view _name)
: m_name(_name)
{
    auto reg = registry::lock();
    m_index = next_id(reg->producers);
    logfdebug("Register producer %s", name());
    reg->producers.emplace(m_index, this);
}

producer::~producer()
{
    auto reg = registry::lock();
    logfdebug("Unregister producer %s (index %d)", name(), m_index);
    reg->producers.erase(m_index);
}

policy::policy(std::string_view _name)
: m_name(_name)
{
    auto reg = registry::lock();
    m_index = next_id(reg->policies);
    logfdebug("Register policy %s (index %d)", name(), m_index);
    reg->policies.emplace(m_index, this);
}

policy::~policy()
{
    auto reg = registry::lock();
    logfdebug("Unregister policy %s (index %d)", name(), m_index);
    reg->producers.erase(m_index);
}

consumer::consumer(std::string_view _name)
: m_name(_name)
{
    auto reg = registry::lock();
    m_index = next_id(reg->consumers);
    logfdebug("Register consumer %s (index %d)", name(), m_index);
    reg->consumers.emplace(m_index, this);
}

consumer::~consumer()
{
    auto reg = registry::lock();
    logfdebug("Unregister consumer %s (index %d)", name(), m_index);
    reg->consumers.erase(m_index);
}

int main(int argc, const char **argv)
{
    while (++argv, --argc)
    {
        if (std::strncmp(*argv, "--", 2) or argc < 2)
        {
            std::cerr << "Usage: p1gen [--option value]*\n\n";
            return -1;
        }
        const char* name = &(*argv)[2];
        argv++, argc--;
        config::set_param(name, *argv);
    }

    auto t0 = std::chrono::system_clock::now();
    auto interval_config = config::param{"interval", 1000};
    auto interval = std::chrono::milliseconds{interval_config};
    int active_policy = -1;

    situation sit;

    do
    {
        auto reg = registry::lock();
        for (auto&& [name, producer] : reg->producers)
            producer->poll(sit);
        auto policy_it = reg->policies.find(reg->active_policy.get());
        if (reg->active_policy.get() != active_policy)
        {
            logfinfo("Activating policy %s", policy_it == reg->policies.end() ? std::string_view{"null"} : policy_it->second->name());
            active_policy = reg->active_policy.get();
        }
        if (policy_it != reg->policies.end())
            reg->current_budget = policy_it->second->apply(sit);
        for (auto&& [name, consumer] : reg->consumers)
            consumer->handle(reg->current_budget, sit);
    } while ([&] {
        auto t1 = std::chrono::system_clock::now();
        if (t1 > t0 + interval) logfwarn("Finished current interval late: it took %s", duration_cast<std::chrono::milliseconds>(t1 - t0));
        t0 = std::max(t1, t0 + interval);
        timespec ts = { decltype(ts.tv_sec)((t0 - t1) / 1s), decltype(ts.tv_nsec)((t0 - t1) % 1s / 1ns) };
        return sigtimedwait(&sigsuppress.sigset, nullptr, &ts) < 0;
    }());
}
