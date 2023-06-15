#include "logf.h"
#include "config.h"

#include <cstdlib>
#include <chrono>
#include <time.h>
#include <syslog.h>

using namespace logdetail;
using namespace std::literals::chrono_literals;

static auto severity_labels = {"panic", "error", "warn", "info", "debug", "extra"};

std::ostream& logseverity::operator<<(std::ostream& os, logseverity::type v) {
    return os << *(severity_labels.begin() + v);
}
std::istream& logseverity::operator>>(std::istream& is, logseverity::type& v) {
    std::string label;
    is >> label;
    auto it = std::find(severity_labels.begin(), severity_labels.end(), label);
    if (it == severity_labels.end())
        throw std::logic_error{"invalid severity"};
    v = static_cast<logseverity::type>(it - severity_labels.begin());
    return is;
}

msgdef::msgdef(logseverity::type _sev, const char* _file, int _line, const char* _fmt)
: sev(_sev), file(_file), line(_line), fmt(_fmt) {}

bool msgdef::shouldlog() const {
    static config::param<logseverity::type> verbosity{"verbosity", logseverity::info};
    return sev <= verbosity.get();
}

void msgdef::dolog(std::string msg) {
    static config::param<bool> use_syslog{"use_syslog", false};
    //static bool use_syslog = false;
    if (use_syslog) {
        static struct syslog_init_type {
            syslog_init_type() { openlog("p1faker", 0, 0); }
        } syslog_init;

        static auto levels = {LOG_ALERT, LOG_ERR, LOG_WARNING, LOG_INFO, LOG_DEBUG, LOG_DEBUG};
        syslog(*(levels.begin() + sev), "[%s] [%s:%d] %s",
                *(severity_labels.begin() + sev), file, line, msg.c_str());
    } else {
        struct tm tm;
        auto tp = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(tp);
        localtime_r(&tt, &tm);
        std::cerr << boost::format("%04d-%02d-%02d %02d:%02d:%02d.%03d %s [%s] [%s:%d] %s\n")
                % (tm.tm_year + 1900) % (tm.tm_mon + 1) % tm.tm_mday
                % tm.tm_hour % tm.tm_min % tm.tm_sec % (tp.time_since_epoch() % 1s / 1ms) % tm.tm_zone
                % sev % file % line % msg;
    }
    if (sev == logseverity::panic)
        std::abort();
}
