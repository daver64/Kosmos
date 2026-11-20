#include "ship_internal.h"
#include "vessel_ascent_limits.h"
#include "vessel_guidance_profile.h"

#include <cmath>
#include <cstdio>

int main()
{
    int failures = 0;
    const auto check = [&](bool ok, const char *message) {
        std::printf("%s: %s\n", ok ? "ok" : "FAIL", message);
        failures += !ok;
    };
    using namespace kosmos;
    check(vessel_ascent_request_matches(vessel_guidance_profile("Ares Spider"), "Ares"), "Ares profile");
    check(vessel_ascent_request_matches(vessel_guidance_profile("CVEL Deltaglider"), "Deltaglider") &&
          vessel_ascent_request_matches(vessel_guidance_profile("CVEL Deltaglider (Nuclear)"), "Deltaglider"),
          "Deltaglider variants");
    for (const char *name : {"CVEL Shuttle-A", "CVEL Shuttle-A (Nuclear)", "CVEL Shuttle-AQ"})
        check(vessel_ascent_request_matches(vessel_guidance_profile(name), "Shuttle-A"), name);
    check(vessel_guidance_profile("CVEL Soyuz TMA") == VesselGuidanceProfile::soyuz &&
          !vessel_ascent_request_matches(VesselGuidanceProfile::soyuz, "Ares"), "Soyuz landing only");
    check(vessel_guidance_profile("CVEL Shuttle-A variant") == VesselGuidanceProfile::none &&
          !vessel_ascent_request_matches(VesselGuidanceProfile::none, "Shuttle-A"), "unknown names excluded");
    check(std::string_view(vessel_ascent_phase_name(VesselAscentPhase::liftoff)) == "Liftoff" &&
          std::string_view(vessel_ascent_phase_name(VesselAscentPhase::climb)) == "Climb" &&
          std::string_view(vessel_ascent_phase_name(VesselAscentPhase::orbit_burn)) == "Orbit burn" &&
          std::string_view(vessel_ascent_phase_name(VesselAscentPhase::coast)) == "Coast" &&
          std::string_view(vessel_ascent_phase_name(VesselAscentPhase::circularise)) == "Circularise",
          "ascent phase labels remain compatible with MFDs");
    check(vessel_ascent_pressure_factor(70000.0) == 1.0 &&
          vessel_ascent_pressure_factor(110000.0) == 0.5 &&
          std::abs(vessel_ascent_pressure_factor(126000.0) - 0.3) < 1e-12 &&
          vessel_ascent_pressure_factor(200000.0) == 0.3,
          "ascent pressure limiter applies and clamps at its boundaries");

    ShipSpec spec;
    spec.stages.resize(2);
    spec.stages[0].z = 1.0;
    spec.stages[0].length = 4.0;
    spec.stages[1].gear.push_back(sl::dvec3(0, 0, 5));
    check(ship_detail::rocket_lowest_point(spec) == 5.0, "rocket height includes gear");
    spec.stages[1].gear.clear();
    check(ship_detail::rocket_lowest_point(spec) == 3.0, "rocket height includes stage end");
    const sl::dvec3 up(0, 1, 0), pole(0, 0, 1);
    const sl::dquat initial = ship_detail::rocket_pad_orientation(spec, up, pole);
    spec.pad_turn = 90.0;
    const sl::dquat turned = ship_detail::rocket_pad_orientation(spec, up, pole);
    check(glm::length(initial * sl::dvec3(0, 0, -1) - up) < 1e-9 &&
          glm::length(turned * sl::dvec3(0, 0, -1) - up) < 1e-9 &&
          std::abs(glm::dot(initial * sl::dvec3(0, 1, 0), turned * sl::dvec3(0, 1, 0))) < 1e-9,
          "pad turn rotates roof while keeping rocket upright");
    return failures ? 1 : 0;
}
