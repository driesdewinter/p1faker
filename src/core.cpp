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

    std::map<std::string_view, producer*> producers;
    std::map<std::string_view, policy*> policies;
    std::map<std::string_view, consumer*> consumers;
    config::param<std::string> default_policy{"default_policy", ""};
    config::param<double> standard_voltage{"standard_voltage", 230.0};
    settings::object<std::string> active_policy;
    budget current_budget;

private:
    registry()
    : active_policy{"active_policy", default_policy.get()}
    {}

    friend class mutex_protected<registry>;
};

auto curcap_rpc = www::rpc::get("curcap", [] {
    auto reg = registry::lock();
    return int(reg->current_budget.current * reg->standard_voltage.get() * int(phase::count));
});

auto policies_rpc = www::rpc::get("policies", [] {
    auto reg = registry::lock();
    nlohmann::json out;
    out["active_policy"] = reg->active_policy;
    auto& policies = out["policies"];
    for (auto&& [name, ptr] : reg->policies)
    {
        policies[std::string{name}] = {
            {"icon", ptr->icon()},
            {"label", ptr->label()},
            {"description", ptr->description()}
        };
    }
    return out;
});

auto activate_policy_rpc = www::rpc::post<std::string>("activate_policy", [](const std::string& name) {
    auto reg = registry::lock();
    reg->active_policy.edit() = name;
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
    auto it = reg->producers.find(name());
    logfinfo("%s producer %s", it == reg->producers.end() ? "Register" : "Overrule", name());
    if (it != reg->producers.end()) reg->producers.erase(it);
    reg->producers.emplace(name(), this);
}

producer::~producer()
{
    auto reg = registry::lock();
    auto it = reg->producers.find(name());
    if (it == reg->producers.end()) return;
    if (it->second != this) return; // this producer might have been overruled in the meantime, so no guarantee that we find ourselves.
    logfinfo("Unregister producer %s", name());
    reg->producers.erase(it);
}

policy::policy(std::string_view _name)
: m_name(_name)
{
    auto reg = registry::lock();
    auto it = reg->policies.find(name());
    logfinfo("%s %spolicy %s",
            it == reg->policies.end() ? "Register" : "Overrule",
            name() == reg->active_policy.get() ? "currently active " : "",
            name());
    if (it != reg->policies.end()) reg->policies.erase(it);
    reg->policies.emplace(name(), this);
}

policy::~policy()
{
    auto reg = registry::lock();
    auto it = reg->policies.find(name());
    if (it == reg->policies.end()) return;
    if (it->second != this) return; // this policy might have been overruled in the meantime, so no guarantee that we find ourselves.
    logfinfo("Unregister %spolicy %s", name() == reg->active_policy.get() ? "currently active " : "", name());
    reg->policies.erase(it);
}

consumer::consumer(std::string_view _name)
: m_name(_name)
{
    auto reg = registry::lock();
    auto it = reg->consumers.find(name());
    logfinfo("%s consumer %s", it == reg->consumers.end() ? "Register" : "Overrule", name());
    if (it != reg->consumers.end()) reg->consumers.erase(it);
    reg->consumers.emplace(name(), this);
}

consumer::~consumer()
{
    auto reg = registry::lock();
    auto it = reg->consumers.find(name());
    if (it == reg->consumers.end()) return;
    if (it->second != this) return; // this producer might have been overruled in the meantime, so no guarantee that we find ourselves.
    logfinfo("Unregister consumer %s", name());
    reg->consumers.erase(it);
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
    std::string active_policy;

    situation sit;

    do
    {
        auto reg = registry::lock();
        for (auto&& [name, producer] : reg->producers)
            producer->poll(sit);
        auto policy_it = reg->policies.find(reg->active_policy.get());
        if (reg->active_policy.get() != active_policy)
        {
            logfinfo("Activating %spolicy %s", policy_it == reg->policies.end() ? "unknown " : "", reg->active_policy.get());
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
