#include "core.h"

#include "mutex_protected.h"
#include "config.h"
#include "logf.h"
#include "settings.h"

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include <map>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>
#include <string_view>

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
    settings::object<std::string> active_policy;

private:
    registry()
    : active_policy{"active_policy", default_policy.get()}
    {}

    friend class mutex_protected<registry>;
};
} // anonymous namespace

producer::producer(std::string_view _name)
: m_name(_name)
{
    auto reg = registry::lock();
    auto it = reg->producers.find(name());
    if (it != reg->producers.end()) reg->producers.erase(it);
    logfinfo("%s producer %s", it == reg->producers.end() ? "Register" : "Overrule", name());
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

std::vector<std::string> policy::list()
{
    auto reg = registry::lock();
    std::vector<std::string> policynames;
    for (auto&& [name, p] : reg->policies)
        policynames.emplace_back(name);
    return policynames;
}

void policy::activate(std::string_view name)
{
    auto reg = registry::lock();
    reg->active_policy.edit() = std::string{name};
    logfinfo("Activating %spolicy %s", reg->policies.find(name) == reg->policies.end() ? "unknown " : "", name);
}

policy::policy(std::string_view _name)
: m_name(_name)
{
    auto reg = registry::lock();
    auto it = reg->policies.find(name());
    if (it != reg->policies.end()) reg->policies.erase(it);
    logfinfo("%s %spolicy %s",
            it == reg->policies.end() ? "Register" : "Overrule",
            name() == reg->active_policy.get() ? "currently active " : "",
            name());
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
    if (it != reg->consumers.end()) reg->consumers.erase(it);
    logfinfo("%s consumer %s", it == reg->consumers.end() ? "Register" : "Overrule", name());
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

    sigset_t sigset = {};
    sigaddset(&sigset, SIGTERM);
    sigaddset(&sigset, SIGINT);
    sigprocmask(SIG_BLOCK, &sigset, nullptr);
    auto t0 = std::chrono::system_clock::now();
    auto interval_config = config::param{"interval", 1000};
    auto interval = std::chrono::milliseconds{interval_config};
    auto budget_log_threshold = config::param<double>{"budget_log_threshold", 0.9};

    situation sit;
    budget b, bprev;

    {
        auto reg = registry::lock();
        logfinfo("Starting up with %spolicy %s", reg->policies.find(reg->active_policy.get()) == reg->policies.end() ? "unknown " : "", reg->active_policy.get());
    }

    do
    {
        auto reg = registry::lock();
        for (auto&& [name, producer] : reg->producers)
            producer->poll(sit);
        auto policy_it = reg->policies.find(reg->active_policy.get());
        if (policy_it != reg->policies.end())
            b = policy_it->second->apply(sit);
        if (abs(b.current - bprev.current) > budget_log_threshold)
        {
            logfdebug("Budget updated to %.1f A", b.current);
            bprev = b;
        }
        for (auto&& [name, consumer] : reg->consumers)
            consumer->handle(b, sit);
    } while ([&] {
        auto t1 = std::chrono::system_clock::now();
        if (t1 > t0 + interval) logfwarn("Finished current interval late: it took %s", duration_cast<std::chrono::milliseconds>(t1 - t0));
        t0 = std::max(t1, t0 + interval);
        timespec ts = { decltype(ts.tv_sec)((t0 - t1) / 1s), decltype(ts.tv_nsec)((t0 - t1) % 1s / 1ns) };
        return sigtimedwait(&sigset, nullptr, &ts) < 0;
    }());
}
