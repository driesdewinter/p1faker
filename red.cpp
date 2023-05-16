#include "core.h"
#include "config.h"
#include "logf.h"

namespace
{

struct policy_impl : core::policy
{
    policy_impl() : core::policy("red") {}

    core::budget apply(const core::situation& sit)
    {
        auto maxac = std::max_element(sit.ac.begin(), sit.ac.end(),
                [](const auto& l, const auto& r) { return l.current() < r.current(); });
        return {m_max_current.get() - maxac->current()};
    }

    config::param<double> m_max_current{"max_current", 0.0};
} impl;

}


