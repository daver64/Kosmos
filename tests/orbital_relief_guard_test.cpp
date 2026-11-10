#undef NDEBUG
#include "orbital_relief_guard.h"
#include "base.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <string>

int main()
{
    kosmos::Base base;
    base.direction = glm::dvec3(0.0, 1.0, 0.0);
    base.height = 10.0;
    base.flat_radius = 300.0;
    base.blend = 200.0;
    const glm::dvec3 base_centre(10.0, 20.0, 30.0);
    const glm::dvec3 base_position = base_centre + base.direction * 1010.0;
    const double limit = kosmos::base_draw_range + 800.0;
    assert(kosmos::base_in_draw_range(base, base_centre, 1000.0, base_position, kosmos::base_draw_range));
    assert(kosmos::base_in_draw_range(base, base_centre, 1000.0, base_position + glm::dvec3(limit, 0.0, 0.0), kosmos::base_draw_range));
    assert(!kosmos::base_in_draw_range(base, base_centre, 1000.0, base_position + glm::dvec3(limit + 0.01, 0.0, 0.0), kosmos::base_draw_range));
    base.flat_radius = std::numeric_limits<double>::quiet_NaN();
    assert(kosmos::base_in_draw_range(base, base_centre, 1000.0, base_position, kosmos::base_draw_range));
    base.flat_radius = 300.0;
    base.direction.x = std::numeric_limits<double>::infinity();
    assert(kosmos::base_in_draw_range(base, base_centre, 1000.0, base_position, kosmos::base_draw_range));

    const glm::dvec3 centre(10.0, 20.0, 30.0);
    const double radius = 1000.0;
    sl::FlightBody vessel;
    vessel.position = centre + glm::dvec3(0.0, 1400.0, 0.0);
    vessel.orientation = glm::angleAxis(glm::half_pi<double>(), glm::dvec3(0.0, 1.0, 0.0));
    sl::GearContact contact;
    contact.position = glm::dvec3(100.0, -400.0, 0.0);
    vessel.gear.push_back(contact);
    const glm::dvec3 far_origin(20000.0, 0.0, 0.0);
    sl::FlightBody large;
    large.position = far_origin;
    large.orientation = glm::angleAxis(glm::half_pi<double>(), glm::dvec3(0.0, 1.0, 0.0));
    sl::GearContact distant_gear;
    distant_gear.position = glm::dvec3(0.0, 0.0, -10000.0);
    large.gear.push_back(distant_gear);
    assert(kosmos::relief_vessel_near_camera(far_origin, large, glm::dvec3(0.0), 10000.0));
    large.gear[0].position.z = -9999.0;
    assert(!kosmos::relief_vessel_near_camera(far_origin, large, glm::dvec3(0.0), 10000.0));
    assert(kosmos::relief_vessel_near_camera(glm::dvec3(10000.0, 0.0, 0.0), large, glm::dvec3(0.0), 10000.0));
    large.gear[0].position.z = -20000.0;
    assert(kosmos::relief_vessel_near_camera(far_origin, large, glm::dvec3(0.0), 10000.0));
    std::string large_reason;
    int large_checks = 0;
    const auto flat_height = [](const glm::dvec3 &) { return 0.0; };
    const auto reject_height = [&](const glm::dvec3 &, double, const char *)
    {
        ++large_checks;
        return false;
    };
    assert(!kosmos::check_relief_gear_contacts(large, glm::dvec3(0.0, -1000.0, 0.0), glm::dquat(1.0, 0.0, 0.0, 0.0),
                                               1000.0, glm::dvec3(0.0), 10000.0, flat_height, reject_height, large_reason));
    assert(large_checks == 1);
    distant_gear.position.z = -9999.0;
    large.gear.push_back(distant_gear);
    large_checks = 0;
    const auto near_only = [&](const glm::dvec3 &, double, const char *)
    {
        ++large_checks;
        return true;
    };
    assert(kosmos::check_relief_gear_contacts(large, glm::dvec3(0.0, -1000.0, 0.0), glm::dquat(1.0, 0.0, 0.0, 0.0),
                                              1000.0, glm::dvec3(0.0), 10000.0, flat_height, near_only, large_reason));
    assert(large_checks == 1);
    large.gear.pop_back();
    const glm::dvec3 near_contact = large.position + large.direction_to_world(large.gear[0].position);
    large_checks = 0;
    assert(kosmos::check_relief_gear_contacts(large, glm::dvec3(0.0, -1000.0, 0.0), glm::dquat(1.0, 0.0, 0.0, 0.0),
                                              1000.0, near_contact + glm::dvec3(10000.0, 0.0, 0.0), 10000.0,
                                              flat_height, near_only, large_reason));
    assert(large_checks == 1);
    large_checks = 0;
    assert(kosmos::check_relief_gear_contacts(large, glm::dvec3(0.0, -1000.0, 0.0), glm::dquat(1.0, 0.0, 0.0, 0.0),
                                              1000.0, near_contact + glm::dvec3(10000.01, 0.0, 0.0), 10000.0,
                                              flat_height, near_only, large_reason));
    assert(large_checks == 0);
    large.gear[0].position.z = std::numeric_limits<double>::quiet_NaN();
    assert(kosmos::relief_vessel_near_camera(far_origin, large, glm::dvec3(0.0), 10000.0));
    assert(kosmos::relief_vessel_near_camera(glm::dvec3(std::numeric_limits<double>::infinity(), 0.0, 0.0),
                                             large, glm::dvec3(0.0), 10000.0));
    const glm::dquat world_to_tile = glm::angleAxis(glm::half_pi<double>(), glm::dvec3(0.0, 1.0, 0.0));
    const glm::dvec3 expected_direction = glm::normalize(glm::dvec3(-100.0, 1000.0, 0.0));
    std::string reason;
    int checks = 0;
    auto collision = [&](const glm::dvec3 &direction)
    {
        assert(glm::length(direction - expected_direction) < 1e-10);
        return 0.0;
    };
    auto check = [&](const glm::dvec3 &direction, double expected, const char *where)
    {
        ++checks;
        assert(glm::length(direction - expected_direction) < 1e-10);
        assert(expected == 0.0 && std::string(where) == "ship gear");
        reason = "ship gear: drawn height mismatch";
        return false;
    };
    assert(!kosmos::check_relief_gear_contacts(vessel, centre, world_to_tile, radius, centre, 10000.0, collision, check, reason));
    assert(checks == 1 && reason == "ship gear: drawn height mismatch");

    vessel.gear.insert(vessel.gear.begin(), contact);
    checks = 0;
    auto fail_second = [&](const glm::dvec3 &, double, const char *)
    {
        return ++checks < 2;
    };
    assert(!kosmos::check_relief_gear_contacts(vessel, centre, world_to_tile, radius, centre, 10000.0, collision, fail_second, reason));
    assert(checks == 2);
    auto pass = [](const glm::dvec3 &, double, const char *) { return true; };
    assert(kosmos::check_relief_gear_contacts(vessel, centre, world_to_tile, radius, centre, 10000.0, collision, pass, reason));
    vessel.gear.erase(vessel.gear.begin());

    vessel.gear[0].position.y = -100.0;
    checks = 0;
    auto flat = [](const glm::dvec3 &) { return 0.0; };
    assert(kosmos::check_relief_gear_contacts(vessel, centre, world_to_tile, radius, centre, 10000.0, flat, check, reason));
    assert(checks == 0);

    vessel.position = centre + glm::dvec3(0.0, 1250.0, 0.0);
    vessel.gear[0].position = glm::dvec3(0.0);
    checks = 0;
    auto reject = [&](const glm::dvec3 &, double, const char *) { ++checks; return false; };
    assert(!kosmos::check_relief_gear_contacts(vessel, centre, world_to_tile, radius, centre, 10000.0, flat, reject, reason));
    assert(checks == 1);
    vessel.position.y += 0.01;
    checks = 0;
    assert(kosmos::check_relief_gear_contacts(vessel, centre, world_to_tile, radius, centre, 10000.0, flat, reject, reason));
    assert(checks == 0);

    vessel.position = centre + glm::dvec3(0.0, 1400.0, 0.0);
    vessel.gear[0].position.x = 100.0;
    vessel.gear[0].position.y = -400.0;
    auto missing = [](const glm::dvec3 &) { return std::numeric_limits<double>::quiet_NaN(); };
    assert(!kosmos::check_relief_gear_contacts(vessel, centre, world_to_tile, radius, centre, 10000.0, missing, check, reason));
    assert(reason == "ship gear: collision height is not finite");

    vessel.gear[0].position.x = std::numeric_limits<double>::infinity();
    assert(!kosmos::check_relief_gear_contacts(vessel, centre, world_to_tile, radius, centre, 10000.0, flat, check, reason));
    assert(reason == "ship gear position is invalid");
}
