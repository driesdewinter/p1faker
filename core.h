#ifndef CORE_H_
#define CORE_H_

#include <array>
#include <span>
#include <string>
#include <vector>

namespace core
{

namespace phase
{ enum type {
    l1,
    l2,
    l3,
    count
};}

struct situation
{
    double house_battery = 1.0; // 0.0 .. 1.0
    double car_battery = 0.0; // 0.0 .. 1.0
    struct ac_type
    {
        double voltage() const { return m_voltage; }
        double current() const { return m_current; }
        ac_type& voltage(double value) { m_voltage = value; return *this; }
        ac_type& current(double value) { m_current = value; return *this; }
        double power() const { return voltage() * current(); } // ignoring cos(phi)...
    private:
        double m_voltage = 230.0;
        double m_current = 0.0;
    };
    std::array<ac_type, phase::count> ac;
};

struct budget
{
    double current = 0.0;
};

struct producer
{
    std::string_view name() const { return m_name; }

    virtual void poll(situation&) = 0;

protected:
    producer(std::string_view name);
    virtual ~producer();
private:
    const std::string m_name;
};

struct policy
{
    static std::vector<std::string> list();
    static void activate(std::string_view name);

    std::string_view name() const { return m_name; }

    virtual budget apply(const situation&) = 0;

protected:
    policy(std::string_view name);
    virtual ~policy();
private:
    const std::string m_name;
};

struct consumer
{
    std::string_view name() const { return m_name; }

    virtual void handle(const budget&, const situation&) = 0;

protected:
    consumer(std::string_view name);
    virtual ~consumer();
private:
    const std::string m_name;
};

} // namespace core

#endif /* CORE_H_ */
