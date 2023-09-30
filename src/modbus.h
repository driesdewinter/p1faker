#ifndef MODBUS_H_
#define MODBUS_H_

#include <vector>
#include <optional>
#include <memory>
#include <boost/asio/ip/address.hpp>

namespace modbus {

struct endpoint {
    boost::asio::ip::address address;
    uint16_t port;

    auto operator<=>(const endpoint& r) const = default;
};

struct register_vector {
    template<typename T>
    T get(uint16_t address) const {
        T ret = 0;
        for (std::size_t i = 0; i < sizeof(T); i++)
            ret = ret << 8 | m_data[(address - m_start_address) * 2 + i];
        return ret;
    }

    register_vector(uint16_t start_address, const uint8_t* data, std::size_t size)
    : m_start_address(start_address), m_data{data, data + size} {}

private:
    uint16_t m_start_address;
    std::vector<uint8_t> m_data;
};

class connection {
public:
    connection(std::string name);

    void update_endpoint_candidates(const std::vector<endpoint>& endpoints);

    std::optional<register_vector> read_holding_registers(uint8_t unit_id, uint16_t start_address, uint16_t word_count);
private:
    struct impl;
    std::shared_ptr<impl> m_impl;
};

} // namespace modbus

#endif /* MODBUS_H_ */
