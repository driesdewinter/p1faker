#ifndef SETTINGS_H_
#define SETTINGS_H_

#include "config.h"
#include "mutex_protected.h"

#include <optional>
#include <string_view>
#include <nlohmann/json.hpp>

namespace settings
{

class param_base
{
public:
    virtual void setjson(const nlohmann::json&) = 0;
    virtual void getjson(nlohmann::json&) = 0;

protected:
    param_base(std::string_view name) : m_name(name) {}
    param_base(const param_base&) = delete;
    param_base& operator=(const param_base&) = delete;
    virtual ~param_base();

    void load();

    const std::string m_name;
};

template<typename T, typename Parser = config::parser<T>>
class param : param_base
{
public:
    param(std::string_view name, const T& hard_default)
    : param_base(name)
    {
        *m_value.lock() = config::param<T, Parser>{name, hard_default}.get();
        load();
    }

    std::string_view name() const { return m_name; }

    T get() const { return *m_value.lock(); }
    operator T() const { return get(); }

protected:
    void setjson(const nlohmann::json& j) override { *m_value.lock() = j.get<T>(); }
    void getjson(nlohmann::json& j) override { j = *m_value.lock(); }
private:
    mutex_protected<T> m_value;
};

// to be used in html to include editable text to display and manipulate that want to include an input field for managing a setting
std::string html(const param<double>& param);

void apply(const nlohmann::json& j);

} // namespace settings

#endif /* SETTINGS_H_ */
