#include "ship.h"
#include "ship_internal.h"

#include <algorithm>
#include <cmath>

namespace kosmos
{
    using namespace ship_detail;

    sl::dvec3 Ship::relative_velocity() const
    {
        const int soi = universe_.soi_body(body().position);
        const Body &planet = universe_.bodies[static_cast<std::size_t>(soi)];
        const sl::FlightBody &pb = *universe_.world.body(soi);
        const sl::dvec3 r = body().position - pb.position;
        return body().velocity - pb.velocity - glm::cross(pb.spin_axis * planet.spin, r);
    }

    void Ship::local_frame(sl::dvec3 &up, sl::dvec3 &east, sl::dvec3 &north) const
    {
        const int soi = universe_.soi_body(body().position);
        up = glm::normalize(body().position - universe_.world.body(soi)->position);
        east = glm::cross(universe_.world.body(soi)->spin_axis, up);
        if (glm::length(east) < 1e-9)
            east = sl::dvec3(1.0, 0.0, 0.0);
        east = glm::normalize(east);
        north = glm::cross(up, east);
    }

    sl::dvec3 Ship::target_direction(const Controls &c, bool &valid) const
    {
        valid = true;
        sl::dvec3 up, east, north;
        local_frame(up, east, north);
        const sl::dvec3 v = relative_velocity();
        const sl::dvec3 r = body().position - universe_.world.body(telemetry_.soi)->position;
        const sl::dvec3 h = glm::cross(r, body().velocity - universe_.world.body(telemetry_.soi)->velocity);
        const std::string &mode = c.sas;
        if (mode == "prograde" || mode == "retrograde")
        {
            if (glm::length(v) < 2.0)
            {
                valid = false;
                return up;
            }
            return mode == "prograde" ? glm::normalize(v) : -glm::normalize(v);
        }
        if (mode == "radial-out")
            return up;
        if (mode == "radial-in")
            return -up;
        if (mode == "normal" || mode == "antinormal")
        {
            if (glm::length(h) < 1e-6)
            {
                valid = false;
                return up;
            }
            return mode == "normal" ? glm::normalize(h) : -glm::normalize(h);
        }
        if (mode == "attitude")
        {
            const double pitch = c.target_pitch * pi / 180.0, heading = c.target_heading * pi / 180.0;
            return up * std::sin(pitch) + (north * std::cos(heading) + east * std::sin(heading)) * std::cos(pitch);
        }
        valid = false;
        return up;
    }

    void Ship::apply_attitude(const Controls &c, double dt)
    {
        if (rcs_dry())
            return;
        // Once the air has hold of a capsule it flies itself shield-first; the thrusters would only fight it.
        if (spec_.hypersonic.enabled && spec_.hypersonic.hold_low_speed && telemetry_.dynamic_pressure > 150.0)
            return;
        sl::FlightBody &b = body();
        const sl::dvec3 w = b.angular_velocity;
        sl::dvec3 desired(0.0);
        bool active = c.sas != "off";
        if (active)
        {
            bool valid = false;
            const sl::dvec3 target = target_direction(c, valid);
            if (valid)
            {
                const sl::dvec3 nose = b.direction_to_world({0.0, 0.0, -1.0});
                const sl::dvec3 axis = glm::cross(nose, target);
                const double s = glm::length(axis);
                const double angle = std::atan2(s, glm::dot(nose, target));
                if (s > 1e-9)
                    desired = b.direction_to_local(axis / s) * std::min(angle * 0.35, spec_.max_rate);
                desired.z = 0.0;
            }
        }
        const sl::dvec3 manual(c.pitch, c.yaw, c.roll);
        sl::dvec3 command(0.0);
        // The torque is held for the whole step, so the gain must stay under one step's worth or a slow frame makes the ship oscillate.
        const double gain = dt > 0.0 && spec_.torque_accel > 0.0 ? std::min(2.0, 0.8 / (spec_.torque_accel * dt)) : 2.0;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::fabs(manual[axis]) > 0.02)
                command[axis] = std::clamp(manual[axis], -1.0, 1.0);
            else if (active)
                command[axis] = std::clamp((desired[axis] - w[axis]) * gain, -1.0, 1.0);
        }
        rcs_torque_ = command;
        if (explicit_rcs_)
            return;
        if (glm::length(command) > 0.0)
            universe_.world.apply_torque(body_, b.direction_to_world(command * spec_.torque_accel * b.inertia()));
    }

    void Ship::apply_flight_controls(const Controls &c)
    {
        sl::FlightBody &b = body();
        const sl::dvec3 w = b.angular_velocity;
        const bool damp = c.sas != "off";
        const double q = std::min(1.0, telemetry_.dynamic_pressure / 1500.0);
        auto command = [&](double stick, double rate, double gain)
        {
            return std::clamp(stick - (damp ? gain * rate * q : 0.0), -1.0, 1.0);
        };
        const double af = c.af_mode == "off" ? 0.0 : 1.0, af_lateral = c.af_mode == "on" ? 1.0 : 0.0;
        // Surface force grows with dynamic pressure, so a full key press is violent in dense, fast air. Authority falls as
        // 1/sqrt(q) beyond about 100 m/s at sea level, which keeps some control feel while taming the twitch; slow flight is untouched.
        const double authority = std::clamp(std::sqrt(6000.0 / std::max(telemetry_.dynamic_pressure, 1.0)), 0.2, 1.0);
        universe_.world.set_control(body_, "elevator", af * command((c.pitch + 2.5 * c.trim) * authority, w.x, spec_.damping.x));
        universe_.world.set_control(body_, "rudder", af_lateral * command(c.yaw * authority, w.y, spec_.damping.y));
        universe_.world.set_control(body_, "aileron", af_lateral * command(c.roll * authority, w.z, spec_.damping.z));
        universe_.world.set_control(body_, "flaps", std::clamp(c.flaps, 0.0, 1.0));

        // Nose-wheel steering follows the yaw input; the wheels roll along their own heading.
        const std::size_t first = b.gear.size() - spec_.wheels.size();
        for (std::size_t i = 0; i < spec_.wheels.size(); ++i)
        {
            const double angle = spec_.wheels[i].steer * std::clamp(c.yaw, -1.0, 1.0);
            b.gear[first + i].forward = {-std::sin(angle), 0.0, -std::cos(angle)};
        }
    }

    void Ship::apply_ground_controls(const Controls &c)
    {
        sl::FlightBody &b = body();
        const sl::FlightBody *planet = telemetry_.soi >= 0 ? universe_.world.body(telemetry_.soi) : nullptr;
        double forward_speed = 0.0;
        if (planet)
        {
            const sl::dvec3 ground_velocity = planet->velocity + glm::cross(planet->spin_axis * planet->spin_rate, b.position - planet->position);
            forward_speed = glm::dot(b.velocity - ground_velocity, b.direction_to_world({0.0, 0.0, -1.0}));
        }
        const double throttle = std::clamp(c.throttle, -1.0, 1.0);
        int driven = 0;
        for (const WheelSpec &w : spec_.wheels)
            driven += w.drive ? 1 : 0;
        // The drive fades as the vehicle nears its top speed in the direction it is being driven.
        const double taper = std::clamp(1.0 - forward_speed * (throttle < 0.0 ? -1.0 : 1.0) / std::max(spec_.ground_max_speed, 0.1), 0.0, 1.0);
        const double per_wheel = driven > 0 ? throttle * spec_.ground_force * taper / driven : 0.0;
        // Steering is eased off with speed so the vehicle does not roll over in a turn.
        const double lock = std::clamp(1.0 - 0.6 * std::fabs(forward_speed) / std::max(spec_.ground_max_speed, 0.1), 0.4, 1.0);
        const std::size_t first = b.gear.size() - spec_.wheels.size();
        for (std::size_t i = 0; i < spec_.wheels.size(); ++i)
        {
            const double angle = spec_.wheels[i].steer * lock * std::clamp(c.yaw, -1.0, 1.0);
            b.gear[first + i].forward = {-std::sin(angle), 0.0, -std::cos(angle)};
            b.gear[first + i].drive_force = spec_.wheels[i].drive ? per_wheel : 0.0;
        }
        for (sl::Thruster &t : b.thrusters)
            t.level = 0.0;
        // Parked on its brakes when idle and nearly still; otherwise only the pedal brakes.
        const bool parked = std::fabs(throttle) < 0.02 && std::fabs(forward_speed) < 1.0;
        universe_.world.set_brakes(body_, std::max(c.brake, parked ? 1.0 : 0.0));
    }

    void Ship::apply_controls(const Controls &input, double dt)
    {
        Controls c = input;
        rcs_torque_ = rcs_push_ = sl::dvec3(0.0);
        if (crashed_ || health_ <= 0.0)
            return;
        apply_rcs_mode(c);
        animate_mechanisms(c, dt);
        if (c.parachute > 0)
            deploy_parachute();
        else if (c.parachute < 0)
            cut_parachutes();
        for (const ChuteRuntime &chute : chutes_)
            if (chute.state != ChuteState::stowed && chute.state != ChuteState::gone)
                c.sas = "off";
        if (spec_.ground())
        {
            apply_ground_controls(c);
            return;
        }
        if (c.stage)
            stage_once();
        sl::FlightBody &b = body();
        set_thruster_levels(c);
        if (flies_as_aircraft())
        {
            // The control surfaces answer the stick whatever the RCS switch says.
            Controls surfaces = c;
            surfaces.pitch = input.pitch;
            surfaces.yaw = input.yaw;
            surfaces.roll = input.roll;
            apply_flight_controls(surfaces);
            hold_hover_attitude(c);
            // High in the air the surfaces have little bite, so the thrusters take over as in space.
            if (b.mode == sl::FlightBodyMode::dynamic && telemetry_.dynamic_pressure < 500.0 && c.hover <= 0.02)
            {
                apply_attitude(c, dt);
                apply_rcs(c);
                use_rcs_propellant(dt);
            }
            return;
        }
        if (b.mode == sl::FlightBodyMode::dynamic)
        {
            apply_attitude(c, dt);
            apply_rcs(c);
            use_rcs_propellant(dt);
        }
    }

    double Ship::engine_value(const std::string &role, const std::string &what) const
    {
        const sl::FlightBody &b = body();
        if (b.thrusters.empty() || stage_first_thruster_.size() < static_cast<std::size_t>(active_stage_) + 2)
            return 0.0;
        double sum = 0.0, level = 0.0;
        int count = 0;
        for (int i = stage_first_thruster_[static_cast<std::size_t>(active_stage_)]; i < stage_first_thruster_[static_cast<std::size_t>(active_stage_) + 1]; ++i)
        {
            const sl::Thruster &t = b.thrusters[static_cast<std::size_t>(i)];
            const std::size_t k = static_cast<std::size_t>(i);
            if (thruster_rcs_[k])
                continue;
            std::string mine;
            if (thruster_pod_[k] >= 0)
                mine = t.position.x < 0.0 ? "pod-left" : "pod-right";
            else if (thruster_hover_[k])
                mine = t.position.z < 0.0 ? "hover-front" : "hover-aft";
            else
                mine = t.position.x < 0.0 ? "main-left" : "main-right";
            if (mine != role)
                continue;
            const bool fed = t.tank < 0 || b.tanks[static_cast<std::size_t>(t.tank)].mass > 0.0;
            const double lvl = fed ? t.level : 0.0;
            level += lvl;
            ++count;
            sum += (what == "thrust" || what == "accel") ? lvl * t.max_thrust : t.isp > 0.0 ? lvl * t.max_thrust / (t.isp * g0) : 0.0;
        }
        if (what == "accel")
            return b.mass() > 0.0 ? sum / b.mass() : 0.0;
        return what == "level" ? (count ? level / count : 0.0) : sum;
    }

    bool Ship::rcs_dry() const
    {
        return spec_.rcs_propellant > 0.0 && rcs_fuel_ <= 0.0;
    }

    void Ship::use_rcs_propellant(double dt)
    {
        rcs_flow_ = 0.0;
        if (spec_.rcs_propellant <= 0.0 || dt <= 0.0)
            return;
        const double activity = std::min(1.0, 0.3 * (std::fabs(rcs_torque_.x) + std::fabs(rcs_torque_.y) + std::fabs(rcs_torque_.z))) + glm::length(rcs_push_);
        rcs_flow_ = activity * std::max(spec_.rcs_thrust, 1000.0) / (spec_.rcs_isp * g0);
        rcs_fuel_ = std::max(0.0, rcs_fuel_ - rcs_flow_ * dt);
    }

    void Ship::apply_rcs_mode(Controls &c) const
    {
        const double reverse = spec_.reverse_throttle && c.throttle < 0.0 ? c.throttle : 0.0;
        if (spec_.rcs_modes)
        {
            if (c.rcs_mode == "off")
            {
                c.pitch = c.yaw = c.roll = c.rcs_x = c.rcs_y = c.rcs_z = 0.0;
                c.sas = "off";
            }
            else if (c.rcs_mode == "lin")
            {
                // The rotation keys push instead: W/S forward and back, A/D left and right, Q/E up and down.
                c.rcs_z = std::clamp(c.rcs_z - c.pitch, -1.0, 1.0);
                c.rcs_x = std::clamp(c.rcs_x - c.yaw, -1.0, 1.0);
                c.rcs_y = std::clamp(c.rcs_y + c.roll, -1.0, 1.0);
                c.pitch = c.yaw = c.roll = 0.0;
            }
        }
        if (reverse < 0.0)
            c.rcs_z = std::clamp(c.rcs_z + reverse, -1.0, 1.0);
    }

    void Ship::animate_mechanisms(const Controls &c, double dt)
    {
        for (int side = 0; side < 2; ++side)
        {
            const double target = std::clamp(side == 0 ? c.pod_tilt_left : c.pod_tilt_right, 0.0, 1.0) * 3.14159265358979;
            const double step = spec_.pod_turn_rate * dt;
            pod_angle_[side] += std::clamp(target - pod_angle_[side], -step, step);
        }
        if (anim_pos_.size() != spec_.animation_controls.size())
        {
            anim_pos_.clear();
            for (const ShipSpec::AnimationControl &a : spec_.animation_controls)
                anim_pos_.push_back(a.initial);
        }
        for (std::size_t i = 0; i < anim_pos_.size(); ++i)
        {
            const double target = i < c.anim.size() ? std::clamp(c.anim[i], 0.0, 1.0) : anim_pos_[i];
            const double step = dt / spec_.animation_controls[i].duration;
            anim_pos_[i] = std::clamp(anim_pos_[i] + std::clamp(target - anim_pos_[i], -step, step), 0.0, 1.0);
        }
        sl::FlightBody &b = body();
        if (spec_.retractable_gear)
        {
            const double target = c.gear > 0.5 ? 1.0 : 0.0;
            const double step = dt / spec_.gear_time;
            gear_position_ = std::clamp(gear_position_ + std::clamp(target - gear_position_, -step, step), 0.0, 1.0);
            const bool locked = gear_position_ >= 0.999;
            for (sl::GearContact &g : b.gear)
                g.enabled = locked;
        }
    }

    void Ship::set_thruster_levels(const Controls &c)
    {
        sl::FlightBody &b = body();
        const double throttle = std::clamp(c.throttle, 0.0, 1.0);
        for (std::size_t i = 0; i < b.thrusters.size(); ++i)
            b.thrusters[i].level = 0.0;
        for (int i = stage_first_thruster_[static_cast<std::size_t>(active_stage_)];
             i < stage_first_thruster_[static_cast<std::size_t>(active_stage_) + 1]; ++i)
        {
            const int pod = thruster_pod_[static_cast<std::size_t>(i)];
            sl::Thruster &t = b.thrusters[static_cast<std::size_t>(i)];
            if (pod >= 0)
            {
                const double a = pod_angle_[pod];
                t.direction = {0.0, std::sin(a), std::cos(a)};
                t.level = std::clamp(pod == 0 ? c.pod_left : c.pod_right, 0.0, 1.0);
            }
            else
            {
                const bool main_engine = !thruster_rcs_[static_cast<std::size_t>(i)] && !thruster_hover_[static_cast<std::size_t>(i)];
                t.level = thruster_rcs_[static_cast<std::size_t>(i)] ? 0.0 : thruster_hover_[static_cast<std::size_t>(i)] ? std::clamp(c.hover, 0.0, 1.0) : throttle;
                const auto &engines = spec_.stages[static_cast<std::size_t>(active_stage_)].engines;
                const std::size_t local = static_cast<std::size_t>(i - stage_first_thruster_[static_cast<std::size_t>(active_stage_)]);
                if (main_engine && local < engines.size())
                {
                    // The gimbal swings the thrust line about the ship's X (pitch) and Y (yaw) axes.
                    const double range = glm::radians(spec_.gimbal_range);
                    const sl::dquat swing = glm::angleAxis(std::clamp(c.gimbal_yaw, -1.0, 1.0) * range, sl::dvec3(0.0, 1.0, 0.0)) * glm::angleAxis(std::clamp(c.gimbal_pitch, -1.0, 1.0) * range, sl::dvec3(1.0, 0.0, 0.0));
                    t.direction = glm::normalize(swing * engines[local].direction);
                }
            }
        }
        if (flies_as_aircraft())
        {
            // A parked aircraft holds its brakes; once rolling, only the pedal brakes.
            const bool parked = throttle < 0.02 && telemetry_.landed && telemetry_.surface_speed < 2.0;
            universe_.world.set_brakes(body_, std::max(c.brake, parked ? 1.0 : 0.0));
        }
        else
            universe_.world.set_brakes(body_, throttle < 0.02 ? 1.0 : 0.0);
    }

    // On hover thrust there is no airflow over the surfaces, so the RCS thrusters hold attitude.
    void Ship::hold_hover_attitude(const Controls &c)
    {
        sl::FlightBody &b = body();
        if (c.hover > 0.02 && b.mode == sl::FlightBodyMode::dynamic && c.sas != "off")
        {
            sl::dvec3 up, east, north;
            local_frame(up, east, north);
            const sl::dvec3 ub = b.direction_to_local(up);
            const sl::dvec3 w = b.angular_velocity;
            const sl::dvec3 desired(std::clamp(ub.z * 1.5, -0.6, 0.6), 0.0, std::clamp(-ub.x * 1.5, -0.6, 0.6));
            sl::dvec3 command(0.0);
            command.x = std::clamp((desired.x - w.x) * 3.0, -1.0, 1.0);
            command.z = std::clamp((desired.z - w.z) * 3.0, -1.0, 1.0);
            command.y = std::clamp(-w.y * 3.0, -1.0, 1.0);
            rcs_torque_ = command;
            universe_.world.apply_torque(body_, b.direction_to_world(command * spec_.torque_accel * b.inertia()));
        }
    }

    void Ship::apply_rcs(const Controls &c)
    {
        if (rcs_dry())
            return;
        sl::FlightBody &b = body();
        rcs_push_ = sl::dvec3(0.0);
        if (explicit_rcs_)
        {
            // The thrusters themselves make the force and torque; each fires in proportion to its strongest group.
            const double lin[6] = {-c.rcs_x, c.rcs_x, c.rcs_y, -c.rcs_y, c.rcs_z, -c.rcs_z};
            const double rot[6] = {rcs_torque_.x, -rcs_torque_.x, rcs_torque_.y, -rcs_torque_.y, rcs_torque_.z, -rcs_torque_.z};
            for (int i = stage_first_thruster_[static_cast<std::size_t>(active_stage_)];
                 i < stage_first_thruster_[static_cast<std::size_t>(active_stage_) + 1]; ++i)
            {
                const unsigned mask = thruster_rcs_[static_cast<std::size_t>(i)];
                double level = 0.0;
                for (int k = 0; k < 6; ++k)
                {
                    if (mask & (1u << k))
                        level = std::max(level, lin[k]);
                    if (mask & (1u << (k + 6)))
                        level = std::max(level, rot[k]);
                }
                b.thrusters[static_cast<std::size_t>(i)].level = std::clamp(level, 0.0, 1.0);
            }
            rcs_torque_ = sl::dvec3(0.0);
        }
        else if (spec_.rcs_thrust > 0.0)
        {
            const sl::dvec3 push(c.rcs_x, c.rcs_y, -c.rcs_z);
            rcs_push_ = glm::length(push) > 0.0 ? glm::normalize(push) : push;
            if (glm::length(push) > 0.0)
                universe_.world.apply_force(body_, b.direction_to_world(glm::normalize(push) * spec_.rcs_thrust));
        }
    }
} // namespace kosmos
