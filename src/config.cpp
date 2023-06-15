#include "config.h"
#include "mutex_protected.h"
#include "logf.h"

#include <boost/algorithm/string/trim.hpp>

#include <map>
#include <vector>
#include <fstream>
#include <optional>

#include <signal.h>

using namespace config;

namespace {

struct sigsuppress_type {
    sigsuppress_type() {
        sigaddset(&sigset, SIGTERM);
        sigaddset(&sigset, SIGINT);
        sigprocmask(SIG_BLOCK, &sigset, nullptr);
    }
    sigset_t sigset = {};
} sigsuppress;

struct param_desc {
    std::optional<std::string> value;
    std::vector<param_base*> subscribers;
};

static int lock_recurse = 0;
#define rlogferror(fmt...) if (lock_recurse <= 1) __logf(logseverity::error, fmt)
#define rlogfdebug(fmt...) if (lock_recurse <= 1) __logf(logseverity::debug, fmt)

struct registry {
    std::map<std::string, param_desc> m_params;

    void param_parse(const std::string& name, param_base* p, std::string_view value) {
        try {
            auto result = p->parse(value);
            rlogfdebug("Set config param %s to %s", name, result);
        } catch (std::exception& e) {
            rlogferror("Failed to set config param %s: %s (parsed from %s)", name, e.what(), value);
        }
    }
};

using protected_registry_type = mutex_protected<registry, std::recursive_mutex>;
using lock_type_base = mutex_protected<registry, std::recursive_mutex>::locked_access<registry>;
struct lock_type : lock_type_base {
    lock_type(protected_registry_type& reg) : lock_type_base(reg.lock()) { lock_recurse++; }
    ~lock_type() { lock_recurse--; }
};

static auto get_registry() {
    static mutex_protected<registry, std::recursive_mutex> instance;
    return lock_type{instance};
}

struct config_file {
    config_file(const char* path) {
        std::ifstream fin{std::string{path}};
        if (not fin.is_open()) {
            rlogfdebug("Could not open config file %s", path);
            return;
        }
        rlogfdebug("Processing %s", path);
        while (fin.good() and not fin.eof()) {
            std::string line;
            std::getline(fin, line);
            for (char delimiter : {'\r', '\n', '#'}) {
                auto pos = line.find(delimiter);
                if (pos != std::string::npos) line.resize(pos);
            }
            auto pos = line.find('=');
            if (pos == std::string::npos)
                continue;
            std::string name = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            boost::trim(name);
            boost::trim(value);
            set_param(name, value);
        }
    }
};
config_file etc_config{"/etc/p1faker.conf"};
config_file cwd_config{"p1faker.conf"};

} // anonymous namespace

void config::set_param(std::string_view _name, std::string_view value)
{
    auto reg = get_registry();
    auto name = std::string{_name};
    param_desc& desc = reg->m_params[name];
    desc.value = std::string{value};
    for (auto* p : desc.subscribers)
        reg->param_parse(name, p, *desc.value);
}

void param_base::init()
{
    auto reg = get_registry();
    param_desc& desc = reg->m_params[m_name];
    desc.subscribers.push_back(this);
    char* env = getenv(m_name.c_str());
    if (env)
        reg->param_parse(m_name, this, env);
    if (desc.value)
        reg->param_parse(m_name, this, *desc.value);
}

param_base::~param_base()
{
    auto reg = get_registry();
    param_desc& desc = reg->m_params[m_name];
    auto it = std::find_if(desc.subscribers.begin(), desc.subscribers.end(), [&] (auto p) { return p == this; });
    if (it != desc.subscribers.end())
        desc.subscribers.erase(it);
}
