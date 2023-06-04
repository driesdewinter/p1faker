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
        return int(s->budget.current * s->situation.grid_voltage() * s->situation.grid.size());
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
            {"grid", [&] {
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& phase : s.situation.grid)
                {
                    arr.push_back(nlohmann::json{
                        {"voltage", phase.voltage},
                        {"current", phase.current},
                        {"power", phase.power()}
                    });
                }
                return arr;
            }()}
        }}
    };
}

} // anonymous namespace
