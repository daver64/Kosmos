#include "ship.h"

#include "ship_internal.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <fstream>

namespace kosmos
{
    using namespace ship_detail;

    namespace
    {
        // How much water ice (mined from regolith) and nitrogen air a body offers, 0..1. Known worlds are listed; other bodies are
        // guessed from their density and distance from the star: icy bodies are wet, cold rocky ones hold some ice, hot rock is dry.
        void surface_richness(const Universe &u, int index, double &water, double &nitrogen)
        {
            struct Known { const char *name; double water, nitrogen; };
            static const Known known[] = {
                {"Earth", 1.0, 0.78}, {"Moon", 0.15, 0.0}, {"Mars", 0.45, 0.03}, {"Mercury", 0.05, 0.0}, {"Venus", 0.0, 0.03},
                {"Phobos", 0.1, 0.0}, {"Deimos", 0.1, 0.0}, {"Ceres", 0.8, 0.0}, {"Vesta", 0.05, 0.0},
                {"Io", 0.0, 0.0}, {"Europa", 1.0, 0.0}, {"Ganymede", 0.9, 0.0}, {"Callisto", 0.9, 0.0},
                {"Mimas", 1.0, 0.0}, {"Enceladus", 1.0, 0.0}, {"Tethys", 1.0, 0.0}, {"Dione", 1.0, 0.0}, {"Rhea", 1.0, 0.0},
                {"Titan", 0.9, 0.95}, {"Iapetus", 0.9, 0.0}, {"Miranda", 0.8, 0.0}, {"Ariel", 0.8, 0.0}, {"Umbriel", 0.7, 0.0},
                {"Titania", 0.8, 0.0}, {"Oberon", 0.8, 0.0}, {"Triton", 0.9, 0.1}, {"Pluto", 0.7, 0.1}, {"Charon", 0.8, 0.0}};
            const Body &b = u.bodies[static_cast<std::size_t>(index)];
            for (const Known &k : known)
                if (b.name == k.name)
                {
                    water = k.water;
                    nitrogen = k.nitrogen;
                    return;
                }
            const double density = b.radius > 0.0 ? b.gm / 6.674e-11 / (4.0 / 3.0 * 3.14159265358979 * b.radius * b.radius * b.radius) : 3000.0;
            double au = 1.0;
            for (std::size_t i = 0; i < u.bodies.size(); ++i)
                if (u.bodies[i].kind == BodyKind::star)
                {
                    au = std::max(glm::length(u.position(index) - u.position(static_cast<int>(i))) / 149597870700.0, 0.05);
                    break;
                }
            water = density < 2600.0 ? 0.8 : au > 2.5 ? 0.4 : au > 1.2 ? 0.15 : 0.03;
            nitrogen = 0.0;
        }
    } // namespace

    Ship::Ship(Universe &universe, ShipSpec spec) : name(spec.name), universe_(universe), spec_(std::move(spec))
    {
        build_body();
        rcs_fuel_ = spec_.rcs_propellant;
        shield_ = spec_.shield.capacity;
        if (!spec_.systems.empty())
            systems_ = std::make_unique<Systems>(spec_.systems, life_support_);
        instrument_state_.resize(spec_.instruments.size());
        for (std::size_t i = 0; i < instrument_state_.size(); ++i)
            instrument_state_[i].frames = spec_.instruments[i].frames;
    }

    namespace
    {
        // The unit direction `forward` turned by `az` (to the right) and `el` (up), with the ship's +Y as the reference for "up".
        sl::dvec3 pointed(const sl::dvec3 &forward, double az, double el)
        {
            sl::dvec3 hint(0.0, 1.0, 0.0);
            if (std::abs(glm::dot(forward, hint)) > 0.95)
                hint = sl::dvec3(1.0, 0.0, 0.0);
            const sl::dvec3 right = glm::normalize(glm::cross(hint, forward));
            const sl::dvec3 up = glm::cross(forward, right);
            return glm::normalize(forward * (std::cos(az) * std::cos(el)) + right * (std::sin(az) * std::cos(el)) + up * std::sin(el));
        }
    } // namespace

    sl::dvec3 Ship::instrument_direction(std::size_t i) const
    {
        if (i >= spec_.instruments.size())
            return body().direction_to_world(sl::dvec3(0.0, 0.0, 1.0));
        const InstrumentState &st = instrument_state_[i];
        return body().direction_to_world(pointed(spec_.instruments[i].direction, st.az, st.el));
    }

    sl::dvec3 Ship::instrument_position(std::size_t i) const
    {
        const sl::dvec3 local = i < spec_.instruments.size() ? spec_.instruments[i].position : sl::dvec3(0.0);
        return origin() + body().direction_to_world(local);
    }

    void Ship::instrument_shoot(std::size_t i)
    {
        if (i >= instrument_state_.size())
            return;
        InstrumentState &st = instrument_state_[i];
        if (st.frames > 0 && st.pending == 0)
            st.pending = 1;
    }

    void Ship::step_instruments(double dt, const sl::dvec3 &sun_local, bool sunlit)
    {
        for (std::size_t i = 0; i < instrument_state_.size(); ++i)
        {
            const ShipSpec::Instrument &in = spec_.instruments[i];
            InstrumentState &st = instrument_state_[i];
            st.powered = true;
            if (!in.load.empty() && systems_)
            {
                st.powered = false;
                for (const SystemComponent &c : systems_->components())
                    if (c.name == in.load)
                    {
                        st.powered = c.enabled && c.powered && c.wear < 1.0;
                        break;
                    }
            }
            if (!st.powered)
                st.pending = 0;
            const double limit = glm::radians(in.max_slew);
            st.target_az = std::clamp(st.target_az, -limit, limit);
            st.target_el = std::clamp(st.target_el, -limit, limit);
            if (st.powered && dt > 0.0)
            {
                const double step = glm::radians(in.slew) * dt;
                st.az += std::clamp(st.target_az - st.az, -step, step);
                st.el += std::clamp(st.target_el - st.el, -step, step);
            }
            const double c = std::clamp(glm::dot(pointed(in.direction, st.az, st.el), sun_local), -1.0, 1.0);
            st.sun_angle = glm::degrees(std::acos(c));
            st.shutter = in.type != "radar" && sunlit && st.sun_angle < in.sun_limit;
            if (st.shutter)
                st.pending = 0;
        }
    }

    void Ship::set_life_support(LifeSupportMode mode)
    {
        life_support_ = mode;
        if (systems_)
            systems_->set_mode(mode);
    }

    Ship::~Ship()
    {
        unpublish();
        for (const Debris &d : debris_)
            universe_.world.destroy_body(d.body);
        universe_.world.destroy_body(body_);
    }

    sl::FlightBody &Ship::body() { return *universe_.world.body(body_); }
    const sl::FlightBody &Ship::body() const { return *universe_.world.body(body_); }

    ShipCarryState Ship::carry_state() const
    {
        ShipCarryState state;
        state.active_stage = active_stage_;
        state.stage_propellant.reserve(body().tanks.size());
        for (const auto &tank : body().tanks)
            state.stage_propellant.push_back(tank.mass);
        state.rcs_propellant = rcs_fuel_;
        state.gear_position = gear_position_;
        for (std::size_t i = 0; i < spec_.animation_controls.size(); ++i)
            state.animation_positions.push_back(animation_position(i));
        state.health = health_;
        state.shield = shield_;
        if (systems_)
        {
            state.systems = systems_->state();
            state.has_systems = true;
        }
        return state;
    }

    void Ship::restore_carry_state(const ShipCarryState &state)
    {
        sl::FlightBody &b = body();
        const int target_stage = std::clamp(state.active_stage, 0, std::max(0, stage_count() - 1));
        for (int stage = 0; stage < target_stage; ++stage)
        {
            for (int i = stage_first_thruster_[static_cast<std::size_t>(stage)]; i < stage_first_thruster_[static_cast<std::size_t>(stage) + 1]; ++i)
            {
                b.thrusters[static_cast<std::size_t>(i)].max_thrust = 0.0;
                b.thrusters[static_cast<std::size_t>(i)].level = 0.0;
            }
            if (static_cast<std::size_t>(stage) < b.tanks.size())
                b.tanks[static_cast<std::size_t>(stage)] = {0.0, 0.0};
            if (systems_)
                systems_->drop_stage(stage);
            const std::size_t gear_count = spec_.stages[static_cast<std::size_t>(stage)].gear.size();
            b.gear.erase(b.gear.begin(), b.gear.begin() + static_cast<std::ptrdiff_t>(std::min(gear_count, b.gear.size())));
        }
        active_stage_ = target_stage;
        for (std::size_t stage = static_cast<std::size_t>(target_stage); stage < b.tanks.size() && stage < state.stage_propellant.size(); ++stage)
            b.tanks[stage].mass = std::clamp(state.stage_propellant[stage], 0.0, b.tanks[stage].capacity);
        rcs_fuel_ = std::clamp(state.rcs_propellant, 0.0, spec_.rcs_propellant);
        gear_position_ = std::clamp(state.gear_position, 0.0, 1.0);
        if (state.animation_positions.size() == spec_.animation_controls.size())
        {
            anim_pos_ = state.animation_positions;
            for (double &position : anim_pos_)
                position = std::clamp(position, 0.0, 1.0);
        }
        health_ = std::clamp(state.health, 0.0, 1.0);
        shield_ = std::clamp(state.shield, 0.0, spec_.shield.capacity);
        if (systems_ && state.has_systems)
            systems_->restore(state.systems);
        recentre();
        refresh_mass_properties();
    }

    void Ship::build_body()
    {
        body_ = universe_.world.create_body(name);
        sl::FlightBody &b = body();
        b.drag_area = spec_.drag_area;
        b.angular_damping = 0.0;
        for (std::size_t s = 0; s < spec_.stages.size(); ++s)
        {
            const StageSpec &stage = spec_.stages[s];
            b.tanks.push_back({stage.propellant, stage.propellant * stage.fuel_start});
            stage_first_thruster_.push_back(static_cast<int>(b.thrusters.size()));
            for (const EngineSpec &e : stage.engines)
            {
                sl::Thruster t;
                t.position = e.position;
                t.direction = e.direction;
                t.max_thrust = e.type == EngineType::hyperjump ? 0.0 : e.thrust;
                t.isp = e.isp;
                t.isp_sea_level = e.isp_sl;
                t.tank = static_cast<int>(s);
                t.air_breathing = e.air_breathing;
                b.thrusters.push_back(t);
                thruster_hover_.push_back(e.hover);
                unsigned mask = 0;
                for (const std::string &g : e.rcs_groups)
                    mask |= rcs_group_bit(g);
                thruster_rcs_.push_back(mask);
                explicit_rcs_ = explicit_rcs_ || mask != 0;
                thruster_pod_.push_back(e.pod ? (e.position.x < 0.0 ? 0 : 1) : -1);
            }
            for (const sl::dvec3 &g : stage.gear)
            {
                sl::GearContact contact;
                contact.position = g;
                contact.forward = {0.0, 0.0, -1.0};
                contact.rolling_resistance = 0.3;
                b.gear.push_back(contact);
            }
        }
        stage_first_thruster_.push_back(static_cast<int>(b.thrusters.size()));
        for (const WheelSpec &w : spec_.wheels)
        {
            sl::GearContact contact;
            contact.position = w.position;
            contact.forward = {0.0, 0.0, -1.0};
            contact.rolling_resistance = w.rolling_resistance;
            b.gear.push_back(contact);
        }
        for (const SurfaceSpec &sf : spec_.surfaces)
        {
            sl::LiftingSurface surface;
            surface.control_name = sf.control;
            surface.position = sf.position;
            surface.forward = sf.forward;
            surface.up = sf.up;
            surface.area = sf.area;
            surface.cl0 = sf.cl0;
            surface.cl_alpha = sf.cl_alpha;
            surface.cd0 = sf.cd0;
            surface.induced_drag = sf.induced_drag;
            surface.stall_angle = sf.stall;
            surface.control_gain = sf.gain;
            b.surfaces.push_back(surface);
        }
        refresh_mass_properties();
    }

    void Ship::set_surface_grip(double factor)
    {
        std::vector<sl::GearContact> &gear = body().gear;
        if (base_grip_.size() != gear.size())
        {
            base_grip_.clear();
            for (const sl::GearContact &g : gear)
                base_grip_.emplace_back(g.lateral_friction, g.brake_friction);
        }
        for (std::size_t i = 0; i < gear.size(); ++i)
        {
            gear[i].lateral_friction = base_grip_[i].first * factor;
            gear[i].brake_friction = base_grip_[i].second * factor;
        }
    }

    void Ship::refresh_mass_properties()
    {
        sl::FlightBody &b = body();
        double mass = 0.0, transverse = 0.0, roll = 0.0;
        for (std::size_t s = static_cast<std::size_t>(active_stage_); s < spec_.stages.size(); ++s)
        {
            const StageSpec &st = spec_.stages[s];
            mass += st.dry_mass;
            transverse += st.dry_mass * ((st.z - com_.z) * (st.z - com_.z) + st.length * st.length / 12.0 + st.radius * st.radius / 4.0);
            roll += st.dry_mass * st.radius * st.radius / 2.0;
        }
        mass = std::max(mass, 1.0);
        const double scale = (mass + attached_mass_ + payload_mass_) / mass;
        b.dry_mass = mass + attached_mass_ + payload_mass_;
        b.dry_inertia = {std::max(transverse, 1.0) * scale, std::max(transverse, 1.0) * scale, std::max(roll, 1.0) * scale};
        if (spec_.inertia.x > 0.0 && spec_.inertia.y > 0.0 && spec_.inertia.z > 0.0)
            b.dry_inertia = spec_.inertia * scale;
    }

    void Ship::set_payload_mass(double kilograms)
    {
        if (std::fabs(kilograms - payload_mass_) < 1.0e-6)
            return;
        payload_mass_ = kilograms;
        refresh_mass_properties();
    }

    double Ship::absorb(double amount)
    {
        if (spec_.shield.capacity <= 0.0 || amount <= 0.0)
            return amount;
        shield_quiet_ = 0.0;
        const double taken = std::min(shield_, amount);
        shield_ -= taken;
        return amount - taken;
    }

    void Ship::shield_tick(double dt)
    {
        if (spec_.shield.capacity <= 0.0)
            return;
        shield_quiet_ += dt;
        if (shield_quiet_ >= spec_.shield.delay)
            shield_ = std::min(spec_.shield.capacity, shield_ + spec_.shield.recharge * dt);
    }

    void Ship::damage(double amount)
    {
        if (crashed_ || amount <= 0.0)
            return;
        health_ = std::max(0.0, health_ - amount);
        if (health_ > 0.0)
            return;
        sl::FlightBody &b = body();
        for (sl::Thruster &t : b.thrusters)
            t.level = 0.0;
        // Something in the air is left to fall and crash by the usual rules; the ground and buildings are wrecked on the spot.
        if (spec_.building() || telemetry_.altitude < 50.0 || b.mode != sl::FlightBodyMode::dynamic)
        {
            crashed_ = true;
            pin_to_ground();
        }
    }

    void Ship::set_attached_mass(double kilograms)
    {
        if (std::fabs(kilograms - attached_mass_) < 1.0e-6)
            return;
        const double before = body().mass();
        attached_mass_ = kilograms;
        refresh_mass_properties();
        // The gear springs were sized for the ship alone; stiffen them for what it now carries so it does not sink.
        const double factor = before > 1.0 ? body().mass() / before : 1.0;
        if (gear_k_ > 0.0 && std::fabs(factor - 1.0) > 1.0e-9)
        {
            gear_k_ *= factor;
            for (std::size_t i = 0; i < body().gear.size(); ++i)
            {
                body().gear[i].stiffness *= factor;
                body().gear[i].damping *= factor;
            }
        }
    }

    void Ship::port_world(int index, sl::dvec3 &position, sl::dvec3 &direction, sl::dvec3 &up) const
    {
        const PortSpec &port = spec_.ports[static_cast<std::size_t>(index)];
        const sl::FlightBody &b = body();
        position = b.to_world(port.position - com_);
        const sl::dmat3 r = b.rotation();
        direction = r * port.direction;
        up = r * port.up;
    }

    double Ship::port_reach() const
    {
        double reach = 0.0;
        for (const PortSpec &port : spec_.ports)
            reach = std::max(reach, glm::length(port.position - com_));
        return reach;
    }

    sl::dvec3 Ship::point_velocity(const sl::dvec3 &world_point) const
    {
        const sl::FlightBody &b = body();
        const sl::dvec3 omega = b.rotation() * b.angular_velocity;
        return b.velocity + glm::cross(omega, world_point - b.position);
    }

    bool Ship::stage_once()
    {
        if (active_stage_ >= stage_count() - 1)
            return false;
        bool with_next;
        do
        {
            with_next = spec_.stages[static_cast<std::size_t>(active_stage_)].with_next;
            drop_stage(active_stage_);
            ++active_stage_;
        } while (with_next && active_stage_ < stage_count() - 1);
        stage_time_ = universe_.time();
        recentre();
        refresh_mass_properties();
        return true;
    }

    void Ship::drop_stage(int index)
    {
        const StageSpec &st = spec_.stages[static_cast<std::size_t>(index)];
        const double leftover = body().tanks[static_cast<std::size_t>(index)].mass;
        const double total = body().mass();
        const int count = st.radial_pair ? 2 : 1;
        std::vector<int> ids;
        for (int i = 0; i < count; ++i)
        {
            const int id = universe_.world.create_body(name + " " + st.name);
            if (id < 0)
            {
                std::fprintf(stderr, "[kosmos] cannot create debris for %s: flight body limit reached\n", st.name.c_str());
                for (int created : ids)
                    universe_.world.destroy_body(created);
                ids.clear();
                break;
            }
            ids.push_back(id);
        }
        // Creating a body can reallocate the world's body storage.
        sl::FlightBody &b = body();
        sl::dvec3 impulse(0.0);
        double separated_mass = 0.0;
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            const int id = ids[i];
            sl::FlightBody &d = *universe_.world.body(id);
            sl::dvec3 centre(0.0, 0.0, st.z);
            sl::dvec3 direction = st.separation_dir;
            if (st.radial_pair)
            {
                centre.x = st.parts[i].offset.x;
                centre.y = st.parts[i].offset.y;
                direction = {centre.x, centre.y, 0.0};
            }
            const sl::dvec3 kick = b.direction_to_world(glm::normalize(direction)) * st.separation_speed;
            d.dry_mass = std::max((st.dry_mass + leftover) / count, 1.0);
            const double radius = st.radial_pair ? st.radius / std::sqrt(2.0) : st.radius;
            const double transverse = d.dry_mass * (st.length * st.length / 12.0 + radius * radius / 4.0);
            d.dry_inertia = {transverse, transverse, d.dry_mass * radius * radius / 2.0};
            const sl::dvec3 arm = b.direction_to_world(centre - com_);
            d.position = b.position + arm;
            d.velocity = b.velocity + glm::cross(b.rotation() * b.angular_velocity, arm) + kick;
            d.orientation = b.orientation;
            d.angular_velocity = b.angular_velocity;
            d.drag_area = 3.14159 * radius * radius;
            Debris debris;
            debris.body = id;
            debris.stage = index;
            debris.centre = centre;
            debris.part = st.radial_pair ? static_cast<int>(i) : -1;
            debris_.push_back(debris);
            impulse += kick * d.dry_mass;
            separated_mass += d.dry_mass;
        }
        b.velocity -= impulse / std::max(total - separated_mass, 1.0);

        for (int i = stage_first_thruster_[static_cast<std::size_t>(index)]; i < stage_first_thruster_[static_cast<std::size_t>(index) + 1]; ++i)
        {
            b.thrusters[static_cast<std::size_t>(i)].max_thrust = 0.0;
            b.thrusters[static_cast<std::size_t>(i)].level = 0.0;
        }
        b.tanks[static_cast<std::size_t>(index)] = {0.0, 0.0};
        if (systems_)
            systems_->drop_stage(index);
        // Gear belongs to stages in order, and stages leave in order, so the leading contacts are this stage's.
        b.gear.erase(b.gear.begin(), b.gear.begin() + static_cast<std::ptrdiff_t>(std::min(st.gear.size(), b.gear.size())));
    }

    sl::dvec3 Ship::camera_focus() const
    {
        sl::dvec3 target = spec_.camera_target;
        if (active_stage_ <= 0 || active_stage_ >= static_cast<int>(spec_.stages.size()))
            return target;
        const auto middle = [&](int first)
        {
            double lo = 1.0e30, hi = -1.0e30;
            for (std::size_t s = static_cast<std::size_t>(first); s < spec_.stages.size(); ++s)
            {
                lo = std::min(lo, spec_.stages[s].z - spec_.stages[s].length * 0.5);
                hi = std::max(hi, spec_.stages[s].z + spec_.stages[s].length * 0.5);
            }
            return 0.5 * (lo + hi);
        };
        // Ease from the previous stack's centre to the new one, so the camera does not jump when a stage drops.
        const double t = std::clamp((universe_.time() - stage_time_) / 2.5, 0.0, 1.0), ease = t * t * (3.0 - 2.0 * t);
        const double before = active_stage_ > 1 ? middle(active_stage_ - 1) : middle(0);
        target.z += (before + (middle(active_stage_) - before) * ease) - middle(0);
        return target;
    }

    void Ship::recentre()
    {
        sl::FlightBody &b = body();
        double mass = 0.0, moment = 0.0;
        for (std::size_t s = static_cast<std::size_t>(active_stage_); s < spec_.stages.size(); ++s)
        {
            const double m = spec_.stages[s].dry_mass + (s < b.tanks.size() ? b.tanks[s].mass : 0.0);
            mass += m;
            moment += m * spec_.stages[s].z;
        }
        if (mass <= 0.0)
            return;
        const sl::dvec3 delta = sl::dvec3(0.0, 0.0, moment / mass) - com_;
        if (glm::length(delta) < 1.0e-6)
            return;
        const sl::dvec3 arm = b.direction_to_world(delta);
        b.velocity += glm::cross(b.rotation() * b.angular_velocity, arm);
        b.position += arm;
        for (sl::Thruster &t : b.thrusters)
            t.position -= delta;
        for (sl::GearContact &g : b.gear)
            g.position -= delta;
        for (sl::LiftingSurface &sf : b.surfaces)
            sf.position -= delta;
        com_ += delta;
    }

    void Ship::go_dynamic()
    {
        if (on_rails())
            universe_.world.release_rails(body_);
    }

    bool Ship::on_rails() const { return body().mode == sl::FlightBodyMode::rails; }

    bool Ship::flies_as_aircraft() const
    {
        if (!spec_.aircraft())
            return false;
        if (!spec_.hybrid)
            return true;
        const int soi = telemetry_.soi;
        if (soi < 0 || soi >= static_cast<int>(universe_.bodies.size()))
            return false;
        const Body &planet = universe_.bodies[static_cast<std::size_t>(soi)];
        return planet.atmosphere_density >= 0.1225 && telemetry_.altitude < atmosphere_top(planet);
    }

    bool Ship::go_rails()
    {
        if (on_rails() || crashed_)
            return on_rails();
        sl::FlightBody &b = body();
        b.angular_velocity = sl::dvec3(0.0);
        return universe_.world.set_rails(body_, universe_.soi_body(b.position));
    }

    bool Ship::can_rails(const Controls &c) const { return rails_blocker(c).empty(); }

    std::string Ship::rails_blocker(const Controls &c) const
    {
        if (crashed_)
            return "crashed";
        if (telemetry_.landed)
            return "landed";
        // A throttle left up with empty tanks does nothing, so it must not hold the clock back.
        if ((c.throttle > 0.0 || c.hover > 0.0) && telemetry_.thrust > 0.0)
            return "engines are firing";
        const Body &planet = universe_.bodies[static_cast<std::size_t>(telemetry_.soi)];
        const double top = atmosphere_top(planet);
        if (telemetry_.altitude < top + 1000.0)
            return "inside the atmosphere";
        // An orbit that will dip back into the air may still be warped; Sim::step slows the clock as the air gets close.
        return {};
    }

    double Ship::rails_margin() const
    {
        const Body &planet = universe_.bodies[static_cast<std::size_t>(telemetry_.soi)];
        const double top = atmosphere_top(planet);
        return top > 0.0 ? telemetry_.altitude - (top + 1000.0) : 1.0e18;
    }

    void Ship::pin_to_ground()
    {
        sl::FlightBody &b = body();
        const int soi = telemetry_.soi;
        if (soi < 0)
            return;
        const sl::dquat inverse = glm::inverse(ground_spin(universe_, soi));
        pin_body_ = soi;
        pin_position_ = inverse * (b.position - universe_.position(soi));
        pin_attitude_ = inverse * b.orientation;
        pinned_ = true;
        b.mode = sl::FlightBodyMode::kinematic;
        b.angular_velocity = sl::dvec3(0.0);
        for (sl::Thruster &t : b.thrusters)
            t.level = 0.0;
        hold_pinned();
    }

    void Ship::hold_for_warp(bool on)
    {
        if (on && !pinned_ && !crashed_ && !on_rails() && telemetry_.landed)
        {
            pin_to_ground();
            warp_pinned_ = true;
        }
        else if (!on && warp_pinned_)
        {
            warp_pinned_ = false;
            pinned_ = false;
            body().mode = sl::FlightBodyMode::dynamic;
        }
    }

    void Ship::hold_pinned()
    {
        if (!pinned_ || pin_body_ < 0)
            return;
        sl::FlightBody &b = body();
        const sl::dquat spin = ground_spin(universe_, pin_body_);
        const sl::FlightBody *planet = universe_.world.body(pin_body_);
        b.position = planet->position + spin * pin_position_;
        b.orientation = glm::normalize(spin * pin_attitude_);
        // Carried along with the ground: planet velocity plus its spin about the pole.
        b.velocity = planet->velocity + glm::cross(planet->spin_axis * planet->spin_rate, b.position - planet->position);
        b.angular_velocity = sl::dvec3(0.0);
    }

    void Ship::after_step(double dt)
    {
        update_telemetry();
        step_systems(dt);

        // A building is part of the ground: held there from the first step, with no flight.
        if (spec_.building())
        {
            if (!pinned_)
                pin_to_ground();
            else
                hold_pinned();
            return;
        }
        step_reentry(dt);
        resolve_landing();
        expire_debris(dt);
    }

    void Ship::step_systems(double dt)
    {
        sl::FlightBody &b = body();
        if (systems_ && dt > 0.0)
        {
            mission_time_ += dt;
            SystemsContext ctx;
            ctx.mission_time = mission_time_;
            for (std::size_t i = 0; i < spec_.animation_controls.size(); ++i)
                ctx.animation_positions.push_back(animation_position(i));
            ctx.landed = telemetry_.landed;
            if (ctx.landed && telemetry_.soi >= 0 && telemetry_.soi < static_cast<int>(universe_.bodies.size()))
                surface_richness(universe_, telemetry_.soi, ctx.water_richness, ctx.nitrogen_richness);
            {
                const auto &tanks = b.tanks;
                const std::size_t stage = static_cast<std::size_t>(active_stage_);
                if (stage < tanks.size())
                    ctx.propellant_room = std::max(tanks[stage].capacity - tanks[stage].mass, 0.0);
            }
            int star = -1;
            for (std::size_t i = 0; i < universe_.bodies.size(); ++i)
                if (universe_.bodies[i].kind == BodyKind::star)
                {
                    star = static_cast<int>(i);
                    break;
                }
            if (star >= 0)
            {
                const sl::dvec3 to_star = universe_.position(star) - b.position;
                const double distance = std::max(glm::length(to_star), 1.0);
                const sl::dvec3 dir = to_star / distance;
                ctx.sun_dir = b.direction_to_local(dir);
                ctx.sun_flux = universe_.stellar_luminosity * std::pow(149597870700.0 / distance, 2.0);
                // Eclipse: the ray to the star passes through any other body.
                for (std::size_t i = 0; i < universe_.bodies.size(); ++i)
                {
                    if (static_cast<int>(i) == star)
                        continue;
                    const sl::dvec3 to_body = universe_.position(static_cast<int>(i)) - b.position;
                    const double along = glm::dot(to_body, dir);
                    if (along <= 0.0 || along >= distance)
                        continue;
                    if (glm::length(to_body - dir * along) < universe_.bodies[i].radius)
                    {
                        ctx.sun_flux = 0.0;
                        break;
                    }
                }
            }
            std::vector<double> boundaries{0.0, dt};
            const bool animated = animation_previous_.size() == ctx.animation_positions.size() && std::abs(animation_elapsed_ - dt) < 1e-6;
            if (animated)
                for (const SystemComponent &component : spec_.systems.components)
                    if (component.deployment_index >= 0)
                    {
                        const std::size_t i = static_cast<std::size_t>(component.deployment_index);
                        const double start = animation_previous_[i], end = ctx.animation_positions[i], threshold = component.deployment_threshold;
                        if ((start < threshold && end >= threshold) || (start >= threshold && end < threshold))
                            boundaries.push_back(std::abs(threshold - start) * spec_.animation_controls[i].duration);
                    }
            std::sort(boundaries.begin(), boundaries.end());
            double propellant_made = 0.0;
            for (std::size_t segment = 1; segment < boundaries.size(); ++segment)
            {
                const double begin = boundaries[segment - 1], end = boundaries[segment];
                if (end <= begin)
                    continue;
                SystemsContext part = ctx;
                part.mission_time = ctx.mission_time - dt + end;
                part.propellant_room = std::max(ctx.propellant_room - propellant_made, 0.0);
                if (animated)
                    for (std::size_t i = 0; i < part.animation_positions.size(); ++i)
                    {
                        const double start = animation_previous_[i], target = ctx.animation_positions[i];
                        const double travel = (begin + end) * 0.5 / spec_.animation_controls[i].duration;
                        part.animation_positions[i] = start + std::clamp(target - start, -travel, travel);
                    }
                systems_->step(end - begin, part);
                propellant_made += systems_->take_propellant_made();
            }
            animation_elapsed_ = 0.0;
            step_instruments(dt, ctx.sun_dir, ctx.sun_flux > 0.0);
            if (propellant_made > 0.0 && static_cast<std::size_t>(active_stage_) < b.tanks.size())
                b.tanks[static_cast<std::size_t>(active_stage_)].mass += propellant_made;
            if (systems_->crew_lost() && !crashed_)
            {
                std::fprintf(stderr, "[kosmos] %s: crew lost\n", name.c_str());
                crashed_ = true;
                if (telemetry_.altitude < 50.0)
                    pin_to_ground();
                else
                {
                    b.mode = sl::FlightBodyMode::kinematic;
                    b.velocity = universe_.world.body(telemetry_.soi)->velocity;
                    b.angular_velocity = sl::dvec3(0.0);
                    for (sl::Thruster &t : b.thrusters)
                        t.level = 0.0;
                }
            }
        }
    }

    void Ship::resolve_landing()
    {
        sl::FlightBody &b = body();

        // Ships built for orbit have no landing gear: a gentle touchdown leaves them resting on the surface, a hard one wrecks them.
        double burning = 0.0;
        for (const sl::Thruster &t : b.thrusters)
            burning = std::max(burning, t.level);
        // A capsule has no gear either: it touches down when its hull, not its centre of mass, reaches the ground.
        double hull_reach = 0.3;
        const bool capsule_landing = !spec_.orbital_only && spec_.kind == "capsule" && b.gear.empty() && !spec_.aircraft() && !spec_.horizontal;
        if (capsule_landing)
        {
            hull_reach = 0.0;
            for (std::size_t s = static_cast<std::size_t>(std::max(active_stage_, 0)); s < spec_.stages.size(); ++s)
                hull_reach = std::max(hull_reach, spec_.stages[s].z + spec_.stages[s].length * 0.5);
        }
        const bool touching = (spec_.orbital_only || capsule_landing) && !crashed_ && burning < 0.05 && b.mode == sl::FlightBodyMode::dynamic &&
                              (telemetry_.landed || telemetry_.altitude <= hull_reach);
        const bool hard_landing = touching && approach_speed_ > spec_.crash_speed;
        if (hard_landing)
            std::fprintf(stderr, "[kosmos] %s: hit the ground at %.1f m/s\n", name.c_str(), approach_speed_);
        else if (touching)
        {
            const double lift = capsule_landing ? hull_reach - telemetry_.altitude : -telemetry_.altitude;
            if (lift > 0.0)
                b.position += glm::normalize(b.position - universe_.position(telemetry_.soi)) * lift;
            pin_to_ground();
            resting_ = true;
        }
        else if (resting_ && !crashed_)
        {
            if (burning > 0.05)
            {
                pinned_ = false;
                b.mode = sl::FlightBodyMode::dynamic;
                resting_ = false;
            }
        }
        if (!touching || hard_landing)
            approach_speed_ = telemetry_.surface_speed;
        if (hard_landing || (!crashed_ && b.mode == sl::FlightBodyMode::dynamic && !telemetry_.landed))
        {
            double tail = 0.0;
            for (std::size_t s = static_cast<std::size_t>(active_stage_); s < spec_.stages.size(); ++s)
                tail = std::max(tail, spec_.stages[s].z + spec_.stages[s].length * 0.5);
            double gear_z = 0.0;
            for (const sl::GearContact &g : b.gear)
                gear_z = std::max(gear_z, g.position.z);
            if (spec_.aircraft() || spec_.horizontal)
            {
                tail = 0.0;
                for (const sl::GearContact &g : b.gear)
                    tail = std::max(tail, -g.position.y);
                gear_z = tail;
            }
            if (hard_landing || telemetry_.altitude + std::max(tail, gear_z) < 0.4 * std::max(tail, gear_z))
            {
                crashed_ = true;
                // The wreck rests on the surface rather than sunk into it.
                const double origin_alt = universe_.world.altitude_of(b.position);
                if (origin_alt < 1.5)
                    b.position += glm::normalize(b.position - universe_.position(telemetry_.soi)) * (1.5 - origin_alt);
                pin_to_ground();
            }
        }
        else if (pinned_)
            hold_pinned();
        else if (crashed_ && b.mode == sl::FlightBodyMode::kinematic)
            b.velocity = universe_.world.body(telemetry_.soi)->velocity;
    }

    void Ship::expire_debris(double dt)
    {
        for (std::size_t i = 0; i < debris_.size();)
        {
            debris_[i].age += dt;
            const sl::FlightBody *d = universe_.world.body(debris_[i].body);
            const bool fallen = universe_.world.altitude_of(d->position) < -20.0;
            if (fallen || debris_[i].age > 900.0)
            {
                universe_.world.destroy_body(debris_[i].body);
                debris_.erase(debris_.begin() + static_cast<std::ptrdiff_t>(i));
            }
            else
                ++i;
        }
    }


    std::string ship_skins_directory(const ShipSpec &spec)
    {
        for (const StageSpec &stage : spec.stages)
            for (const PartSpec &part : stage.parts)
                if (!part.model.empty())
                {
                    const std::filesystem::path model = (std::filesystem::path(spec.directory) / part.model).lexically_normal();
                    return (model.parent_path().parent_path() / "Skins").string();
                }
        return "";
    }

    std::vector<std::string> ship_skins(const ShipSpec &spec)
    {
        std::vector<std::string> names;
        const std::string directory = ship_skins_directory(spec);
        std::error_code ec;
        if (!directory.empty())
            for (const auto &entry : std::filesystem::directory_iterator(directory, ec))
                if (entry.is_directory(ec))
                    names.push_back(entry.path().filename().string());
        std::sort(names.begin(), names.end());
        return names;
    }

} // namespace kosmos
