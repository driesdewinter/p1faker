#include "core.h"
#include "logf.h"
#include "mutex_protected.h"
#include "www.h"

namespace
{

config::param<int> default_car_min_power{"simulator.car_min_power", 2000};
config::param<int> default_car_max_power{"simulator.car_max_power", 7000};
config::param<int> default_inverter_max_power{"inverter_max_power", 8000};
config::param<int> default_battery_max_power{"battery_max_power", 5000};

struct simulator : core::producer, core::consumer
{
    struct state
    {
        struct input {
            int car_min_power = default_car_min_power.get();
            int car_max_power = default_car_max_power.get();
            int inverter_max_power = default_inverter_max_power.get();
            int battery_max_power = default_battery_max_power.get();
            int battery_state = 50;
            int solar_power = 0;
            std::vector<int> house_power;
        };
        struct output
        {
            int car_power = 0;
            int battery_output = 0;
            std::vector<int> grid_power;
        };
        input i;
        output o;
    };
    friend void to_json(nlohmann::json&, const state::input&);
    friend void from_json(const nlohmann::json&, state::input&);
    friend void to_json(nlohmann::json&, const state::output&);
    friend void from_json(const nlohmann::json&, state::output&);
    mutex_protected<state> m_state;
    www::rpc m_get_input = www::rpc::get("simulator/input", [this] {
        return m_state.lock()->i;
    });
    www::rpc m_set_input = www::rpc::post<state::input>("simulator/input", [this] (const auto& s) {
        m_state.lock()->i = s;
    });
    www::rpc m_get_output = www::rpc::get("simulator/output", [this] {
        return m_state.lock()->o;
    });

    simulator() : core::producer("simulator"), core::consumer("simulator") {}

    void poll(core::situation& sit) override
    {
        auto s = m_state.lock();
        s->i.house_power.resize(sit.grid.size());
        s->o.grid_power.resize(sit.grid.size());
        for (std::size_t phase = 0; phase < sit.grid.size(); phase++)
        {
            s->o.grid_power[phase] = s->i.house_power[phase] + (s->o.car_power - s->i.solar_power) / int(sit.grid.size());
        }
        int grid_power = std::accumulate(s->o.grid_power.begin(), s->o.grid_power.end(), 0);

        sit.battery_state = 0.01 * s->i.battery_state;

        if (s->i.battery_state <= 99 and grid_power < 0)
        {
            s->o.battery_output = std::max(grid_power, -s->i.battery_max_power);
        }
        else if (s->i.battery_state >= 1 and grid_power > 0)
        {
            s->o.battery_output = std::min(grid_power, std::min(s->i.battery_max_power, s->i.inverter_max_power - s->i.solar_power));
        }
        else
        {
            s->o.battery_output = 0.0;
        }
        sit.battery_output = s->o.battery_output;
        sit.inverter_output = sit.battery_output + s->i.solar_power;
        for (std::size_t phase = 0; phase < sit.grid.size(); phase++)
        {
            s->o.grid_power[phase] -= s->o.battery_output / int(sit.grid.size());
            sit.grid[phase].current = s->o.grid_power[phase] / sit.grid[phase].voltage;
        }
    }

    void handle(const core::budget& b, const core::situation& sit) override
    {
        auto s = m_state.lock();
        int car_power = s->o.car_power + int(b.current * sit.grid_voltage() * sit.grid.size());
        if (car_power > s->i.car_max_power) car_power = s->i.car_max_power;
        if (car_power < s->i.car_min_power) car_power = 0;
        if (s->o.car_power != car_power) logfinfo("Charging car at %s W", car_power);
        s->o.car_power = car_power;
    }
};

std::optional<simulator> impl;
config::param<bool> enable{"simulator.enable", false};
int _ = [] {
    if (enable) impl.emplace();
    return 0;
}();

void from_json(const nlohmann::json& j, simulator::state::input& s)
{
    j.at("battery_state").get_to(s.battery_state);
    j.at("solar_power").get_to(s.solar_power);
    j.at("car_min_power").get_to(s.car_min_power);
    j.at("car_max_power").get_to(s.car_max_power);
    j.at("inverter_max_power").get_to(s.inverter_max_power);
    j.at("battery_max_power").get_to(s.battery_max_power);
    for (std::size_t i = 0; ; i++)
    {
        auto it = j.find(str(boost::format("house_power_l%d") % (i + 1)));
        if (it == j.end()) break;
        s.house_power.push_back(it->get<int>());
    }
}

void to_json(nlohmann::json& j, const simulator::state::input& s)
{
    j = nlohmann::json{
        {"battery_state", s.battery_state},
        {"solar_power", s.solar_power},
        {"car_min_power", s.car_min_power},
        {"car_max_power", s.car_max_power},
        {"inverter_max_power", s.inverter_max_power},
        {"battery_max_power", s.battery_max_power},
    };
    for (std::size_t i = 0; i < s.house_power.size(); i++)
        j[str(boost::format("house_power_l%d") % (i + 1))] = s.house_power[i];
}

void to_json(nlohmann::json& j, const simulator::state::output& s)
{
    j = nlohmann::json{
        {"car_power", s.car_power},
        {"battery_output", s.battery_output},
    };
    for (std::size_t i = 0; i < s.grid_power.size(); i++)
        j[str(boost::format("grid_power_l%d") % (i + 1))] = s.grid_power[i];
}

} // anonymous namespace
