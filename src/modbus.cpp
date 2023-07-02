#include "modbus.h"
#include "logf.h"
#include "config.h"

#include <sstream>
#include <boost/asio.hpp>

using namespace std::literals::chrono_literals;

namespace modbus {
namespace error {
enum type {
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

std::ostream& operator<<(std::ostream& os, type _v) {
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

boost::system::error_category& category() {
    static struct : boost::system::error_category {
        const char* name() const noexcept override { return "modbus"; }
        std::string message(int ev) const override { return (std::ostringstream{} << static_cast<type>(ev)).str(); }
    } instance;
    return instance;
}

} // namespace error

auto buserr(error::type v) { return boost::system::error_code{v, error::category()}; }
auto busexc(error::type v) { return boost::system::system_error{buserr(v)}; }
auto syserr(int err) { return boost::system::error_code{err, boost::system::system_category()}; }
auto sysexc(int err) { return boost::system::system_error{syserr(err)}; }

struct request {
    uint16_t transaction_id = htons(1);
    uint16_t protocol_id = 0;
    uint16_t length = htons(6);
    uint8_t unit_id = 0;
    uint8_t function_code = 0;
    uint16_t reference_number;
    uint16_t word_count;
};

struct response {
    uint16_t transaction_id;
    uint16_t protocol_id;
    uint16_t length;
    uint8_t unit_id;
    uint8_t function_code:7;
    uint8_t status:1;
    union {
        struct {
            uint8_t byte_count;
            uint8_t payload[0];
        };
        uint8_t exception;
    };

    void validate(std::size_t received_bytes, const request& req) const {
        if (received_bytes < offsetof(response, payload)) throw busexc(error::invalid_response);
        if (transaction_id != req.transaction_id) throw busexc(error::unexpected_response);
        if (protocol_id != 0) throw busexc(error::invalid_response);
        if (status) throw busexc(error::type(exception));
        if (unit_id != req.unit_id) throw busexc(error::unexpected_response);
        if (function_code != req.function_code) throw busexc(error::unexpected_response);
        if (byte_count != ntohs(req.word_count) * 2) throw busexc(error::unexpected_response);
        if (ntohs(length) != ntohs(req.word_count) * 2 + 3) throw busexc(error::unexpected_response);
        if (received_bytes != offsetof(response, payload) + byte_count) throw busexc(error::unexpected_response);
    }
};

config::param<int> tcp_rcv_timeout("modbus.tcp_rcv_timeout", 200);

} // namespace modbus

using namespace modbus;

struct connection::impl {
    std::string m_name;
    std::vector<boost::asio::ip::tcp::endpoint> m_endpoints;
    boost::asio::io_service m_io_service{};
    boost::asio::ip::tcp::socket m_sock{m_io_service};
    boost::system::error_code m_connect_error = syserr(EBADF);
    boost::system::error_code m_request_error{};

    impl(std::string _name)
    : m_name(_name) {}

    void update_endpoint_candidates(const std::vector<boost::asio::ip::tcp::endpoint>& endpoints)
    {
        m_endpoints = endpoints;
        if (m_sock.is_open() and std::find(m_endpoints.begin(), m_endpoints.end(), m_sock.remote_endpoint()) == m_endpoints.end())
        {
            if (not m_connect_error.failed()) {
                logfinfo("Intentionally disconnect from %s at %s:%d because it is no longer a candidate endpoint.",
                        m_name, m_sock.remote_endpoint().address(), m_sock.remote_endpoint().port());
            }
            m_connect_error = syserr(EBADF);
        }
    }

    void reconnect() {
        if (m_endpoints.empty()) {
            if (m_connect_error != syserr(ENOMEDIUM)) {
                logfinfo("Cannot connect %s at because there are no endpoint candidates (yet)", m_name);
                m_connect_error = syserr(ENOMEDIUM);
            }
        } else for (const auto& ep : m_endpoints) {
            try {
                logfdebug("Try to connect to %s at %s:%d", m_name, ep.address(), ep.port());
                m_sock = boost::asio::ip::tcp::socket{m_io_service, ep.protocol()};
                const int timeout = tcp_rcv_timeout;
                ::setsockopt(m_sock.native_handle(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
                m_sock.connect(ep);
                if (m_connect_error.failed())
                    logfinfo("Successfully connected to %s at %s:%s", m_name, ep.address(), ep.port());
                m_connect_error = {};
                break; // only continue the loop in case of connection failure
            } catch (boost::system::system_error& e) {
                if (m_connect_error != e.code()) {
                    logferror("Failed to connect to %s at %s:%d : %s", m_name, ep.address(), ep.port(), e.code().message());
                    m_connect_error = e.code();
                }
            }
        }
    }

    std::optional<register_vector> read_holding_registers(uint8_t unit_id, uint16_t start_address, uint16_t word_count) {
        if (m_connect_error.failed() or m_request_error.failed())
            reconnect(); // currently in error state -> reconnect.
        if (m_connect_error.failed())
            return std::nullopt;

        try {
            request req;
            req.function_code = 3;
            req.reference_number = htons(start_address);
            req.word_count = htons(word_count);
            req.unit_id = unit_id;
            std::size_t written = m_sock.write_some(boost::asio::buffer(reinterpret_cast<const uint8_t*>(&req), sizeof(req)));
            if (written != sizeof(req))
                throw sysexc(EMSGSIZE);
            std::array<uint8_t, 1500> raw_response;
            auto t1 = std::chrono::system_clock::now();
            std::size_t received = m_sock.read_some(boost::asio::buffer(raw_response, raw_response.size()));
            auto t2 = std::chrono::system_clock::now();
            if (t2 - t1 > 1ms * tcp_rcv_timeout)
                logfwarn("Reading modbus registers %s... from %s at %s:%s took long: %s",
                        start_address, m_name, m_sock.remote_endpoint().address(), m_sock.remote_endpoint().port(),
                        duration_cast<std::chrono::milliseconds>(t2 - t1));
            const response& rep = *reinterpret_cast<const response*>(raw_response.begin());
            rep.validate(received, req);
            m_request_error = {};
            return register_vector{start_address, &rep.payload[0], rep.byte_count};
        } catch (boost::system::system_error& e) {
             if (m_request_error != e.code()) {
                 logferror("Reading modbus registers from %s at %s:%s failed: %s",
                         m_name, m_sock.remote_endpoint().address(), m_sock.remote_endpoint().port(),
                         e.code().message());
                 m_request_error = e.code();
             }
             return std::nullopt;
        }

    }

};

connection::connection(std::string name)
: m_impl{std::make_shared<impl>(name)} {}

void connection::update_endpoint_candidates(const std::vector<boost::asio::ip::tcp::endpoint>& endpoints)
{
    m_impl->update_endpoint_candidates(endpoints);
}

std::optional<register_vector> connection::read_holding_registers(uint8_t unit_id, uint16_t start_address, uint16_t word_count) {
    return m_impl->read_holding_registers(unit_id, start_address, word_count);
}

