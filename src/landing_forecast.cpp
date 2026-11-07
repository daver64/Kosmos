#include "landing_forecast.h"

#include "base.h"
#include "landing_descent.h"
#include "ship.h"
#include "universe.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace kosmos
{
    struct LandingForecastStepper::Impl
    {
        LandingForecastResult result;
        Universe &universe;
        Ship &ship;
        Base pad;
        int body_id;
        bool shuttle, through_touchdown;
        sl::dvec3 axis, direction;
        double dv, duration, thrust, reserve;
        double elapsed = 0.0, delivered = 0.0, start_time = 0.0;
        int descent_steps = 0;
        enum class Phase { burn, idle, descent, approach, done } phase = Phase::burn;
        static constexpr double dt = 0.1;

        Impl(Universe &world, Ship &vessel, const Base &base, int body, bool is_shuttle,
             const sl::dvec3 &thrust_axis, const sl::dvec3 &burn_direction, double delta_v,
             double burn_duration, double max_thrust, double fuel_reserve, bool touchdown)
            : universe(world), ship(vessel), pad(base), body_id(body), shuttle(is_shuttle),
              through_touchdown(touchdown), axis(thrust_axis), direction(burn_direction), dv(delta_v), duration(burn_duration),
              thrust(max_thrust), reserve(fuel_reserve)
        {
            const auto finite = [](const sl::dvec3 &v) {
                return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
            };
            if (body_id < 0 || static_cast<std::size_t>(body_id) >= universe.bodies.size() ||
                !universe.world.body(body_id) || !finite(axis) || !finite(direction) ||
                std::abs(glm::length(axis) - 1.0) > 1e-6 || std::abs(glm::length(direction) - 1.0) > 1e-6 ||
                !std::isfinite(dv) || dv < 0.0 || !std::isfinite(duration) || duration < 0.0 || duration > 240.0 ||
                !std::isfinite(thrust) || thrust <= 0.0 || !std::isfinite(reserve) || reserve < 0.0 ||
                !std::isfinite(ship.telemetry().mass) || ship.telemetry().mass <= 0.0)
                phase = Phase::done;
            else
                universe.world.max_substep = dt;
        }

        void fail(LandingForecastStatus status)
        {
            result.status = status;
            phase = Phase::done;
        }

        void physics(const sl::dvec3 &wanted, double level) {
            Controls controls;
            controls.sas = "attitude";
            controls.hover = level;
            controls.gear = 1.0;
            if (shuttle)
            {
                controls.landing_attitude = true;
                controls.landing_axis = axis;
                controls.landing_direction = wanted;
            }
            else
            {
                const sl::FlightBody &primary = *universe.world.body(body_id);
                const sl::dvec3 up = glm::normalize(ship.body().position - primary.position);
                sl::dvec3 east = glm::cross(primary.spin_axis, up);
                if (glm::length(east) < 1e-8)
                    east = glm::cross(sl::dvec3(1, 0, 0), up);
                east = glm::normalize(east);
                const sl::dvec3 north = glm::cross(up, east);
                controls.target_pitch = glm::degrees(std::asin(std::clamp(glm::dot(wanted, up), -1.0, 1.0)));
                controls.target_heading = std::fmod(glm::degrees(std::atan2(glm::dot(wanted, east),
                                                                           glm::dot(wanted, north))) + 360.0, 360.0);
            }
            ship.apply_controls(controls, dt);
            universe.world.step(dt);
            ship.after_step(dt);
        }

        void step()
        {
            if (phase == Phase::done)
                return;
            if (phase == Phase::burn)
            {
                if (elapsed >= 40.0 + duration + dt)
                {
                    if (delivered + 0.1 < dv)
                        fail(LandingForecastStatus::insufficient_fuel);
                    else
                        phase = Phase::idle;
                    return;
                }
                if (ship.crashed() || ship.telemetry().soi != body_id || !ship.attitude_control_available())
                    return fail(LandingForecastStatus::vessel_failure);
                if (!std::isfinite(ship.telemetry().stage_fuel) || ship.telemetry().stage_fuel <= reserve)
                    return fail(LandingForecastStatus::insufficient_fuel);
                double level = 0.0;
                if (elapsed >= 40.0 && delivered < dv)
                {
                    if (glm::dot(ship.body().direction_to_world(axis), direction) < 0.995)
                        return fail(LandingForecastStatus::attitude_failure);
                    level = std::min(1.0, (dv - delivered) * ship.telemetry().mass / (thrust * dt));
                    delivered += thrust * level / ship.telemetry().mass * dt;
                }
                physics(direction, level);
                elapsed += dt;
                return;
            }
            if (phase == Phase::idle)
            {
                if (shuttle)
                {
                    Controls idle;
                    ship.apply_controls(idle, dt);
                    universe.world.step(dt);
                    ship.after_step(dt);
                }
                else
                    physics(direction, 0.0);
                start_time = universe.time();
                phase = Phase::descent;
                return;
            }
            if (descent_steps++ >= (phase == Phase::approach ? 28000 : 18000))
                return fail(LandingForecastStatus::timeout);
            const Body &body = universe.bodies[static_cast<std::size_t>(body_id)];
            const sl::FlightBody &primary = *universe.world.body(body_id);
            const sl::dvec3 r = ship.body().position - primary.position;
            const sl::dvec3 v = ship.body().velocity - primary.velocity;
            const double radius = glm::length(r);
            if (!std::isfinite(radius) || radius <= 0.0)
                return fail(LandingForecastStatus::invalid_guidance);
            const sl::dvec3 up = r / radius;
            pad.direction = base_direction(universe, body_id, pad);
            pad.pole = universe.pole(body_id);
            sl::dvec3 ground;
            double heading;
            base_pad(pad, body.radius, ground, heading);
            const double distance = radius * std::acos(std::clamp(glm::dot(up, ground), -1.0, 1.0));
            const Telemetry &t = ship.telemetry();
            if (ship.crashed() || t.soi != body_id || !ship.attitude_control_available())
                return fail(LandingForecastStatus::vessel_failure);
            if (!std::isfinite(t.stage_fuel) || t.stage_fuel <= reserve)
                return fail(LandingForecastStatus::insufficient_fuel);
            if (phase == Phase::approach && t.landed && t.surface_speed < 2.0)
            {
                result.position = ship.body().position;
                result.velocity = ship.body().velocity;
                result.fuel = t.stage_fuel;
                result.distance = radius * std::acos(std::clamp(glm::dot(up, pad.direction), -1.0, 1.0));
                result.surface_speed = t.surface_speed;
                return fail(result.distance <= 3000.0 ?
                            LandingForecastStatus::landed : LandingForecastStatus::missed_target);
            }
            const sl::dvec3 surface_v = ship.relative_velocity();
            const double hspeed = glm::length(surface_v - up * glm::dot(surface_v, up));
            if (phase == Phase::descent && distance < 1500.0 && t.altitude > 300.0 && t.altitude < 3000.0 &&
                hspeed < 40.0 && std::abs(t.vertical_speed) < 25.0)
            {
                result.captured = true;
                if (!through_touchdown)
                {
                    result.position = ship.body().position;
                    result.velocity = ship.body().velocity;
                    result.fuel = t.stage_fuel;
                    return fail(LandingForecastStatus::captured);
                }
                phase = Phase::approach;
            }
            if (phase == Phase::approach)
            {
                const double gravity = body.gm / (radius * radius);
                const auto command = landing_approach_command(r, surface_v, ground, t.altitude, t.vertical_speed,
                                                              gravity, thrust / t.mass);
                if (!command.valid)
                    return fail(LandingForecastStatus::invalid_guidance);
                const double level = glm::dot(ship.body().direction_to_world(axis), command.direction) > 0.98 ?
                                     command.level : 0.0;
                physics(command.direction, level);
                return;
            }
            const auto command = landing_descent_command(r, v, ground, pad.pole, body.spin, body.gm,
                                                         body.radius + pad.height + pad.pad_deck + 2500.0,
                                                         std::max(650.0 - (universe.time() - start_time), 60.0));
            const double magnitude = glm::length(command.acceleration);
            if (!command.valid || !std::isfinite(magnitude) || magnitude < 1e-8)
                return fail(LandingForecastStatus::invalid_guidance);
            const sl::dvec3 desired = command.acceleration / magnitude;
            const double level = glm::dot(ship.body().direction_to_world(axis), desired) > 0.98 ?
                std::clamp(magnitude * t.mass / thrust, 0.0, 1.0) : 0.0;
            physics(desired, level);
        }
    };

    LandingForecastStepper::LandingForecastStepper(Universe &universe, Ship &ship, const Base &pad, int body_id,
                                                   bool shuttle, const sl::dvec3 &axis, const sl::dvec3 &direction,
                                                   double dv, double duration, double thrust, double reserve,
                                                   bool through_touchdown)
        : impl_(std::make_unique<Impl>(universe, ship, pad, body_id, shuttle, axis, direction, dv, duration, thrust,
                                      reserve, through_touchdown))
    {
    }

    LandingForecastStepper::~LandingForecastStepper() = default;

    bool LandingForecastStepper::advance(int max_steps)
    {
        for (int i = 0; i < max_steps && impl_->phase != Impl::Phase::done; ++i)
            impl_->step();
        return impl_->phase == Impl::Phase::done;
    }

    const LandingForecastResult &LandingForecastStepper::result() const { return impl_->result; }

    LandingForecastResult forecast_landing(Universe &universe, Ship &ship, Base pad, int body_id,
                                           bool shuttle, const sl::dvec3 &axis,
                                           const sl::dvec3 &direction, double dv, double duration, double thrust,
                                           double reserve)
    {
        LandingForecastStepper forecast(universe, ship, pad, body_id, shuttle, axis, direction, dv, duration, thrust, reserve);
        while (!forecast.advance(512))
        {
        }
        return forecast.result();
    }

    struct LandingCandidateForecast::Impl
    {
        LandingForecastResult result;
        std::vector<std::unique_ptr<sl::Terrain>> terrain;
        std::unique_ptr<Universe> copy;
        std::unique_ptr<Ship> forecast;
        std::unique_ptr<LandingForecastStepper> stepper;

        Impl(const Universe &source, const Ship &ship, const Base &pad, int body_id, bool shuttle,
             const sl::dvec3 &axis, const LandingCandidate &candidate, double thrust, double reserve, bool through_touchdown)
        {
            const double dv = glm::length(candidate.window.change.delta_v);
            const double coast = candidate.ignition_wait - 40.0;
            if (!candidate.window.valid || !candidate.window.change.valid || !candidate.burn.valid ||
                !candidate.burn.sufficient_fuel || !std::isfinite(dv) ||
                !std::isfinite(coast) || coast < 0.0 || body_id < 0 ||
                static_cast<std::size_t>(body_id) >= source.bodies.size() || ship.telemetry().soi != body_id ||
                ship.crashed())
                return;
            if (source.world.body_count() >= 4096)
            {
                result.status = LandingForecastStatus::unavailable_world;
                return;
            }
            for (int i = 0; i < source.world.body_count(); ++i)
            {
                const sl::FlightBody *body = source.world.body(i);
                if (body && body->active && body->wind_field)
                {
                    result.status = LandingForecastStatus::unavailable_wind;
                    return;
                }
            }
            copy = std::make_unique<Universe>(source);
            auto isolate_terrain = [&](const sl::Terrain *original) {
                sl::TerrainConfig config = original->config();
                config.worker_threads = 0;
                terrain.push_back(std::make_unique<sl::Terrain>(config, original->source()));
                return terrain.back().get();
            };
            if (copy->world.flat_terrain)
                copy->world.flat_terrain = isolate_terrain(copy->world.flat_terrain);
            for (int i = 0; i < copy->world.body_count(); ++i)
            {
                sl::FlightBody *body = copy->world.body(i);
                if (!body || !body->active)
                    continue;
                if (static_cast<std::size_t>(i) >= copy->bodies.size())
                {
                    if (i != ship.body_id() && body->gm > 0.0)
                    {
                        result.status = LandingForecastStatus::unavailable_world;
                        return;
                    }
                    copy->world.destroy_body(i);
                    continue;
                }
                if (body->terrain)
                    body->terrain = isolate_terrain(body->terrain);
            }
            const sl::FlightBody state = ship.body();
            forecast = std::make_unique<Ship>(*copy, ship.spec());
            forecast->restore_carry_state(ship.carry_state());
            forecast->body() = state;
            forecast->after_step(0.0);
            if (!forecast->go_rails())
            {
                result.status = LandingForecastStatus::unavailable_world;
                return;
            }
            copy->world.max_substep = 1.0e12;
            copy->world.step(coast);
            forecast->go_dynamic();
            forecast->after_step(0.0);
            if (forecast->telemetry().soi != body_id)
            {
                result.status = LandingForecastStatus::vessel_failure;
                return;
            }
            const sl::dvec3 velocity = candidate.window.departure_velocity;
            const double speed = glm::length(velocity);
            if (dv < 1e-8 && (!std::isfinite(speed) || speed < 1e-8))
                return;
            const sl::dvec3 direction = dv < 1e-8 ? -velocity / speed : candidate.window.change.delta_v / dv;
            stepper = std::make_unique<LandingForecastStepper>(*copy, *forecast, pad, body_id, shuttle, axis, direction,
                                                               dv, candidate.burn.duration, thrust, reserve, through_touchdown);
        }
    };

    LandingCandidateForecast::LandingCandidateForecast(const Universe &universe, const Ship &ship, const Base &pad,
                                                       int body_id, bool shuttle, const sl::dvec3 &axis,
                                                       const LandingCandidate &candidate, double thrust, double reserve,
                                                       bool through_touchdown)
        : impl_(std::make_unique<Impl>(universe, ship, pad, body_id, shuttle, axis, candidate, thrust, reserve,
                                      through_touchdown))
    {
    }

    LandingCandidateForecast::~LandingCandidateForecast() = default;

    bool LandingCandidateForecast::advance(int max_steps)
    {
        return !impl_->stepper || impl_->stepper->advance(max_steps);
    }

    const LandingForecastResult &LandingCandidateForecast::result() const
    {
        return impl_->stepper ? impl_->stepper->result() : impl_->result;
    }

    LandingForecastResult forecast_landing_candidate(const Universe &source, const Ship &ship, Base pad,
                                                     int body_id, bool shuttle, const sl::dvec3 &axis,
                                                     const LandingCandidate &candidate, double thrust, double reserve,
                                                     bool through_touchdown)
    {
        LandingCandidateForecast forecast(source, ship, pad, body_id, shuttle, axis, candidate, thrust, reserve,
                                          through_touchdown);
        while (!forecast.advance(512))
        {
        }
        return forecast.result();
    }
}
