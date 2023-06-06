#include "core.h"
#include "settings.h"
#include "logf.h"

namespace
{
settings::param<int> next_policy{"next_policy", 0};
settings::param<int> default_next_delay{"default_next_delay", 7200};
settings::param<int> next_time{"next_time", 0};

struct producer_impl : core::producer {

    producer_impl() : core::producer("schedule") {}

    void poll(core::situation&) override {
        auto tnow = std::chrono::system_clock::now();
        using tp = decltype(tnow);
        auto tnext = tp{std::chrono::seconds{next_time}};
        if (tnow > tnext and tnext != tp{}) {
            settings::apply({{"active_policy", next_policy.get()}, {"next_time", 0}});
        }
    }
} impl;

}

