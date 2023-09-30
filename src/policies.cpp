#include "core.h"
#include "settings.h"
#include "logf.h"

namespace
{

struct red_impl : core::policy
{
    red_impl() : core::policy{"red"} {}

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

settings::param<double> battery_max_power("battery_max_power", 5000.0);
settings::param<double> battery_min_state("battery_min_state", 10.0);
settings::param<double> inverter_max_power("inverter_max_power", 8000.0);

struct gen_impl : core::policy
{
    gen_impl(std::string_view name, double max_grid_power, double min_solar_power)
    : core::policy{name}
    , m_max_grid_power{str(boost::format("%s.max_grid_power") % name), max_grid_power}
    , m_min_solar_power{str(boost::format("%s.min_solar_power") % name), min_solar_power}
    {}

    core::budget apply(const core::situation& sit)
    {
        auto power_budget = m_max_grid_power.get();

        if (sit.solar_output() >= m_min_solar_power.get()) {
            auto inverter_power_budget = sit.solar_output();
            if (sit.battery_state >= battery_min_state.get() * 0.01)
                inverter_power_budget += battery_max_power.get();
            else if (sit.battery_output > 0.0) // under 5%, battery may keep on giving whatever it is giving, but not more
                inverter_power_budget += sit.battery_output;
            power_budget += std::min(inverter_power_budget, inverter_max_power.get());
        }

        power_budget -= sit.consumption();

        auto current_budget = power_budget / sit.grid_voltage() / sit.grid.size();

        // in case of very unbalanced load, the red policy might set a stronger constraint.
        // follow the red policy in that case - otherwise we're risking a power failure.
        auto current_budget_red = red.apply(sit).current;

        return core::budget{std::min(current_budget, current_budget_red)};
    }

    settings::param<double> m_max_grid_power;
    settings::param<double> m_min_solar_power;
};

struct orange_impl : gen_impl
{
    orange_impl() : gen_impl("orange", 8000.0, 0.0) {}

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
            ) % settings::html(m_max_grid_power));
        return desc;
    }
} orange;


struct yellow_impl : gen_impl
{
    yellow_impl() : gen_impl("yellow", 0.0, 0.0) {}

    virtual std::string_view icon() const { return "wi-day-cloudy"; }
    virtual std::string_view label() const { return "Maximaliseer eigen verbruik"; }
    virtual std::string_view description() const {
        static std::string desc = str(boost::format(
            "<p>Er is meer dan genoeg zon om het huishouden op zonne-energie te laten draaien, "
            "maar niet genoeg om ook de batterij van de wagen volledig op te laden. "
            "Probeer daarom vooral te vermijden dat er energie geïnjecteerd wordt in het net, "
            "terwijl we die zelf ook goed kunnen gebruiken.</p>"
            "<p>Concreet: alle opgewekte zonne-energie staat ter beschikking van huishouden en laadpunt.</p>"));
        return desc;
    }
} yellow;

struct green_impl : gen_impl
{
    green_impl() : gen_impl("green", 0.0, 5000.0) {}

    virtual std::string_view icon() const { return "wi-day-sunny"; }
    virtual std::string_view label() const { return "Minimaliseer afname"; }
    virtual std::string_view description() const {
        static std::string desc = str(boost::format(
            "<p>Er is meer dan genoeg zon om het huishouden en de wagen op zonne-energie te laten draaien. "
            "Probeer dus vooral te vermijden dat er energie wordt afgenomen van het net.</p>"
            "<p>Laad de wagen daarom enkel op als de zonnepanelen minstens %s W opbrengen.</p>"
            "<p>Aangezien de opgewekte zonne-energie typisch niet plotseling van alles naar niets "
            "gaat, maar eerder geleidelijk vermindert, krijgt op die manier de thuisbatterij "
            "nog voldoende kans om op te laden voor de zon helemaal onder gaat.</p>"
            ) % settings::html(m_min_solar_power));
        return desc;
    }
} green;

}
