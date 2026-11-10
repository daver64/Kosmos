#undef NDEBUG
#include "lights.h"

#include <cassert>

int main()
{
    sl::PropertyTree props;
    kosmos::LightSpec nav = kosmos::parse_light({{"navigation", true}});
    kosmos::LightSpec lamp = kosmos::parse_light({{"navigation", false}});
    kosmos::LightSpec strobe = kosmos::parse_light({
        {"navigation", true}, {"pattern", "strobe"}, {"control", "/controls/lighting/strobe"}});

    assert(!kosmos::ship_light_enabled(nav, props));
    assert(kosmos::ship_light_enabled(lamp, props));
    assert(!kosmos::ship_light_enabled(strobe, props));
    props.set_bool("/controls/lighting/nav", true);
    assert(kosmos::ship_light_enabled(nav, props));
    assert(!kosmos::ship_light_enabled(strobe, props));
    props.set_double("/controls/lighting/strobe", 1.0);
    props.set_bool("/controls/lighting/nav", false);
    props.set_bool("/controls/lighting/lamps", false);
    assert(!kosmos::ship_light_enabled(nav, props));
    assert(kosmos::ship_light_enabled(strobe, props));
    assert(!kosmos::ship_light_enabled(lamp, props));
    assert(!kosmos::ship_light_enabled(strobe, props, true));
    strobe.always = true;
    assert(kosmos::ship_light_enabled(strobe, props, true));
    props.set_bool("/controls/lighting/strobe", false);
    assert(!kosmos::ship_light_enabled(strobe, props, true));
    const auto saved = kosmos::light_to_json(strobe);
    assert(saved.at("control") == "/controls/lighting/strobe");
    assert(saved.at("always") == true);
    assert(kosmos::parse_light(saved).control == strobe.control);
}
