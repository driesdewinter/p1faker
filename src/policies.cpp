#include "core.h"
#include "config.h"
#include "logf.h"

namespace
{

struct red_impl : core::policy
{
    red_impl() : core::policy("00red") {}

    virtual std::string_view icon() const { return "wi-lightning"; }
    virtual std::string_view label() const { return "Volle kracht"; }
    virtual std::string_view description() const { return
            "<p>Laad de wagen zo snel mogelijk op. Het laadvermogen wordt enkel begrensd door "
            "de maximum stroom die van het net kan gehaald worden ( <input class=\"number\" "
            "type=\"text\" value=\"25\" onchange=\"console.log('hello')\"></input> A per fase), om te vermijden dat de "
            "hoofdautomaat uitvalt.</p>";
    }

    core::budget apply(const core::situation& sit)
    {
        auto maxac = std::max_element(sit.ac.begin(), sit.ac.end(),
                [](const auto& l, const auto& r) { return l.current < r.current; });
        return {m_max_current.get() - maxac->current};
    }

    config::param<double> m_max_current{"max_current", 0.0};
} red;

struct orange_impl : core::policy
{
    orange_impl() : core::policy("01orange") {}

    virtual std::string_view icon() const { return "wi-cloudy"; }
    virtual std::string_view label() const { return "Beperkte capaciteit"; }
    virtual std::string_view description() const { return
            "<p>Er is niet genoeg zon om het volledige huishouden op zonne-energie te laten draaien. "
            "De thuisbatterij geraakt zelfs niet volledig opgeladen. "
            "Wacht daarom ook niet tot er een overschot aan zonne-energie is alvorens de wagen "
            "te beginnen opladen.</p>"
            "<p>Start meteen met laden, ook als er geen zon is, maar begrens "
            "het totale verbruik van het laadpunt plus het huishouden wel tot <input class=\"number\" "
            "type=\"text\" value=\"8000\"></input> W. Hierdoor worden hoge kwartierwaarden die "
            "het capaciteitstarief de hoogte zouden doen injagen vermeden.</p>";
    }

    core::budget apply(const core::situation& sit)
    {
        auto grid_power = std::accumulate(sit.ac.begin(), sit.ac.end(), 0.0,
                        [](double sum, const auto& ac) { return sum + ac.current * ac.voltage; });
        auto grid_voltage = std::accumulate(sit.ac.begin(), sit.ac.end(), 0.0,
                                [](double sum, const auto& ac) { return sum + ac.voltage; }) / sit.ac.size();
        auto budget_power = m_max_capacity.get() - grid_power;
        auto budget_current = budget_power / grid_voltage / sit.ac.size();

        // in case of very unbalanced load, the red policy might set a stronger constraint.
        // follow the red policy in that case - otherwise we're risking a power failure.
        auto budget_current_red = red.apply(sit).current;

        return core::budget{std::min(budget_current, budget_current_red)};
    }

    config::param<double> m_max_capacity{"max_capacity", 8000.0};
} orange;

struct yellow_impl : core::policy
{
    yellow_impl() : core::policy("02yellow") {}

    virtual std::string_view icon() const { return "wi-day-cloudy"; }
    virtual std::string_view label() const { return "Maximaliseer eigen verbruik"; }
    virtual std::string_view description() const { return
            "<p>Er is meer dan genoeg zon om het huishouden op zonne-energie te laten draaien, "
            "maar niet genoeg om ook de batterij van de wagen volledig op te laden. "
            "Probeer daarom vooral te vermijden dat er energie geïnjecteerd wordt in het net, "
            "terwijl we die zelf ook goed kunnen gebruiken.</p>"
            "<p>Concreet: alle opgewekte zonne-energie staat ter beschikking van huishouden en laadpunt. "
            "Ook als het niveau van de thuisbatterij meer dan <input class=\"number\" type=\"text\" value=\"10\"></input> % is, "
            "mag deze tot <input class=\"number\" type=\"text\" value=\"5000\"></input> W leveren.</p>"
            "<p>Door het niveau van de thuisbatterij laag te houden, maximaliseren we het eigen "
            "verbruik: als de wagen stopt met laden omdat hij ontkoppeld wordt of omdat het mimimum "
            "vermogen om te beginnen laden niet gehaald wordt, dan kan de opgewekte energie "
            "nog gebruikt worden om de thuisbatterij op te laden.</li>"
            "</ul>";
    }

    config::param<double> lift_budget_to = {"alpha.lift_budget_to", 8.0};
    config::param<double> lift_budget_min = {"alpha.lift_budget_min", 4.0};
    config::param<double> tolerance = {"alpha.tolerance", 1.0};

    core::budget apply(const core::situation& sit)
    {
        double avgac = std::accumulate(sit.ac.begin(), sit.ac.end(), 0.0,
                [](double sum, const auto& ac) { return sum + ac.current; }) / sit.ac.size();
        core::budget b;
        if      (avgac < -lift_budget_to.get())  b.current = tolerance.get() - avgac;
        else if (avgac < -lift_budget_min.get()) b.current = tolerance.get() + lift_budget_to.get();
        else                                     b.current = tolerance.get() - avgac;
        return b;
    }
} yellow;

struct green_impl : core::policy
{
    green_impl() : core::policy("03green") {}

    virtual std::string_view icon() const { return "wi-day-sunny"; }
    virtual std::string_view label() const { return "Minimaliseer afname"; }
    virtual std::string_view description() const { return
            "<p>Er is meer dan genoeg zon om het huishouden en de wagen op zonne-energie te laten draaien. "
            "Probeer daarom vooral te vermijden dat er energie wordt afgenomen van het net.</p>"
            "<p>Enkel de energie die anders in het net geïnjecteerd zou worden staat ter beschikking "
            "voor het laadpunt. Stop onder meer met laden als het niveau van de thuisbatterij onder de "
            "<input class=\"number\" type=\"text\" value=\"90\"></input> % zakt.</p>";
    }

    config::param<double> lift_budget_to = {"alpha.lift_budget_to", 8.0};
    config::param<double> lift_budget_min = {"alpha.lift_budget_min", 4.0};
    config::param<double> tolerance = {"alpha.tolerance", 1.0};

    core::budget apply(const core::situation& sit)
    {
        double avgac = std::accumulate(sit.ac.begin(), sit.ac.end(), 0.0,
                [](double sum, const auto& ac) { return sum + ac.current; }) / sit.ac.size();
        core::budget b;
        if      (avgac < -lift_budget_to.get())  b.current = tolerance.get() - avgac;
        else if (avgac < -lift_budget_min.get()) b.current = tolerance.get() + lift_budget_to.get();
        else                                     b.current = tolerance.get() - avgac;
        return b;
    }
} green;


}


