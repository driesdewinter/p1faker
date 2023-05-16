#ifndef LOGF_H_
#define LOGF_H_

#include <cmath>

#include <iostream>
#include <boost/format.hpp>

namespace logseverity
{
enum type {
    panic,
    error,
    warn,
    info,
    debug,
    extra
};
std::ostream& operator<<(std::ostream& os, type v);
std::istream& operator>>(std::istream& is, type& v);
}

namespace logdetail
{
struct msgdef {
    msgdef(logseverity::type, const char*, int, const char*);

    template<typename... Args>
    void trylog(Args&&... args)
    {
        if (not shouldlog()) return;
        try
        {
            dolog(str((boost::format(fmt) % ... % args)));
        }
        catch (std::exception& e)
        {
            dolog(str(boost::format("formatting '%s' failed: %s") % fmt % e.what()));
        }
    }
private:
    bool shouldlog() const;
    void dolog(std::string msg);
    logseverity::type sev; const char* file; int line; const char* fmt;
};
}

#define __logf(sev, fmt, ...) [&] { static logdetail::msgdef msgdef{sev, __FILE__, __LINE__, fmt}; msgdef.trylog(__VA_ARGS__); }()
#define logferror(fmt...) __logf(logseverity::error, fmt)
#define logfwarn(fmt...)  __logf(logseverity::warn , fmt)
#define logfinfo(fmt...)  __logf(logseverity::info , fmt)
#define logfdebug(fmt...) __logf(logseverity::debug, fmt)
#define logfextra(fmt...) __logf(logseverity::extra, fmt)

#define panic_on(condition, fmt...) [&] { if (condition) __logf(logseverity::panic, fmt); }()

#endif /* LOGF_H_ */
