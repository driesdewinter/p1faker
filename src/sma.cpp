#include "core.h"
#include "config.h"
#include "logf.h"

#include <boost/asio.hpp>

using namespace std::literals::chrono_literals;

namespace
{

struct modbus_request
{
    uint16_t transaction_id = htons(1);
    uint16_t protocol_id = 0;
    uint16_t length = htons(6);
    uint8_t unit_id = 3;
    uint8_t function_code = 3;
    uint16_t reference_number;
    uint16_t word_count;
};

namespace modbus_exception
{
enum type
{
    illegal_function = 1,
    illegal_data_address = 2,
    illegal_data_value = 3,
    slave_device_failure = 4,
    acknowledge = 5,
    slave_device_busy = 6,
    negative_acknowledge = 7,
    memory_parity_bit_error = 8,
    gateway_path_unavailable = 10,
    gateway_target_device_failed_to_respond = 11,
    invalid_response = 12,
    unexpected_response = 13,
};
std::ostream& operator<<(std::ostream& os, type _v)
{
    unsigned int v = _v;
    static auto labels = {
            "ModbusException[0]",
            "Illegal function",
            "Illegal data address",
            "Illegal data value",
            "Slave device failure",
            "Acknowledge",
            "Slave device busy",
            "Negative acknowledge",
            "Memory parity bit error",
            "ModbusException[9]",
            "Gateway path unavailable",
            "Gateway target device failed to respond",
            "Invalid response",
            "Unexpected response"
    };
    if (v < unsigned(labels.end() - labels.begin()))
        return os << *(labels.begin() + v);
    return os << "ModbusException[" << v << "]";
}

struct : boost::system::error_category
{
    const char* name() const noexcept override { return "modbus"; }
    std::string message( int ev ) const override { return (std::ostringstream{} << static_cast<modbus_exception::type>(ev)).str(); }
} error_category;

}
auto err(modbus_exception::type v) { return boost::system::error_code{v, modbus_exception::error_category}; }
auto exc(modbus_exception::type v) { return boost::system::system_error{err(v)}; }

struct modbus_response
{
    uint16_t transaction_id;
    uint16_t protocol_id;
    uint16_t length;
    uint8_t unit_id;
    uint8_t function_code:7;
    uint8_t status:1;
    union {
        struct
        {
            uint8_t byte_count;
            uint8_t payload[0];
        };
        uint8_t exception;
    };

    void validate(std::size_t received_bytes, const modbus_request& request) const
    {
        if (received_bytes < offsetof(modbus_response, payload)) throw exc(modbus_exception::invalid_response);
        if (transaction_id != request.transaction_id) throw exc(modbus_exception::unexpected_response);
        if (protocol_id != 0) throw exc(modbus_exception::invalid_response);
        if (status) throw exc(modbus_exception::type(exception));
        if (unit_id != request.unit_id) throw exc(modbus_exception::unexpected_response);
        if (function_code != request.function_code) throw exc(modbus_exception::unexpected_response);
        if (byte_count != ntohs(request.word_count) * 2) throw exc(modbus_exception::unexpected_response);
        if (ntohs(length) != ntohs(request.word_count) * 2 + 3) throw exc(modbus_exception::unexpected_response);
        //if (received_bytes != sizeof(*this) + byte_count) throw exc(modbus_exception::unexpected_response);
    }

    uint32_t get(uint16_t offset) const
    {
        std::size_t byte_offset = offset * 2;
        if (byte_offset + 4 > byte_count) return 0;
        const uint8_t* b = &payload[byte_offset];
        return b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
    }

};

struct producer_impl : core::producer
{
    producer_impl() : core::producer("sma") {}

    static boost::system::error_code syserr(int err) { return {err, boost::system::system_category()}; }
    static boost::system::system_error sysexc(int err) { return boost::system::system_error{syserr(err)}; }

    void reconnect()
    {
        try
        {
            if (m_ip->is_unspecified()) throw sysexc(EFAULT);
            boost::asio::ip::tcp::endpoint addr{m_ip, m_port};
            m_sock = boost::asio::ip::tcp::socket{m_io_service, addr.protocol()};
            m_sock.connect(addr);
            if (m_connect_error.failed()) logfinfo("Successfully connected to %s:%s", m_ip, m_port);
            m_connect_error = {};
        }
        catch (boost::system::system_error& e)
        {
            if (m_connect_error != e.code())
            {
                logferror("Failed to connect to SMA inverter at %s:%d : %s", m_ip, m_port, e.code().message());
                m_connect_error = e.code();
            }
        }
    }

    void poll(core::situation& sit) override
    {
        if (m_connect_error.failed() or m_request_error.failed()) reconnect(); // currently in error state -> try to reconnect.
        if (m_connect_error.failed()) return; // not (re-)connected -> give up.

        try
        {
            // 30775 = total power produced by inverter
            // 31393 = battery charge
            // 31395 = discharge
            // 30845 = soc(%)

            modbus_request request;
            request.reference_number = htons(31253);
            request.word_count = htons(18);
            auto t0 = std::chrono::system_clock::now();
            std::size_t written = m_sock.write_some(boost::asio::buffer(reinterpret_cast<const uint8_t*>(&request), sizeof(request)));
            if (written != sizeof(request)) throw sysexc(EMSGSIZE);
            auto t1 = std::chrono::system_clock::now();
            std::array<uint8_t, 1500> raw_response;
            std::size_t received = m_sock.read_some(boost::asio::buffer(raw_response, raw_response.size()));
            auto t2 = std::chrono::system_clock::now();
            if (t2 - t0 > 500ms)
            {
                logfwarn("sma: read took %s, write took %s", duration_cast<std::chrono::milliseconds>(t2 - t1), duration_cast<std::chrono::milliseconds>(t1 - t0));
            }
            const modbus_response& response = *reinterpret_cast<const modbus_response*>(raw_response.begin());
            response.validate(received, request);

            for (size_t i = 0; i < 3; i++)
            {
                sit.ac[i].voltage = response.get(i * 2) / 100.0;
                sit.ac[i].current = (double(response.get(i * 2 + 12)) - double(response.get(i * 2 + 6))) * 100.0 / response.get(i * 2);
            }

            if (m_request_error.failed())
            {
                logfdebug("Reading modbus registers from %s:%s succeeded.", m_ip, m_port);
                m_request_error = {};
            }
        }
        catch (boost::system::system_error& e)
        {
             if (m_request_error != e.code())
             {
                 logferror("Reading modbus registers from %s:%s failed: %s", m_ip, m_port, e.code().message());
                 m_request_error = e.code();
             }
        }
    }

    struct ip_parser {
        boost::asio::ip::address operator()(std::string_view text) { return boost::asio::ip::make_address(text); }
    };

    boost::asio::io_service m_io_service;
    config::param<boost::asio::ip::address, ip_parser> m_ip = {"sma.ip", boost::asio::ip::address{}};
    config::param<uint16_t> m_port{"sma.port", 502};
    boost::asio::ip::tcp::socket m_sock{m_io_service};
    boost::system::error_code m_connect_error = syserr(EBADF);
    boost::system::error_code m_request_error;
} impl;

} // anonymous namespace
