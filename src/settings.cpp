#include "settings.h"

#include "config.h"
#include "logf.h"
#include "mutex_protected.h"

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
};

} // anonymous namespace

void object_base::load()
{
    auto reg = registry::lock();
    auto it = reg->all_settings.find(m_name);
    if (it == reg->all_settings.end()) return;
    try
    {
        setjson(it.value());
    }
    catch (std::exception& e)
    {
        logferror("Failed to parse settings object %s: %s", m_name, e.what());
    }
}

void object_base::save()
{
    auto reg = registry::lock();
    try
    {
        getjson(reg->all_settings[m_name]);
    }
    catch (std::exception& e)
    {
        logferror("Failed to collect settings object %s: %s", m_name, e.what());
        return;
    }
    std::string tmpfile = reg->settings_file.get() + ".tmp";
    std::ofstream fout{tmpfile};
    if (not fout.is_open())
    {
        logfinfo("Could not open settings file %s. Settings will not be persistent.", reg->settings_file.get());
        return;
    }
    try
    {
        fout << reg->all_settings;
        fout.close();
        rename(tmpfile.c_str(), reg->settings_file.get().c_str());
    }
    catch (std::exception& e)
    {
        logferror("Failed to serialize settings file %s: %s", reg->settings_file.get(), e.what());
        return;
    }
    logfdebug("Written settings to %s", reg->settings_file.get());
}
