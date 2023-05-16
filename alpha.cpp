#include "core.h"
#include "config.h"

#include <numeric>

namespace
{

struct policy_impl : core::policy
{
    policy_impl() : core::policy("alpha") {}

    config::param<double> lift_budget_to = {"alpha.lift_budget_to", 9.0};
    config::param<double> lift_budget_min = {"alpha.lift_budget_min", 4.0};

    core::budget apply(const core::situation& sit)
    {
        double avgac = std::accumulate(sit.ac.begin(), sit.ac.end(), 0.0,
                [](double sum, const auto& ac) { return sum + ac.current(); }) / sit.ac.size();
        core::budget b;
        if      (avgac < -lift_budget_to.get())  b.current = -avgac;
        else if (avgac < -lift_budget_min.get()) b.current = lift_budget_to.get();
        else                                     b.current = -avgac;
        return b;
    }
} impl;

}


