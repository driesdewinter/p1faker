#include <boost/format.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <ranges>
#include <string_view>

#include <termios.h>
#include <unistd.h>

using namespace std::literals::chrono_literals;

static const char* p1template = "/XMX5XMXCQA0000020863\r\n"
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
    "1-0:31.7.0(%02.1f*A)\r\n"
    "1-0:51.7.0(%02.1f*A)\r\n"
    "1-0:71.7.0(%02.1f*A)\r\n"
    "1-0:21.7.0(00.000*kW)\r\n"
    "1-0:41.7.0(00.000*kW)\r\n"
    "1-0:61.7.0(00.000*kW)\r\n"
    "1-0:22.7.0(00.000*kW)\r\n"
    "1-0:42.7.0(00.000*kW)\r\n"
    "1-0:62.7.0(00.000*kW)\r\n"
    "!";

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
  double current_offset = std::strtod(argv[1], nullptr);
  auto t = std::chrono::system_clock::now();
  boost::asio::ip::tcp::endpoint sma_addr{boost::asio::ip::address::from_string("192.168.0.216"), 502};
  boost::asio::io_service io_service;
  boost::asio::ip::tcp::socket sma_sock(io_service, sma_addr.protocol());
  sma_sock.connect(sma_addr);

  static constexpr std::size_t nreg = 9;

  union
  {
      uint8_t raw[0];
      struct
      {
          uint16_t transaction_id = htons(1);
          uint16_t protocol_id = htons(0);
          uint16_t length = htons(6);
          uint8_t unit_id = 3;
          uint8_t function_code = 3;
          uint16_t reference_number = htons(31253);
          uint16_t word_count = htons(nreg*2);
      };
  } modbus_request;

  union
  {
      uint8_t raw[1500];
      struct
      {
          uint16_t transaction_id;
          uint16_t protocol_id;
          uint16_t length;
          uint8_t unit_id;
          uint8_t function_code;
          uint8_t byte_count;
          uint8_t payload[0];
      };
  } modbus_response;


  int fd = open("/dev/ttyS0", O_RDWR | O_SYNC | O_EXCL);
  if (fd < 0)
  {
      std::cerr << "Could not open \"/dev/ttyS0\": " << strerror(errno) << ". Using stdout instead.\n";
      fd = STDOUT_FILENO;
  }
  else
  {
      // Create new termios struct, we call it 'tty' for convention
      // No need for "= {0}" at the end as we'll immediately write the existing
      // config to this struct
      struct termios tty;

      // Read in existing settings, and handle any error
      // NOTE: This is important! POSIX states that the struct passed to tcsetattr()
      // must have been initialized with a call to tcgetattr() overwise behaviour
      // is undefined
      if (tcgetattr(fd, &tty) != 0)
      {
          std::cerr << "tcgetattr() failed: " << strerror(errno) << "\n";
          return -1;
      }
      tty.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS | ICANON);
      tty.c_cflag |= CS8;
      cfsetispeed(&tty, B115200);
      cfsetospeed(&tty, B115200);
      if (tcsetattr(fd, TCSANOW, &tty) != 0)
      {
          std::cerr << "tcsetattr() failed: " << strerror(errno) << "\n";
          return -1;
      }
  }

  auto reg = [&](std::size_t i)
  {
      return modbus_response.payload[i*4+2]<<8 | modbus_response.payload[i*4+3];
  };

  while (true)
  {
      sma_sock.send(boost::asio::buffer(&modbus_request.raw[0], sizeof(modbus_request)));
      size_t len = sma_sock.read_some(boost::asio::buffer(&modbus_response.raw[0], sizeof(modbus_response)));
      if (&modbus_response.raw[0] + len < &modbus_response.payload[0])
      {
          std::cerr << "Received modbus payload too short: " << len << "\n";
      }
      else if (ntohs(modbus_response.byte_count) < nreg * 4)
      {
          std::cerr << "Received modbus byte_count too short: " << modbus_response.byte_count << "\n";
      }
      else if (ntohs(modbus_response.length) < nreg * 4 + 3)
      {
          std::cerr << "Received modbus length too short: " << modbus_response.length << "\n";
      }
      std::array<double, 3> current;
      for (size_t i = 0; i < 3; i++) current[i] = std::max(0.0, current_offset
              + 100.0 * (reg(i + 6) - reg(i + 3)) / reg(i));
      auto payload = str(boost::format(p1template) % current[0] % current[1] % current[2]);
      auto fullmsg = str(boost::format("%s%04X\r\n") % payload % crc16(payload));
      if (write(fd, fullmsg.c_str(), fullmsg.size()) != fullmsg.size())
      {
          std::cerr << "write() failed: " << strerror(errno) << "\n";
          return -1;
      }
      std::cout << boost::format("L1=%02.1fA L2=%02.1fA L3=%02.1fA\n")
              % current[0] % current[1] % current[2];
      std::this_thread::sleep_until(t = std::max(t + 1s, std::chrono::system_clock::now()));
  }
  return 0;
}
