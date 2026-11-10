#include "landing_plane.h"
#include "landing_descent.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>

namespace
{
    int failures = 0;

    void check(bool condition, const char *description)
    {
        std::printf("%s: %s\n", condition ? "ok" : "FAIL", description);
        failures += condition ? 0 : 1;
    }
}

int main()
{
    const sl::dvec3 position(1000000.0, 0.0, 0.0);
    const sl::dvec3 velocity(120.0, 3000.0, 0.0);
    const double angle = 0.4;
    const sl::dvec3 target = glm::normalize(sl::dvec3(0.5, std::cos(angle), std::sin(angle)));
    const auto plan = kosmos::landing_plane_change(position, velocity, target);
    const sl::dvec3 changed = velocity + plan.delta_v;
    check(plan.valid, "valid tilted target");
    check(std::abs(plan.angle - angle) < 1e-12, "plane angle");
    check(std::abs(glm::length(plan.delta_v) - 6000.0 * std::sin(angle / 2.0)) < 1e-9, "plane-change delta-v");
    check(std::abs(glm::length(changed) - glm::length(velocity)) < 1e-9, "speed preserved");
    check(std::abs(changed.x - velocity.x) < 1e-12, "radial velocity preserved");
    check(std::abs(glm::dot(glm::normalize(glm::cross(position, changed)), target)) < 1e-12,
          "resulting plane contains target");

    const auto opposite = kosmos::landing_plane_change(position, velocity, -target);
    check(opposite.valid && glm::length(opposite.delta_v - plan.delta_v) < 1e-9, "same plane for antipodal target");
    const auto coplanar = kosmos::landing_plane_change(position, velocity, sl::dvec3(0.5, -1.0, 0.0));
    check(coplanar.valid && glm::length(coplanar.delta_v) < 1e-9, "coplanar target needs no burn");
    const auto retrograde = kosmos::landing_plane_change(position, -velocity, target);
    check(retrograde.valid && glm::length(retrograde.delta_v + plan.delta_v) < 1e-9,
          "retrograde orbit uses its nearest plane");
    check(!kosmos::landing_plane_change(sl::dvec3(0.0), velocity, target).valid, "reject zero position");
    check(!kosmos::landing_plane_change(position, sl::dvec3(10.0, 0.0, 0.0), target).valid, "reject radial trajectory");
    check(!kosmos::landing_plane_change(position, velocity, position).valid, "reject ambiguous radial target");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto approach = kosmos::landing_approach_command(sl::dvec3(1000000.0, 0.0, 0.0),
                                                           sl::dvec3(0.0), sl::dvec3(1.0, 0.0, 0.0),
                                                           500.0, 0.0, 4.0, 10.0);
    check(approach.valid && approach.distance == 0.0 && approach.direction.x > 0.999 &&
              approach.level > 0.0 && approach.level <= 1.0,
          "local approach directs thrust upward and produces bounded throttle");
    check(!kosmos::landing_approach_command(sl::dvec3(0.0), sl::dvec3(0.0), sl::dvec3(1.0, 0.0, 0.0),
                                            500.0, 0.0, 4.0, 10.0).valid &&
              !kosmos::landing_approach_command(sl::dvec3(1.0, 0.0, 0.0), sl::dvec3(0.0), sl::dvec3(1.0, 0.0, 0.0),
                                               500.0, 0.0, 4.0, nan).valid,
          "local approach rejects invalid geometry and acceleration");
    check(!kosmos::landing_plane_change(position, velocity, sl::dvec3(nan, 1.0, 0.0)).valid, "reject non-finite target");
    const double burn_mass = 30000.0, burn_flow = 80.0, burn_thrust = 240000.0, burn_dv = 600.0;
    const double short_remaining = 0.2, acceleration = 2.0;
    const double level_one = kosmos::landing_plane_burn_level(short_remaining, acceleration, 0.1);
    const double level_four = kosmos::landing_plane_burn_level(short_remaining, acceleration, 0.4);
    check(std::abs(level_one - 1.0) < 1e-12 && std::abs(level_four - 0.25) < 1e-12 &&
              std::abs(level_four * acceleration * 0.4 - short_remaining) < 1e-12 &&
              kosmos::landing_plane_burn_level(5.0, acceleration, 0.4) == 1.0,
          "finite burn limits the last throttle step using the effective warped interval");
    const auto burn = kosmos::landing_plane_burn(burn_dv, burn_mass, 10000.0, 1000.0, burn_thrust, burn_flow);
    const double expected_burn_fuel = burn_mass * -std::expm1(-burn_dv * burn_flow / burn_thrust);
    check(burn.valid && burn.sufficient_fuel && std::abs(burn.fuel - expected_burn_fuel) < 1e-9 &&
              std::abs(burn.duration - expected_burn_fuel / burn_flow) < 1e-10,
          "finite plane burn predicts rocket-equation fuel and duration");
    double weighted_time = 0.0, delivered = 0.0;
    constexpr int samples = 100000;
    for (int i = 0; i < samples; ++i)
    {
        const double time = burn.duration * (i + 0.5) / samples;
        const double dv = burn_thrust / (burn_mass - burn_flow * time) * burn.duration / samples;
        delivered += dv;
        weighted_time += time * dv;
    }
    check(burn.valid && std::abs(delivered - burn_dv) < 1e-8 &&
              std::abs(burn.impulse_center - weighted_time / delivered) < 1e-9 &&
              burn.impulse_center > burn.duration / 2.0,
          "finite plane burn centers its delta-v weighted impulse");
    const auto tiny_burn = kosmos::landing_plane_burn(1e-9, burn_mass, 10000.0, 0.0, burn_thrust, burn_flow);
    check(tiny_burn.valid && tiny_burn.sufficient_fuel && tiny_burn.duration > 0.0 &&
              std::abs(tiny_burn.impulse_center / tiny_burn.duration - 0.5) < 1e-10,
          "tiny plane burn has a numerically stable impulse center");
    const auto zero_burn = kosmos::landing_plane_burn(0.0, burn_mass, 10000.0, 0.0, burn_thrust, burn_flow);
    check(zero_burn.valid && zero_burn.sufficient_fuel && zero_burn.fuel == 0.0 &&
              zero_burn.duration == 0.0 && zero_burn.impulse_center == 0.0,
          "zero plane change requires no burn");
    const auto reserved = kosmos::landing_plane_burn(burn_dv, burn_mass, 10000.0, 5000.0, burn_thrust, burn_flow);
    check(reserved.valid && !reserved.sufficient_fuel, "plane burn respects reserved descent fuel");
    check(!kosmos::landing_plane_burn(nan, burn_mass, 10000.0, 0.0, burn_thrust, burn_flow).valid &&
              !kosmos::landing_plane_burn(burn_dv, burn_mass, burn_mass, 0.0, burn_thrust, burn_flow).valid &&
              !kosmos::landing_plane_burn(burn_dv, burn_mass, 10000.0, -1.0, burn_thrust, burn_flow).valid &&
              !kosmos::landing_plane_burn(burn_dv, burn_mass, 10000.0, 0.0, 0.0, burn_flow).valid &&
              !kosmos::landing_plane_burn(burn_dv, burn_mass, 10000.0, 0.0, burn_thrust, 0.0).valid,
          "plane burn rejects invalid propulsion and mass inputs");
    const sl::dvec3 initial_r(1.0, 0.0, 0.0), initial_v(0.0, 0.0, -1.0), pole(0.0, 1.0, 0.0);
    const double initial_angle = 0.8, lead = 0.3;
    const sl::dvec3 destination(std::cos(initial_angle), 0.0, -std::sin(initial_angle));
    const auto fixed = kosmos::landing_plane_window(1.0, initial_r, initial_v, destination, pole, 0.0, 2.0, lead, 3.0);
    check(fixed.valid && std::abs(fixed.wait - 0.5) < 1e-8, "fixed target departure window");
    check(fixed.valid && glm::length(fixed.change.delta_v) < 1e-9, "fixed coplanar target needs no plane change");
    const auto rotating = kosmos::landing_plane_window(1.0, initial_r, initial_v, destination, pole, 0.1, 2.0, lead, 3.0);
    check(rotating.valid && std::abs(rotating.wait - (0.8 + 0.2 - lead) / 0.9) < 1e-8,
          "departure window predicts target rotation through arrival");
    check(rotating.valid &&
              std::abs(glm::dot(glm::normalize(rotating.departure_position), rotating.arrival_target) - std::cos(lead)) < 1e-10,
          "arrival target has the required lead angle");
    const auto retro_spin = kosmos::landing_plane_window(1.0, initial_r, initial_v, destination, pole, -0.1, 2.0, lead, 3.0);
    check(retro_spin.valid && std::abs(retro_spin.wait - (0.8 - 0.2 - lead) / 1.1) < 1e-8,
          "departure window handles reverse body rotation");
    const auto fast_spin = kosmos::landing_plane_window(1.0, initial_r, initial_v, destination, pole, 10.0, 0.0, lead, 3.0);
    check(fast_spin.valid &&
              std::abs(glm::dot(glm::normalize(fast_spin.departure_position), fast_spin.arrival_target) - std::cos(lead)) < 1e-10,
          "departure window samples fast body rotation");
    const sl::dvec3 tilted_target(std::cos(0.8) * std::cos(0.1), std::sin(0.1),
                                  -std::sin(0.8) * std::cos(0.1));
    const auto tilted = kosmos::landing_plane_window(1.0, initial_r, initial_v, tilted_target, pole, 0.0, 2.0, lead, 3.0);
    check(tilted.valid && glm::length(tilted.change.delta_v) > 0.0 &&
              std::abs(glm::dot(glm::normalize(glm::cross(tilted.departure_position,
                                                           tilted.departure_velocity + tilted.change.delta_v)),
                                tilted.arrival_target)) < 1e-10,
          "tilted target receives a plane change at the departure window");
    const auto windows = kosmos::landing_plane_windows(1.0, initial_r, initial_v, tilted_target, pole,
                                                       0.1, 2.0, lead, 18.0);
    bool ordered = windows.size() >= 2;
    for (std::size_t i = 0; i < windows.size(); ++i)
    {
        const auto &window = windows[i];
        ordered = ordered && window.valid && window.change.valid && window.wait >= 0.0 && window.wait <= 18.0 &&
                  (i == 0 || windows[i - 1].wait < window.wait) &&
                  std::abs(glm::dot(glm::normalize(window.departure_position), window.arrival_target) -
                           std::cos(lead)) < 1e-10;
    }
    check(ordered, "candidate windows are distinct, chronological and satisfy the requested lead");
    const auto best_window = kosmos::landing_plane_window(1.0, initial_r, initial_v, tilted_target, pole,
                                                           0.1, 2.0, lead, 18.0);
    const auto least_dv = std::min_element(windows.begin(), windows.end(), [](const auto &a, const auto &b) {
        return glm::length(a.change.delta_v) < glm::length(b.change.delta_v);
    });
    check(best_window.valid && least_dv != windows.end() && best_window.wait == least_dv->wait,
          "single-window API still chooses the lowest delta-v candidate");
    check(kosmos::landing_plane_windows(1.0, initial_r, initial_v, destination, pole, 0.0, 2.0, lead, 0.1).empty(),
          "candidate search returns no windows when no crossing exists");
    check(kosmos::landing_plane_windows(1.0, initial_r, initial_v, destination, pole, nan, 2.0, lead, 3.0).empty(),
          "candidate search rejects invalid input");
    const std::vector<double> leads{0.3, 0.4, 0.5};
    const auto candidates = kosmos::landing_candidates(1.0, initial_r, initial_v, tilted_target, pole, 0.1, 2.0,
                                                        18.0, leads, 1000.0, 500.0, 100.0, 1000.0, 1.0, 0.0, 240.0);
    bool candidate_properties = candidates.size() >= 3;
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        const auto &c = candidates[i];
        candidate_properties = candidate_properties && c.window.valid && c.burn.valid && c.burn.sufficient_fuel &&
                               c.burn.fuel <= 400.0 && c.ignition_wait >= 0.0 &&
                               std::abs(c.ignition_wait - (c.window.wait - c.burn.impulse_center)) < 1e-12 &&
                               (i == 0 || candidates[i - 1].burn.fuel <= c.burn.fuel);
    }
    check(candidate_properties, "candidate search sorts and validates finite burns across multiple leads");
    const auto duplicate_leads = kosmos::landing_candidates(1.0, initial_r, initial_v, tilted_target, pole, 0.1, 2.0,
                                                             18.0, {0.3, 0.4, 0.5, 0.3}, 1000.0, 500.0,
                                                             100.0, 1000.0, 1.0, 0.0, 240.0);
    check(duplicate_leads.size() == candidates.size(), "candidate search deduplicates repeated leads");
    const auto no_fuel = kosmos::landing_candidates(1.0, initial_r, initial_v, tilted_target, pole, 0.1, 2.0,
                                                     18.0, leads, 1000.0, 500.0, 500.0, 1000.0, 1.0, 0.0, 240.0);
    check(no_fuel.empty(), "candidate search preserves fuel reserved for descent");
    const auto no_alignment = kosmos::landing_candidates(1.0, initial_r, initial_v, tilted_target, pole, 0.1, 2.0,
                                                          18.0, leads, 1000.0, 500.0, 100.0, 1000.0, 1.0, 19.0, 240.0);
    check(no_alignment.empty(), "candidate search requires enough time to align");
    const auto invalid_leads = kosmos::landing_candidates(1.0, initial_r, initial_v, tilted_target, pole, 0.1, 2.0,
                                                          18.0, {0.3, nan}, 1000.0, 500.0, 100.0, 1000.0, 1.0, 0.0, 240.0);
    check(invalid_leads.empty(), "candidate search rejects a partially invalid lead-angle set");
    const sl::dvec3 initial_window_target(std::cos(lead), 0.0, -std::sin(lead));
    const auto initial_windows = kosmos::landing_plane_windows(1.0, initial_r, initial_v, initial_window_target,
                                                               pole, 0.0, 0.0, lead, 7.0);
    check(initial_windows.size() == 2 && initial_windows[0].wait == 0.0 &&
              std::abs(initial_windows[1].wait - 2.0 * std::acos(-1.0)) < 1e-8,
          "candidate search includes a current window once and the next orbit");
    check(!kosmos::landing_plane_window(1.0, initial_r, initial_v, destination, pole, 0.0, 2.0, lead, 0.1).valid,
          "no crossing in search interval");
    check(!kosmos::landing_plane_window(1.0, initial_r, initial_v, destination, pole, 0.0, 2.0, lead, 100.0).valid,
          "reject unbounded search interval");
    check(!kosmos::landing_plane_window(1.0, initial_r, sl::dvec3(0.0, 0.0, -2.0), destination,
                                        pole, 0.0, 2.0, lead, 3.0).valid, "reject unbound orbit");
    const auto eccentric = kosmos::landing_plane_window(1.0, initial_r, sl::dvec3(0.0, 0.0, -0.8),
                                                         destination, pole, 0.0, 2.0, lead, 3.0);
    check(eccentric.valid &&
              std::abs(glm::dot(glm::normalize(eccentric.departure_position), eccentric.arrival_target) - std::cos(lead)) < 1e-10,
          "departure window works for an eccentric orbit");
    check(!kosmos::landing_plane_window(1.0, initial_r, initial_v, destination, pole, nan, 2.0, lead, 3.0).valid,
          "reject non-finite body rotation");
    std::mt19937 random(17);
    std::uniform_real_distribution<double> coordinate(-1.0, 1.0);
    bool random_ok = true;
    for (int i = 0; i < 1000; ++i)
    {
        const sl::dvec3 r(coordinate(random), coordinate(random), coordinate(random));
        const sl::dvec3 v(coordinate(random), coordinate(random), coordinate(random));
        const sl::dvec3 t(coordinate(random), coordinate(random), coordinate(random));
        const auto candidate = kosmos::landing_plane_change(r, v, t);
        if (!candidate.valid)
        {
            random_ok = false;
            break;
        }
        const sl::dvec3 new_velocity = v + candidate.delta_v;
        const sl::dvec3 normal = glm::normalize(glm::cross(r, new_velocity));
        if (std::abs(glm::dot(normal, glm::normalize(t))) > 1e-10 ||
            std::abs(glm::length(new_velocity) - glm::length(v)) > 1e-10 ||
            std::abs(glm::dot(r, candidate.delta_v)) > 1e-10)
        {
            random_ok = false;
            break;
        }
    }
    check(random_ok, "random plane changes preserve speed and place the target in the new plane");
    check(kosmos::landing_descent_captured(1499.0, 301.0, 39.0, -24.0), "capture accepts values inside limits");
    check(!kosmos::landing_descent_captured(1500.0, 301.0, 39.0, 0.0), "capture excludes distance boundary");
    check(!kosmos::landing_descent_captured(0.0, 300.0, 0.0, 0.0), "capture excludes lower altitude boundary");
    check(!kosmos::landing_descent_captured(0.0, 3000.0, 0.0, 0.0), "capture excludes upper altitude boundary");
    check(!kosmos::landing_descent_captured(0.0, 1000.0, 40.0, 0.0), "capture excludes horizontal speed boundary");
    check(!kosmos::landing_descent_captured(0.0, 1000.0, 0.0, -25.0), "capture excludes vertical speed boundary");
    check(kosmos::landing_descent_remaining_seconds(0.0) == 650.0 &&
          kosmos::landing_descent_remaining_seconds(590.0) == 60.0 &&
          kosmos::landing_descent_remaining_seconds(700.0) == 60.0, "descent guidance time is clamped");
    const auto stationary = kosmos::landing_descent_command(sl::dvec3(2, 0, 0), sl::dvec3(0),
                                                             sl::dvec3(1, 0, 0), pole, 0.0, 4.0, 2.0, 10.0);
    check(stationary.valid && glm::length(stationary.acceleration - sl::dvec3(1, 0, 0)) < 1e-12,
          "stationary descent command cancels gravity");
    const auto turning = kosmos::landing_descent_command(sl::dvec3(2, 0, 0), sl::dvec3(0),
                                                          sl::dvec3(1, 0, 0), pole, 0.1, 4.0, 2.0, 10.0);
    const sl::dvec3 arrival(2.0 * std::cos(1.0), 0.0, -2.0 * std::sin(1.0));
    const sl::dvec3 arrival_velocity = glm::cross(pole * 0.1, arrival);
    const sl::dvec3 expected = 6.0 * (arrival - sl::dvec3(2, 0, 0)) / 100.0 - 0.2 * arrival_velocity +
                               sl::dvec3(1, 0, 0);
    check(turning.valid && glm::length(turning.acceleration - expected) < 1e-12,
          "descent command predicts rotating target position and velocity");
    check(!kosmos::landing_descent_command(initial_r, initial_v, destination, pole, 0.0, 1.0, 1.0, 0.0).valid,
          "descent command rejects zero time");
    check(!kosmos::landing_descent_command(initial_r, initial_v, destination, pole, nan, 1.0, 1.0, 10.0).valid,
          "descent command rejects non-finite spin");
    kosmos::LandingTrajectoryInput trajectory;
    trajectory.radius = 1000000.0;
    trajectory.mu = 1.0e12;
    trajectory.position = sl::dvec3(1002500.0, 0.0, 0.0);
    trajectory.target_direction = sl::dvec3(1.0, 0.0, 0.0);
    trajectory.spin_axis = pole;
    trajectory.mass = 10000.0;
    trajectory.fuel = 4000.0;
    trajectory.reserve = 500.0;
    trajectory.thrust = 100000.0;
    trajectory.flow = 20.0;
    trajectory.atmosphere_scale_height = 8500.0;
    const auto already_captured = kosmos::landing_trajectory_predict(trajectory);
    check(already_captured.captured && already_captured.fuel == trajectory.fuel &&
              already_captured.distance == 0.0 && already_captured.altitude == 2500.0,
          "trajectory screening recognizes a stationary local capture");
    trajectory.position.x = trajectory.radius - 1.0;
    check(!kosmos::landing_trajectory_predict(trajectory).captured, "trajectory screening rejects impact");
    trajectory.position.x = trajectory.radius + 2500.0;
    trajectory.reserve = trajectory.fuel + 1.0;
    check(!kosmos::landing_trajectory_predict(trajectory).captured, "trajectory screening enforces fuel reserve");
    trajectory.reserve = 500.0;
    trajectory.plane_delta_v = sl::dvec3(0.0, 100000.0, 0.0);
    check(!kosmos::landing_trajectory_predict(trajectory).captured, "trajectory screening rejects infeasible plane burn");
    trajectory.plane_delta_v = sl::dvec3(0.0);
    trajectory.atmosphere_density = nan;
    check(!kosmos::landing_trajectory_predict(trajectory).captured, "trajectory screening rejects non-finite atmosphere");
    trajectory.atmosphere_density = 1.225;
    trajectory.lifting_surfaces = true;
    check(!kosmos::landing_trajectory_predict(trajectory).supported,
          "trajectory screening refuses to ignore atmospheric lifting surfaces");
    trajectory.atmosphere_density = 0.0;
    check(kosmos::landing_trajectory_predict(trajectory).captured,
          "trajectory screening accepts lifting surfaces in a vacuum");
    std::printf("landing plane test: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
