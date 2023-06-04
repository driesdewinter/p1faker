#include "core.h"
#include "logf.h"
#include "mutex_protected.h"
#include "www.h"

namespace
{

struct monitor : core::consumer
{
    struct state
    {
        core::situation situation;
        core::budget budget;
    };
    friend void to_json(nlohmann::json&, const state&);
    mutex_protected<state> m_state;
    www::rpc m_getall = www::rpc::get("monitor", [this] {
        return *m_state.lock();
    });
    www::rpc m_curcap = www::rpc::get("curcap", [this] {
        auto s = m_state.lock();
        auto grid_voltage = std::accumulate(s->situation.ac.begin(), s->situation.ac.end(), 0.0,
                                [](double sum, const auto& ac) { return sum + ac.voltage; }) / s->situation.ac.size();
        return int(s->budget.current * grid_voltage * s->situation.ac.size());
    });

    monitor() : core::consumer("monitor") {}

    void handle(const core::budget& b, const core::situation& sit) override
    {
        auto s = m_state.lock();
        s->budget = b;
        s->situation = sit;
    }
} impl;

void to_json(nlohmann::json& j, const monitor::state& s)
{
    j = nlohmann::json{
        {"budget", nlohmann::json{
            {"current", s.budget.current}
        }},
        {"situation", nlohmann::json{
            {"battery_state", s.situation.battery_state},
            {"inverter_output", s.situation.inverter_output},
            {"battery_output", s.situation.battery_output},
            {"solar_output", s.situation.solar_output()},
            {"consumption", s.situation.consumption()},
            {"ac", [&] {
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& ac : s.situation.ac)
                {
                    arr.push_back(nlohmann::json{
                        {"voltage", ac.voltage},
                        {"current", ac.current},
                        {"power", ac.power()}
                    });
                }
                return arr;
            }()}
        }}
    };
}

} // anonymous namespace
