#undef NDEBUG
#include "reentry.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace kosmos;

namespace
{
    constexpr double mu = 3.986004418e14, radius = 6.371e6, pi = 3.14159265358979323846;

    double density(double altitude) { return 1.225 * std::exp(-altitude / 7200.0); }

    struct Entry
    {
        double peak_g = 0.0, peak_flux = 0.0, peak_temp = 0.0, damage = 0.0;
        double final_speed = 0.0, range = 0.0, burnt = 0.0;
    };

    // Planar entry of a vehicle held at constant angle of attack by the hypersonic model (x forward, y up), integrated in 0.1 s steps.
    Entry fly(double speed, double path_deg, double mass, const HypersonicSpec &aero, const ThermalSpec &thermal, double trim_roll_up)
    {
        Entry out;
        ThermalState heat;
        double x = 0.0, y = radius + 120.0e3, vx = speed * std::cos(path_deg * pi / 180.0), vy = speed * std::sin(path_deg * pi / 180.0);
        double mission = 0.0;
        for (; mission < 3000.0 && y > radius; mission += 0.1)
        {
            const double alt = y - radius, v = std::hypot(vx, vy), rho = density(alt);
            const double g = mu / (y * y);
            // Body frame: -Z is the nose, +Y up; the vehicle flies belly-first or shield-first at its trim angle, pitched by `trim_roll_up`.
            const double heading = std::atan2(vy, vx);
            // Local frame x' along velocity, y' perpendicular (up). Windward axis rotated from the velocity by the trim angle.
            const double trim = aero.trim_deg * pi / 180.0;
            const double axis_angle = heading - trim * (trim_roll_up > 0.0 ? 1.0 : -1.0);
            const sl::dvec3 vhat(std::cos(heading), std::sin(heading), 0.0);
            const sl::dvec3 axis(std::cos(axis_angle), std::sin(axis_angle), 0.0);
            // Express the velocity in a frame where `axis` is the windward axis and +Z is the other in-plane direction.
            HypersonicSpec s = aero;
            s.axis = axis;
            s.trim_dir = sl::dvec3(0.0, 0.0, 1.0);
            const AeroResult r = hypersonic_aero(s, vhat * v, rho, sl::dvec3(0.0));
            const double ax = r.force.x / mass, ay = r.force.y / mass;
            out.peak_g = std::max(out.peak_g, std::hypot(ax, ay) / 9.80665);
            vx += ax * 0.1;
            vy += (ay - g + vx * vx / y) * 0.1;
            x += vx * 0.1;
            y += vy * 0.1;
            const double windward = glm::dot(axis, vhat);
            out.damage += step_thermal(thermal, heat, rho, v, windward, 250.0, 0.1) * 0.1;
            out.peak_flux = std::max(out.peak_flux, heat.flux);
            out.peak_temp = std::max(out.peak_temp, heat.temp);
            if (alt > 119000.0 && mission > 5.0 && vy > 0.0 && y > radius + 125e3)
                break; // skipped out
        }
        out.final_speed = std::hypot(vx, vy);
        out.range = x;
        out.burnt = thermal.shield_mass - std::max(heat.shield, 0.0);
        return out;
    }
} // namespace

int main()
{
    // Sutton-Graves: scales as sqrt(rho / Rn) v^3 and is zero without air.
    assert(stagnation_flux(0.0, 7000.0, 1.0) == 0.0);
    const double base = stagnation_flux(1e-4, 7000.0, 1.0);
    assert(std::fabs(stagnation_flux(4e-4, 7000.0, 1.0) / base - 2.0) < 1e-9);
    assert(std::fabs(stagnation_flux(1e-4, 7000.0, 4.0) / base - 0.5) < 1e-9);
    assert(std::fabs(stagnation_flux(1e-4, 14000.0, 1.0) / base - 8.0) < 1e-9);

    // A shuttle-like vehicle: passive tiles, belly-first at 40 degrees angle of attack (50 from the belly normal).
    ThermalSpec tiles;
    tiles.enabled = true;
    tiles.nose_radius = 0.6;
    tiles.heat_area = 300.0;
    tiles.max_temp = 1900.0;
    HypersonicSpec shuttle;
    shuttle.enabled = true;
    shuttle.area = 250.0;
    shuttle.cd_max = 1.8;
    shuttle.cd_skin = 0.05;
    shuttle.trim_deg = 50.0;
    const Entry sh = fly(7800.0, -1.0, 90000.0, shuttle, tiles, 1.0);
    std::printf("shuttle: peak %.2f g, %.0f kW/m2 stagnation, %.0f K, damage %.3f, range %.0f km\n", sh.peak_g, sh.peak_flux / 1e3, sh.peak_temp, sh.damage, sh.range / 1e3);
    std::fflush(stdout);
    assert(sh.peak_g > 0.5 && sh.peak_g < 3.5);
    assert(sh.peak_flux > 3e5 && sh.peak_flux < 2.5e6);
    assert(sh.peak_temp > 1400.0 && sh.peak_temp < 2100.0);
    assert(sh.damage < 0.05);
    assert(sh.range > 3000e3);

    // An Apollo-like capsule at lunar-return speed, shield leading, ablator on: survives with some shield left, a few g, a hot but finite entry.
    ThermalSpec ablator;
    ablator.enabled = true;
    ablator.nose_radius = 4.7;
    ablator.heat_area = 12.0;
    ablator.shield_mass = 1400.0;
    ablator.ablation_heat = 1.2e7;
    ablator.shield_axis = sl::dvec3(1.0, 0.0, 0.0);
    HypersonicSpec capsule;
    capsule.enabled = true;
    capsule.area = 12.0;
    capsule.cd_max = 1.4;
    capsule.trim_deg = 20.0;
    const Entry cap = fly(11000.0, -6.5, 5500.0, capsule, ablator, 1.0);
    std::printf("capsule: peak %.2f g, %.0f kW/m2, shield burnt %.0f kg, damage %.3f\n", cap.peak_g, cap.peak_flux / 1e3, cap.burnt, cap.damage);
    assert(cap.peak_g > 3.0 && cap.peak_g < 9.0);
    assert(cap.burnt > 20.0 && cap.burnt < ablator.shield_mass);

    // The same capsule heat shield turned away from the flow is destroyed.
    ThermalSpec backwards = ablator;
    backwards.shield_axis = sl::dvec3(-1.0, 0.0, 0.0);
    ThermalState state;
    double dmg = 0.0;
    for (int i = 0; i < 600; ++i)
        dmg += step_thermal(backwards, state, 5e-5, 7500.0, -1.0, 250.0, 0.1) * 0.1;
    assert(dmg > 0.3);
    assert(!state.shielded);

    // A warhead (small nose, tiny mass, steep and fast) gets far hotter than the capsule per unit area.
    ThermalSpec warhead;
    warhead.enabled = true;
    warhead.nose_radius = 0.05;
    assert(stagnation_flux(1e-3, 6000.0, warhead.nose_radius) > 9.0 * stagnation_flux(1e-3, 6000.0, 4.7));

    // The restoring moment points toward the trim attitude: flow dead ahead of the axis pitches the nose toward the trim direction.
    HypersonicSpec trim;
    trim.enabled = true;
    trim.axis = sl::dvec3(0.0, -1.0, 0.0);
    trim.trim_dir = sl::dvec3(0.0, 0.0, -1.0);
    trim.trim_deg = 50.0;
    const AeroResult a = hypersonic_aero(trim, sl::dvec3(0.0, 0.0, -3000.0), 1e-3, sl::dvec3(0.0));
    assert(a.blend > 0.99 && glm::length(a.torque) > 0.0);
    // At the trim angle there is no moment.
    const double ta = 50.0 * pi / 180.0;
    const AeroResult t = hypersonic_aero(trim, sl::dvec3(0.0, -std::cos(ta) * 3000.0, -std::sin(ta) * 3000.0), 1e-3, sl::dvec3(0.0));
    assert(glm::length(t.torque) < 1e-6 * glm::length(a.torque));
    // Slow flight is left to the ship's own aerodynamics.
    assert(hypersonic_aero(trim, sl::dvec3(0.0, 0.0, -200.0), 1.0, sl::dvec3(0.0)).blend == 0.0);

    // Parachute: the terminal speed under a canopy of CdA 1000 m^2 and 5500 kg at sea level is about 9 m/s.
    ParachuteSpec chute;
    chute.cd_area = 1000.0;
    ChuteRuntime runtime;
    runtime.state = ChuteState::open;
    const double terminal = std::sqrt(2.0 * 5500.0 * 9.80665 / (1.225 * chute_drag_area(chute, runtime)));
    assert(terminal > 8.0 && terminal < 11.0);
    runtime.state = ChuteState::stowed;
    assert(chute_drag_area(chute, runtime) == 0.0);
    runtime.state = ChuteState::opening;
    runtime.fill = 0.5;
    assert(chute_drag_area(chute, runtime) > chute.reef_cd_area && chute_drag_area(chute, runtime) < chute.cd_area);

    std::printf("reentry tests passed\n");
    return 0;
}
