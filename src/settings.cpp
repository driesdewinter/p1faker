#include "settings.h"

#include "logf.h"
#include "mutex_protected.h"
#include "www.h"

#include <fstream>

using namespace settings;

namespace
{

struct registry
{
    static auto lock()
    {
        static mutex_protected<registry> instance;
        return instance.lock();
    }
    config::param<std::string> settings_file = {"settings_file", "p1gen-settings.json"};
    std::map<std::string, std::vector<param_base*>> subscribers;
    nlohmann::json all_settings;

    registry()
    {
        std::ifstream fin{settings_file};
        if (not fin.is_open())
        {
            logfinfo("Could not open settings file %s. Assuming factory defaults.", settings_file.get());
            return;
        }
        try
        {
            all_settings = nlohmann::json::parse(fin);
        }
        catch (std::exception& e)
        {
            logferror("Failed to deserialize settings file %s: %s", settings_file.get(), e.what());
        }
    }

    void save()
    {
        std::string tmpfile = settings_file.get() + ".tmp";
        std::ofstream fout{tmpfile};
        if (not fout.is_open())
        {
            logfinfo("Could not open settings file %s. Settings will not be persistent.", settings_file.get());
            return;
        }
        try
        {
            fout << all_settings;
            fout.close();
            rename(tmpfile.c_str(), settings_file.get().c_str());
        }
        catch (std::exception& e)
        {
            logferror("Failed to serialize settings file %s: %s", settings_file.get(), e.what());
            return;
        }
        logfdebug("Written settings to %s", settings_file.get());
    }
};

www::rpc get_rpc = www::rpc::get("settings", [] {
    auto reg = registry::lock();
    return reg->all_settings;
});

www::rpc set_rpc = www::rpc::post<nlohmann::json>("settings", [](const nlohmann::json& settings) {
    if (not settings.is_object())
    {
        logfwarn("POST settings: input argument must be an object");
        return;
    }
    auto reg = registry::lock();
    for (auto& [name, value] : settings.items())
    {
        auto sett_it = reg->all_settings.find(name);
        auto subs_it = reg->subscribers.find(name);
        if (sett_it == reg->all_settings.end() or subs_it == reg->subscribers.end())
        {
            logfwarn("POST settings: setting %s not found.", name);
            continue;
        }
        try
        {
            for (auto& subscriber : subs_it->second)
            {
                subscriber->setjson(value);
            }
            *sett_it = value;
            logfdebug("POST settings: changed %s to %s", name, value.dump());
        }
        catch (std::exception& e)
        {
            logfwarn("POST settings: failed to parse %s as value for settings %s: %s", value.dump(), name, e.what());
            // if the failing setjson()-call was not the first one, we end up with inconsistent state across subscribers,
            // but we assume they all apply the same validation rules.
        }
    }
    reg->save();
});

} // anonymous namespace

void param_base::load()
{
    auto reg = registry::lock();
    auto it = reg->all_settings.find(m_name);
    if (it == reg->all_settings.end())
    {
        try
        {
            getjson(reg->all_settings[m_name]);
        }
        catch (std::exception& e)
        {
            logferror("Failed to collect initial setting %s: %s", m_name, e.what());
        }
    }
    else
    {
        try
        {
            setjson(it.value());
        }
        catch (std::exception& e)
        {
            logferror("Failed to parse settings object %s: %s", m_name, e.what());
        }
    }
    reg->subscribers[m_name].push_back(this);
}

param_base::~param_base()
{
    auto reg = registry::lock();
    auto& subscribers = reg->subscribers[m_name];
    auto it = std::find_if(subscribers.begin(), subscribers.end(), [&] (auto p) { return p == this; });
    if (it != subscribers.end())
        subscribers.erase(it);
}
