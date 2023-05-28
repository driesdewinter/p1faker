#ifndef CONFIG_H_
#define CONFIG_H_

#include <string_view>
#include <sstream>

namespace config
{

void set_param(std::string_view name, std::string_view value);

class param_base
{
public:
    virtual std::string parse(std::string_view text) = 0;
protected:
    param_base(std::string_view name);
    param_base(const param_base&) = delete;
    param_base& operator=(const param_base&) = delete;
    void init();
    virtual ~param_base();
private:
    const std::string m_name;
};

template<typename T, typename = void>
struct parser
{
    T operator()(std::string_view text) { T v; std::istringstream{std::string{text}} >> v; return v; }
};

template<typename T>
struct parser<T, std::enable_if<std::is_constructible_v<T, std::string_view>>>
{
    T operator()(std::string_view text) { return T{text}; }
};

template<typename T, typename Parser = parser<T>>
class param : param_base, Parser
{
public:
    param(std::string_view name, T default_value) : param_base(name), m_value(default_value) { init(); }

    operator const T&() const { return m_value; }
    const T& operator*() const { return m_value; }
    const T* operator->() const { return &m_value; }
    const T& get() const { return m_value; }

protected:
    std::string parse(std::string_view text) override
    {
        m_value = Parser::operator()(text);
        return (std::ostringstream{} << m_value).str();
    }
private:
    T m_value;
};
template<typename T, typename Parser>
static inline std::ostream& operator<<(std::ostream& os, const param<T, Parser>& p) { return os << p.get(); }

} // namespace config

#endif /* CONFIG_H_ */
