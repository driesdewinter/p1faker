#include "core.h"
#include "config.h"
#include "logf.h"
#include "www.h"

#include <fstream>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <string.h>

namespace
{

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
    "1-0:31.7.0(%05.1f*A)\r\n"
    "1-0:51.7.0(%05.1f*A)\r\n"
    "1-0:71.7.0(%05.1f*A)\r\n"
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
    for (int i = 0; i < 8; i++)
    {
      if (crc & 1)
        crc = (crc >> 1) ^ 0xa001;
      else
        crc = (crc >> 1);
    }
  }
  return crc;
}

struct consumer_impl : core::consumer
{
    consumer_impl() : core::consumer("p1out") {}
    ~consumer_impl() { reset(); }

    void reset()
    {
        if (m_fd != STDOUT_FILENO)
        {
            logfdebug("Close p1 output");
            close(m_fd);
        }
        m_fd = STDOUT_FILENO;
    }

    void reconnect()
    {
        reset();

        m_fd = m_filename.get().empty() ? STDOUT_FILENO : open(m_filename.get().c_str(), O_RDWR | O_SYNC | O_EXCL);
        if (m_fd < 0)
        {
            if (m_connect_errno != errno)
                logferror("Could not open %s: %s. Using stdout instead.\n", m_filename, strerror(errno));
            m_connect_errno = errno;
            m_fd = STDOUT_FILENO;
            return;
        }
        logfinfo("Connected p1 output to %s", m_filename.get().empty() ? "stdout" : m_filename.get().c_str());
        m_connect_errno = 0;
        if (m_istty) [&]
        {
            struct termios tty;
            if (tcgetattr(m_fd, &tty) != 0)
            {
                logferror("tcgetattr(%s) failed: %s", m_filename, strerror(errno));
                return; // early return from if (istty) { ... }
            }
            tty.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS | ICANON);
            tty.c_cflag |= CS8;
            cfsetispeed(&tty, B115200);
            cfsetospeed(&tty, B115200);
            if (tcsetattr(m_fd, TCSANOW, &tty) != 0)
            {
                logferror("tcsetattr(%s) failed: %s", m_filename, strerror(errno));
            }
        }();
    }

    void handle(const core::budget& budget, const core::situation&) override
    {
        if (m_connect_errno or m_write_errno) reconnect();

        double fakecur = std::max(0.0, m_max_current - budget.current);
        auto payload = str(boost::format(p1template) % fakecur % fakecur % fakecur);
        auto fullmsg = str(boost::format("%s%04X\r\n") % payload % crc16(payload));
        ssize_t n = write(m_fd, fullmsg.c_str(), fullmsg.size());
        int err = n == ssize_t(fullmsg.size()) ? 0
                : n < 0 ? errno
                : EMSGSIZE; // don't support partial writes, so treat it as an error (Message too long).
        if (err != m_write_errno)
        {
            if (err) logferror("Writing %u bytes to p1 output failed: %s", fullmsg.size(), strerror(errno));
            else logfinfo("Successfully written %u bytes to p1 output", fullmsg.size());
            m_write_errno = err;
        }
    }

    config::param<double> m_max_current{"max_current", 0.0};
    config::param<std::string> m_filename{"p1out.file", ""};
    config::param<bool> m_istty{"p1out.istty", false};

    int m_fd = STDOUT_FILENO;
    int m_connect_errno = EBADFD;
    int m_write_errno = 0;
    std::string m_p1template;
} impl;

www::rpc p1status = www::rpc::get("p1status", [] {
    static config::param<std::string> path{"p1status.path", "/sys/class/gpio/gpio2/value"};
    std::ifstream fin{path.get()};
    if (not fin.is_open())
    {
        logfdebug("Could not open config file %s", path);
        return false;
    }
    bool result;
    fin >> result;
    result = !result; // inverted due to opto coupler
    return result;
});

} // anonymous namespace
