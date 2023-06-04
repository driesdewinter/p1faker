#include "core.h"
#include "config.h"
#include "logf.h"
#include "modbus.h"

namespace
{

struct producer_impl : core::producer
{
    producer_impl()
    : core::producer("sma")
    , m_conn("SMA inverter", m_ip.get(), m_port.get())
    {}

    enum modbus_register
    {
        inverter_power = 30775,
        battery_charge = 31393,
        battery_discharge = 31395,
        battery_state_of_charge = 30845,
        grid_voltage_l1 = 31253,
        grid_voltage_l2 = 31255,
        grid_voltage_l3 = 31257,
        power_grid_feeding_l1 = 31259,
        power_grid_feeding_l2 = 31261,
        power_grid_feeding_l3 = 31263,
        power_grid_drawn_l1 = 31265,
        power_grid_drawn_l2 = 31267,
        power_grid_drawn_l3 = 31269,
    };
    static const uint8_t unit_id = 3;

    void poll(core::situation& sit) override
    {
        if (auto reg = m_conn.read_holding_registers(unit_id, grid_voltage_l1, 18))
        {
            for (size_t i = 0; i < sit.grid.size(); i++)
            {
                sit.grid[i].voltage = reg->get<uint32_t>(grid_voltage_l1 + i * 2) / 100.0;
                sit.grid[i].current = ( 1.0 * reg->get<uint32_t>(power_grid_drawn_l1 + i * 2)
                                      - 1.0 * reg->get<uint32_t>(power_grid_feeding_l1 + i * 2)
                                      ) / sit.grid[i].voltage;
            }
        }
        if (auto reg = m_conn.read_holding_registers(unit_id, battery_state_of_charge, 2))
        {
            sit.battery_state = reg->get<uint32_t>(battery_state_of_charge) / 100.0;
        }
        if (auto reg = m_conn.read_holding_registers(unit_id, inverter_power, 2))
        {
            sit.inverter_output = 1.0 * reg->get<uint32_t>(inverter_power);
        }
        if (auto reg = m_conn.read_holding_registers(unit_id, battery_charge, 4))
        {
            sit.battery_output = 0.0 + reg->get<uint32_t>(battery_discharge) - reg->get<uint32_t>(battery_charge);
        }
    }

    struct ip_parser {
        boost::asio::ip::address operator()(std::string_view text) { return boost::asio::ip::make_address(text); }
    };
    config::param<boost::asio::ip::address, ip_parser> m_ip = {"sma.ip", boost::asio::ip::address{}};
    config::param<uint16_t> m_port{"sma.port", 502};
    modbus::connection m_conn;
} impl;

} // anonymous namespace
