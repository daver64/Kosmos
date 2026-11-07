#include "plasma_look.h"

#include <cassert>
#include <cstdio>

using kosmos::plasma_look;

int main()
{
    // No glow in thin or slow air.
    assert(plasma_look(1.0e3, 2.0, 1.0).brilliance == 0.0f);

    const double flux = 1.0e6;
    const kosmos::PlasmaLook warhead = plasma_look(flux, 0.75, 0.3);
    const kosmos::PlasmaLook capsule = plasma_look(flux, 1.4, 2.0);
    const kosmos::PlasmaLook shuttle = plasma_look(flux, 7.7, 5.0);

    // A big blunt vehicle glows brighter and wider than a capsule, which beats a slim warhead.
    assert(warhead.brilliance < capsule.brilliance && capsule.brilliance < shuttle.brilliance);
    assert(warhead.width < capsule.width && capsule.width < shuttle.width);
    assert(warhead.brilliance < 0.3f && shuttle.brilliance > 0.5f);
    const kosmos::PlasmaLook cooler_shuttle = plasma_look(8.0e5, 7.7, 5.0);
    assert(cooler_shuttle.brilliance > 0.4f && cooler_shuttle.brilliance < 0.5f);
    assert(cooler_shuttle.brilliance < shuttle.brilliance);
    // The warhead's wake is a long streak compared with its width; the big ball is not.
    assert(warhead.length / warhead.width > shuttle.length / shuttle.width);

    // More flux means more brilliance and a hotter colour.
    const kosmos::PlasmaLook hotter = plasma_look(3.0e6, 1.4, 2.0);
    assert(hotter.brilliance > capsule.brilliance && hotter.heat > capsule.heat);

    std::printf("plasma_look test passed\n");
    return 0;
}
