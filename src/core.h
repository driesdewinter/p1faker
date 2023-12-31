#ifndef CORE_H_
#define CORE_H_

#include <vector>
#include <string_view>
#include <string>
#include <numeric>

namespace core {

struct situation {
    double battery_state = 1.0; // 0.0 .. 1.0
    double inverter_output = 0.0; // total yield of pv + battery
    double battery_output = 0.0; // battery discharge power (negative in case of charge)
    double solar_output() const { return inverter_output - battery_output; }
    struct grid_type {
        double power() const { return voltage * current; } // ignoring cos(phi)...
        double voltage = 230.0;
        double current = 0.0;
    };
    std::vector<grid_type> grid;
    double grid_voltage() const {
        return std::accumulate(grid.begin(), grid.end(), 0.0,
                [](double sum, const auto& ac) { return sum + ac.voltage; }) / grid.size();
    }
    double grid_output() const {
        return std::accumulate(grid.begin(), grid.end(), 0.0,
                [](double sum, const auto& ac) { return sum + ac.current * ac.voltage; });
    }
    double consumption() const {
        return inverter_output + grid_output();
    }
};

struct budget {
    double current = 0.0;
};

struct producer {
    std::string_view name() const { return m_name; }

    virtual void poll(situation&) = 0;

protected:
    producer(std::string_view name);
    virtual ~producer();
private:
    const std::string m_name;
    int m_index;
};

struct policy {
    std::string_view name() const { return m_name; }
    virtual std::string_view icon() const { return "wi-alien"; }
    virtual std::string_view label() const { return "Custom policy"; }
    virtual std::string_view description() const { return "This is an undocumented custom policy."; }

    virtual budget apply(const situation&) = 0;

protected:
    policy(std::string_view name);
    virtual ~policy();
private:
    static std::string input_field(std::string_view cls, std::string_view id);
    const std::string m_name;
    int m_index;
};

struct consumer {
    std::string_view name() const { return m_name; }

    virtual void handle(const budget&, const situation&) = 0;

protected:
    consumer(std::string_view name);
    virtual ~consumer();
private:
    const std::string m_name;
    int m_index;
};

} // namespace core

#endif /* CORE_H_ */
