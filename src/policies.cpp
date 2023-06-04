#include "core.h"
#include "settings.h"
#include "logf.h"

namespace
{

struct red_impl : core::policy
{
    red_impl() : core::policy("red") {}

    virtual std::string_view icon() const { return "wi-lightning"; }
    virtual std::string_view label() const { return "Volle kracht"; }
    virtual std::string_view description() const {
        static std::string desc = str(boost::format(
            "<p>Laad de wagen zo snel mogelijk op. Het laadvermogen wordt enkel begrensd door "
            "de maximum stroom die van het net kan gehaald worden (%s A per fase), om te vermijden dat de "
            "hoofdautomaat uitvalt.</p>"
            ) % settings::html(m_max_current));
        return desc;
    }

    core::budget apply(const core::situation& sit)
    {
        auto maxphase = std::max_element(sit.grid.begin(), sit.grid.end(),
                [](const auto& l, const auto& r) { return l.current < r.current; });
        return {m_max_current.get() - maxphase->current};
    }

    settings::param<double> m_max_current{"max_current", 0.0};
} red;

struct orange_impl : core::policy
{
    orange_impl() : core::policy("orange") {}

    virtual std::string_view icon() const { return "wi-cloudy"; }
    virtual std::string_view label() const { return "Beperkte capaciteit"; }
    virtual std::string_view description() const {
        static std::string desc = str(boost::format(
            "<p>Er is niet genoeg zon om het volledige huishouden op zonne-energie te laten draaien. "
            "De thuisbatterij geraakt zelfs niet volledig opgeladen. "
            "Wacht daarom ook niet tot er een overschot aan zonne-energie is alvorens de wagen "
            "te beginnen opladen.</p>"
            "<p>Start meteen met laden, ook als er geen zon is, maar begrens "
            "het totale verbruik van het laadpunt plus het huishouden wel tot %s W. "
            "Hierdoor worden hoge kwartierwaarden die "
            "het capaciteitstarief de hoogte zouden doen injagen vermeden.</p>"
            ) % settings::html(m_max_capacity));
        return desc;
    }

    core::budget apply(const core::situation& sit)
    {
        auto budget_power = m_max_capacity.get() - sit.grid_output();
        auto budget_current = budget_power / sit.grid_voltage() / sit.grid.size();

        // in case of very unbalanced load, the red policy might set a stronger constraint.
        // follow the red policy in that case - otherwise we're risking a power failure.
        auto budget_current_red = red.apply(sit).current;

        return core::budget{std::min(budget_current, budget_current_red)};
    }

    settings::param<double> m_max_capacity{"max_capacity", 8000.0};
} orange;

settings::param<double> battery_max_power("battery_max_power", 5000.0);
settings::param<double> inverter_max_power("inverter_max_power", 8000.0);
settings::param<double> real_feeding_threshold = {"real_feeding_threshold", 1000.0};

struct yellow_impl : core::policy
{
    yellow_impl() : core::policy("yellow") {}

    virtual std::string_view icon() const { return "wi-day-cloudy"; }
    virtual std::string_view label() const { return "Maximaliseer eigen verbruik"; }
    virtual std::string_view description() const {
        static std::string desc = str(boost::format(
            "<p>Er is meer dan genoeg zon om het huishouden op zonne-energie te laten draaien, "
            "maar niet genoeg om ook de batterij van de wagen volledig op te laden. "
            "Probeer daarom vooral te vermijden dat er energie geïnjecteerd wordt in het net, "
            "terwijl we die zelf ook goed kunnen gebruiken.</p>"
            "<p>Concreet: alle opgewekte zonne-energie staat ter beschikking van huishouden en laadpunt. "
            "Ook als het niveau van de thuisbatterij meer dan %s %% is, "
            "mag deze al beginnen leveren.</p>"
            "<p>Door het niveau van de thuisbatterij laag te houden, maximaliseren we het eigen "
            "verbruik: als de wagen stopt met laden omdat hij ontkoppeld wordt of omdat het mimimum "
            "vermogen om te beginnen laden niet gehaald wordt, dan kan de opgewekte energie "
            "nog gebruikt worden om de thuisbatterij op te laden.</li>"
            "</ul>") % settings::html(m_battery_threshold));
        return desc;
    }

    settings::param<double> m_battery_threshold = {"yellow.battery_threshold", 10};

    core::budget apply(const core::situation& sit)
    {
        // primary budget = what we're feeding into the grid
        auto budget_power = -sit.grid_output();

        if (sit.battery_state > m_battery_threshold.get() / 100.0)
        {
            // battery level above threshold -> allow maximum discharging
            auto extra_discharging_budget = inverter_max_power.get() - sit.inverter_output;
            auto extra_discharging_capacity = battery_max_power.get() - sit.battery_output;
            budget_power += std::min(extra_discharging_budget, extra_discharging_capacity);
        }
        else if (sit.battery_output < 0.0)
        {
            // battery low but charging -> energy that goes into battery is available for charge point.
            budget_power -= sit.battery_output;
        }

        return core::budget{budget_power / sit.grid_voltage() / sit.grid.size()};
    }
} yellow;

struct green_impl : core::policy
{
    green_impl() : core::policy("green") {}

    virtual std::string_view icon() const { return "wi-day-sunny"; }
    virtual std::string_view label() const { return "Minimaliseer afname"; }
    virtual std::string_view description() const {
        static std::string desc = str(boost::format(
            "<p>Er is meer dan genoeg zon om het huishouden en de wagen op zonne-energie te laten draaien. "
            "Probeer daarom vooral te vermijden dat er energie wordt afgenomen van het net.</p>"
            "<p>Enkel de energie die anders in het net geïnjecteerd zou worden staat ter beschikking "
            "voor het laadpunt. Stop onder meer met laden als het niveau van de thuisbatterij onder de "
            "%s %% zakt.</p>") % settings::html(m_battery_threshold));
        return desc;
    }

    settings::param<double> m_battery_threshold = {"green.battery_threshold", 90};

    core::budget apply(const core::situation& sit)
    {
        // primary budget = what we're feeding into the grid
        auto budget_power = -sit.grid_output();

        if (budget_power >= real_feeding_threshold.get() and sit.battery_state > m_battery_threshold.get() / 100.0)
        {
            // if we are steadily feeding into the grid, but not enough to start charging,
            // we might want to start charging car anyway, partly using battery power.
            // this is to avoid that we're feeding into the grid all day long and never start charging
            // just because there is slightly too little solar power.
            auto extra_discharging_budget = inverter_max_power.get() - sit.inverter_output;
            auto extra_discharging_capacity = battery_max_power.get() - sit.battery_output;
            budget_power += std::min(extra_discharging_budget, extra_discharging_capacity);
        }
        else if (sit.battery_state < m_battery_threshold.get() / 100.0)
        {
            // battery level below threshold -> stop discharging and start charging at maximum power.
            budget_power -= battery_max_power.get() + sit.battery_output;
        }

        return core::budget{budget_power / sit.grid_voltage() / sit.grid.size()};
    }
} green;

}
