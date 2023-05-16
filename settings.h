#ifndef SETTINGS_H_
#define SETTINGS_H_

#include <optional>
#include <string_view>
#include <nlohmann/json.hpp>

namespace settings
{

class object_base
{
protected:
    object_base(std::string_view name) : m_name(name) {}
    virtual ~object_base() {}

    void load();
    virtual void setjson(const nlohmann::json&) = 0;
    void save();
    virtual void getjson(nlohmann::json&) = 0;
private:
    std::string m_name;
};

template<typename T>
class object : object_base
{
public:
    template<typename... Args>
    object(std::string_view name, Args&&... args)
    : object_base(name), m_value(std::forward<Args>(args)...) { load(); }

    operator const T&() const { return m_value; }
    const T& operator*() const { return m_value; }
    const T* operator->() const { return &m_value; }
    const T& get() const { return m_value; }

    struct editor
    {
        T* operator->() { return &m_outer.m_value; }
        T& get() { return m_outer.m_value; }
        editor& operator=(const T& value) { m_outer.m_value = value; return *this; }
        editor& operator=(T&& value) { m_outer.m_value = std::move(value); return *this; }

        editor(const editor& r) : m_outer(r.m_outer) { m_outer.hold(); }
        ~editor() { m_outer.release(); }

    private:
        editor(object& outer) : m_outer(outer) { m_outer.hold(); }
        object& m_outer;
        friend class object;
    };
    editor edit() { return editor{*this}; }

protected:
    void setjson(const nlohmann::json& j) override { m_value = j.get<T>(); }
    void getjson(nlohmann::json& j) override { j = m_value; }
private:
    void hold() { ++m_editor_count; }
    void release() { if (--m_editor_count == 0) save(); }
    T m_value;
    int m_editor_count = 0;
};

} // namespace settings

#endif /* SETTINGS_H_ */
