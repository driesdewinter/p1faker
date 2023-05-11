#include <iostream>
#include <chrono>
#include <thread>
#include <boost/format.hpp>
#include <ranges>

using namespace std::literals::chrono_literals;

const char* p1template = "/XMX5XMXCQA0000020863\r\n"
    "\r\n"
    "1-3:0.2.8(40)\r\n"
    "0-0:1.0.0(000101010000W)\r\n"
    "0-0:96.1.1(4530303030303030303030303030303030)\r\n"
    "1-0:1.8.1(000000.000*kWh)\r\n"
    "1-0:2.8.1(000000.000*kWh)\r\n"
    "1-0:1.8.2(000000.000*kWh)\r\n"
    "1-0:2.8.2(000000.000*kWh)\r\n"
    "0-0:96.14.0(0001)\r\n"
    "1-0:1.7.0(00.000*kW)\r\n"
    "1-0:2.7.0(00.000*kW)\r\n"
    "0-0:17.0.0(000.0*kW)\r\n"
    "0-0:96.3.10(1)\r\n"
    "0-0:96.7.21(00000)\r\n"
    "0-0:96.7.9(00000)\r\n"
    "1-0:99.97.0(0)(0-0:96.7.19)\r\n"
    "1-0:32.32.0(00000)\r\n"
    "1-0:52.32.0(00000)\r\n"
    "1-0:72.32.0(00000)\r\n"
    "1-0:32.36.0(00000)\r\n"
    "1-0:52.36.0(00000)\r\n"
    "1-0:72.36.0(00000)\r\n"
    "0-0:96.13.1(XMX_P1CS_V06)\r\n"
    "0-0:96.13.0()\r\n"
    "1-0:31.7.0({}*A)\r\n"
    "1-0:51.7.0({}*A)\r\n"
    "1-0:71.7.0({}*A)\r\n"
    "1-0:21.7.0(00.000*kW)\r\n"
    "1-0:41.7.0(00.000*kW)\r\n"
    "1-0:61.7.0(00.000*kW)\r\n"
    "1-0:22.7.0(00.000*kW)\r\n"
    "1-0:42.7.0(00.000*kW)\r\n"
    "1-0:62.7.0(00.000*kW)\r\n";

uint16_t crc16(std::string_view payload)
{
  // Polynomial: x^16 + x^15 + x^2 + 1 (0xa001)
  uint16_t crc = 0;
  for (auto octet : payload)
  {
    crc ^= static_cast<uint8_t>(octet);
    for (auto _ : std::views::iota(0, 8))
    {
      if (crc & 1)
        crc = (crc >> 1) ^ 0xa001;
      else
        crc = (crc >> 1);
    }
  }
  return crc;
}

int main(int argc, const char **argv)
{
  const char* current = argv[1];
  auto t = std::chrono::system_clock::now();
  while (true)
  {
    auto payload = str(boost::format(p1template) % current % current % current);
    std::cout << payload << boost::format("{:04x}\r\n") % crc16(payload);
    std::cout.flush();
    std::this_thread::sleep_until(t += 1s);
  }
  return 0;
}
