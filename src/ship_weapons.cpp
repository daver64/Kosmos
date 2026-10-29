#include "ship_weapons.h"

#include "weapon_sound.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace kosmos
{
    namespace
    {
        constexpr std::size_t max_bolts = 400, max_sparks = 900, max_flares = 200;

        sl::dvec3 unit(const sl::dvec3 &v, const sl::dvec3 &fallback)
        {
            const double l = glm::length(v);
            return l > 1.0e-12 ? v / l : fallback;
        }

        glm::vec3 to_f(const sl::dvec3 &v) { return glm::vec3(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)); }

        sl::Colour tint_of(const int c[3], double alpha)
        {
            return {static_cast<std::uint8_t>(std::clamp(c[0], 0, 255)), static_cast<std::uint8_t>(std::clamp(c[1], 0, 255)), static_cast<std::uint8_t>(std::clamp(c[2], 0, 255)),
                    static_cast<std::uint8_t>(std::clamp(alpha, 0.0, 1.0) * 255.0)};
        }

        // The weapon's colour pushed towards white by `amount`, for the hot middle of a glow.
        void brighten(const int c[3], double amount, int out[3])
        {
            for (int i = 0; i < 3; ++i)
                out[i] = static_cast<int>(c[i] + (255 - c[i]) * std::clamp(amount, 0.0, 1.0));
        }

        double fire_input(Sim &sim, const Ship *ship, int index, const std::string &group)
        {
            double value = sim.props.get_double("/ships/ship[" + std::to_string(index) + "]/weapons/" + group + "/fire");
            if (ship == sim.active_ship())
                value = std::max(value, sim.props.get_double("/controls/weapons/" + group + "/fire"));
            return value;
        }
    } // namespace

    ShipWeapons::~ShipWeapons() = default;

    void ShipWeapons::release_graphics()
    {
        if (beam_texture_)
            sl::destroy_bitmap(beam_texture_);
        beam_texture_ = nullptr;
    }

    void ShipWeapons::reset()
    {
        mounts_.clear();
        hit_events_.clear();
        ghost_mounts_.clear();
        beams_.clear();
        streaks_.clear();
        bolts_.clear();
        sparks_.clear();
        flares_.clear();
    }

    double ShipWeapons::random(double low, double high)
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return low + (high - low) * (static_cast<double>(rng_ & 0xFFFFFF) / static_cast<double>(0x1000000));
    }

    sl::dvec3 ShipWeapons::random_direction()
    {
        for (int i = 0; i < 8; ++i)
        {
            const sl::dvec3 v(random(-1, 1), random(-1, 1), random(-1, 1));
            if (glm::length(v) > 0.1 && glm::length(v) <= 1.0)
                return glm::normalize(v);
        }
        return sl::dvec3(0, 1, 0);
    }

    void ShipWeapons::add_sparks(const WeaponSpec &spec, int body, const sl::dvec3 &point, const sl::dvec3 &velocity, int count, double speed, const int *colour)
    {
        for (int i = 0; i < count; ++i)
        {
            if (sparks_.size() >= max_sparks)
                sparks_.erase(sparks_.begin(), sparks_.begin() + static_cast<std::ptrdiff_t>(max_sparks / 8));
            Spark s;
            s.body = body;
            s.position = point;
            s.carry = carry_;
            s.velocity = velocity + random_direction() * speed * random(0.2, 1.0);
            s.life = random(0.4, 1.2);
            const int *base = colour ? colour : spec.look.colour;
            if (random(0, 1) < 0.35)
                brighten(base, 0.7, s.colour);
            else
                std::copy(base, base + 3, s.colour);
            s.size = static_cast<float>(random(0.08, 0.25) * std::max(0.5, spec.look.width * 2.0));
            sparks_.push_back(s);
        }
    }

    void ShipWeapons::add_flare(const WeaponSpec &spec, int body, const sl::dvec3 &point, double size, double life, double light, bool shield, const int *colour)
    {
        if (flares_.size() >= max_flares)
            flares_.erase(flares_.begin());
        Flare f;
        f.body = body;
        f.position = point;
        f.carry = carry_;
        f.size = size;
        f.life = life;
        const int *base = colour ? colour : spec.look.colour;
        std::copy(base, base + 3, f.colour);
        f.light = static_cast<float>(light);
        f.range = static_cast<float>(spec.look.light_range * std::clamp(size / 4.0, 0.4, 3.0));
        f.shield = shield;
        flares_.push_back(f);
    }

    void ShipWeapons::hit(ArsenalContext &context, Arsenal &arsenal, const WeaponSpec &spec, int body, const sl::dvec3 &point, const Arsenal::RayHit &ray, double damage, bool tick)
    {
        const Universe &universe = context.sim->universe;
        const double scale = spec.impact_scale;
        ++hits_;
        {
            const sl::FlightBody *planet = universe.world.body(body);
            carry_ = ray.on_ship && ray.target && planet ? ray.target->body().velocity - planet->velocity : sl::dvec3(0.0);
        }
        const sl::dvec3 up = unit(point, sl::dvec3(0, 1, 0));
        double hull_damage = damage;
        bool shielded = false;
        if (ray.on_ship && ray.target)
        {
            Ship &target = *ray.target;
            const double before = target.shield_level();
            hull_damage = target.absorb(damage);
            shielded = before > 0.0 && hull_damage < damage;
            if (shielded)
            {
                ++shield_hits_;
                const int *shield_colour = target.spec().shield.colour;
                add_flare(spec, body, point, (tick ? 4.0 : 9.0) * std::sqrt(scale), tick ? 0.12 : 0.5, spec.look.light * 0.8, true, shield_colour);
                add_sparks(spec, body, point, sl::dvec3(0.0), tick ? 1 : 10, 30.0, shield_colour);
            }
            if (hull_damage > 0.0)
                target.damage(hull_damage);
            if (damage > 0.0 && hit_events_.size() < 64)
                hit_events_.push_back({&target, spec.id, shielded && hull_damage <= 0.0});
        }
        if (shielded && hull_damage <= 0.0)
            return;
        const double size = (2.0 + spec.look.width * 3.0) * std::sqrt(scale);
        add_flare(spec, body, point, tick ? size * 0.7 : size, tick ? 0.08 : 0.35, spec.look.light * 1.4);
        const int sparks = static_cast<int>((tick ? 3.0 : 14.0 + 40.0 * spec.look.sparkle) * std::sqrt(scale) * (0.4 + spec.look.sparkle));
        for (int i = 0; i < sparks; ++i)
        {
            sl::dvec3 d = random_direction();
            if (!ray.on_ship && glm::dot(d, up) < 0.0)
                d = -d;
            add_sparks(spec, body, point + up * 0.1, sl::dvec3(0.0), 1, 25.0 + 20.0 * std::sqrt(scale));
            if (!sparks_.empty())
                sparks_.back().velocity = d * glm::length(sparks_.back().velocity);
        }
        if (!tick)
        {
            if (spec.impact == "explosion")
                arsenal.spawn_effect(universe, sl::Effect3DKind::explosion, body, point + up * 0.1, std::max(1.0, 2.0 * scale));
            else if (spec.impact == "plasma")
                arsenal.spawn_effect(universe, sl::Effect3DKind::plasma, body, point + up * 0.1, std::max(0.5, scale), up);
        }
    }

    void ShipWeapons::burst(ArsenalContext &context, Arsenal &arsenal, const WeaponSpec &spec, int body, const sl::dvec3 &point, double scale)
    {
        (void)context;
        (void)arsenal;
        add_flare(spec, body, point, (6.0 + spec.look.width * 10.0) * scale, 0.6, spec.look.light);
        add_sparks(spec, body, point, sl::dvec3(0.0), static_cast<int>(10 + 14 * spec.look.sparkle), 30.0 * std::sqrt(scale));
    }

    void ShipWeapons::play_beam(const WeaponSpec &spec)
    {
        if (!sound_on_ || sound_gain_ < 0.02)
            return;
        const int key = static_cast<int>(spec.duration * 10.0 + 0.5);
        sl::Sample *&sample = beam_samples_[key];
        if (!sample)
        {
            const std::vector<std::uint8_t> wav = make_beam_wav(spec.duration, 1.0);
            sample = sl::load_sample_from_memory(wav.data(), wav.size());
        }
        if (sample)
            sl::play_sample(sample, static_cast<int>(255.0 * std::min(1.0, sound_gain_)), 128, 1000, 0);
    }

    void ShipWeapons::play_zap()
    {
        if (!sound_on_ || sound_gain_ < 0.02)
            return;
        if (!zap_sample_)
        {
            const std::vector<std::uint8_t> wav = make_zap_wav();
            zap_sample_ = sl::load_sample_from_memory(wav.data(), wav.size());
        }
        if (zap_sample_)
            sl::play_sample(zap_sample_, static_cast<int>(200.0 * std::min(1.0, sound_gain_)), 128, 960 + static_cast<int>(random(0, 80)), 0);
    }

    void ShipWeapons::fire_mount(ArsenalContext &context, Arsenal &arsenal, Ship *shooter, const Muzzle &muzzle, const WeaponSpec &spec, MountState &state, double dt)
    {
        Universe &universe = context.sim->universe;
        const int body = muzzle.body;
        // A shooter that is not a local ship (another player's ghost) only makes the show: its shots do no damage.
        const double damage_scale = shooter ? 1.0 : 0.0;
        const sl::dvec3 dir = muzzle.direction;
        const sl::dvec3 from = muzzle.position - universe.position(body) + dir * 0.5;
        const sl::dvec3 vel = muzzle.velocity;
        carry_ = vel;

        const sl::dvec3 helper = std::fabs(dir.y) < 0.9 ? sl::dvec3(0, 1, 0) : sl::dvec3(1, 0, 0);
        const sl::dvec3 e1 = glm::normalize(glm::cross(dir, helper)), e2 = glm::cross(dir, e1);
        auto aim_with = [&](double spread) { return unit(dir + (e1 * random(-1, 1) + e2 * random(-1, 1)) * spread, dir); };

        if (spec.fire == "beam")
        {
            if (state.lock > 0.0)
                return;
            if (state.beam_time <= 0.0)
            {
                play_beam(spec);
                add_flare(spec, body, from, spec.look.width * spec.look.glow * 3.0, 0.35, spec.look.light * 2.0);
            }
            state.beam_time += dt;
            if (state.beam_time > spec.duration)
            {
                state.lock = spec.cooldown;
                state.beam_time = 0.0;
                return;
            }
            // A quick flare up, then a fade to nothing.
            const double rise = std::min(1.0, state.beam_time / 0.07);
            const double fall = std::pow(std::max(0.0, 1.0 - state.beam_time / spec.duration), 1.6);
            const double strength = rise * fall;
            const sl::dvec3 end = from + dir * spec.range;
            const Arsenal::RayHit ray = arsenal.cast_ray(context, body, from, end, shooter);
            Segment seg;
            seg.body = body;
            seg.a = from;
            seg.b = ray.hit ? ray.point : end;
            seg.spec = &spec;
            seg.ended = ray.hit;
            seg.strength = strength;
            beams_.push_back(seg);
            if (ray.hit)
            {
                hit(context, arsenal, spec, body, ray.point, ray, spec.ship_damage * dt * strength * damage_scale, true);
                state.effect_timer += dt;
                if (state.effect_timer > 0.4)
                {
                    state.effect_timer = 0.0;
                    if (spec.impact != "spark")
                        hit(context, arsenal, spec, body, ray.point, ray, 0.0, false);
                }
            }
            return;
        }

        if (spec.fire == "volley")
        {
            add_flare(spec, body, from, spec.look.width * spec.look.glow * 1.5, 0.08, spec.look.light * 0.6);
            Bolt b;
            b.body = body;
            b.position = from;
            b.velocity = vel + aim_with(spec.spread) * spec.speed;
            b.spec = &spec;
            b.carry = vel;
            b.owner = shooter;
            b.life = spec.range / spec.speed;
            if (bolts_.size() >= max_bolts)
                bolts_.erase(bolts_.begin());
            bolts_.push_back(b);
            ++shots_;
            play_zap();
            return;
        }

        if (state.cooldown > 0.0)
            return;
        state.cooldown = 1.0 / spec.rate;
        ++shots_;
        add_flare(spec, body, from, spec.look.width * spec.look.glow * 1.6, 0.12, spec.look.light * 0.8);
        add_sparks(spec, body, from, sl::dvec3(0.0), static_cast<int>(2 + 6 * spec.look.sparkle), 15.0);

        if (spec.fire == "pulse")
        {
            const sl::dvec3 end = from + aim_with(spec.spread) * spec.range;
            const Arsenal::RayHit ray = arsenal.cast_ray(context, body, from, end, shooter);
            Segment seg;
            seg.body = body;
            seg.a = from;
            seg.b = ray.hit ? ray.point : end;
            seg.spec = &spec;
            seg.life = spec.duration;
            seg.carry = vel;
            seg.ended = ray.hit;
            streaks_.push_back(seg);
            if (ray.hit)
                hit(context, arsenal, spec, body, ray.point, ray, spec.ship_damage * damage_scale, false);
        }
        else
        {
            const bool flak = spec.fire == "flak";
            for (int i = 0; i < (flak ? spec.count : 1); ++i)
            {
                Bolt b;
                b.body = body;
                b.position = from;
                const double speed = flak ? spec.speed * random(0.75, 1.0) : spec.speed;
                b.velocity = vel + (flak ? aim_with(spec.cone) : aim_with(spec.spread)) * speed;
                b.spec = &spec;
                b.carry = vel;
                b.owner = shooter;
                b.life = flak && spec.lifetime > 0.0 ? spec.lifetime : spec.range / spec.speed;
                b.burst = flak;
                if (bolts_.size() >= max_bolts)
                    bolts_.erase(bolts_.begin());
                bolts_.push_back(b);
            }
        }
    }

    void ShipWeapons::drive_mount(ArsenalContext &context, Arsenal &arsenal, Ship *shooter, const Muzzle &muzzle, const WeaponSpec &spec, MountState &state, bool trigger, double dt)
    {
        if (spec.fire == "volley")
        {
            // A volley runs to its end once started, whatever the trigger does.
            state.volley_timer -= dt;
            if (state.volley_left > 0)
            {
                if (state.volley_timer <= 0.0)
                {
                    fire_mount(context, arsenal, shooter, muzzle, spec, state, dt);
                    --state.volley_left;
                    state.volley_timer = spec.interval;
                }
            }
            else if (trigger && state.cooldown <= 0.0)
            {
                state.volley_left = spec.count;
                state.volley_timer = 0.0;
                state.cooldown = 1.0 / spec.rate;
            }
        }
        else if (spec.fire == "beam")
        {
            // A pulse runs its whole course once triggered; held down, it fires again each time it has recharged.
            if (state.beam_time > 0.0 || trigger)
                fire_mount(context, arsenal, shooter, muzzle, spec, state, dt);
            else
                state.effect_timer = 0.0;
        }
        else if (trigger)
            fire_mount(context, arsenal, shooter, muzzle, spec, state, dt);
        else
            state.effect_timer = 0.0;
    }

    void ShipWeapons::update_ghosts(ArsenalContext &context, Arsenal &arsenal, const std::vector<GhostShooter> &ghosts, double dt)
    {
        Sim &sim = *context.sim;
        std::set<std::uint32_t> present;
        for (const GhostShooter &g : ghosts)
        {
            present.insert(g.id);
            const sl::FlightBody *planet = sim.universe.world.body(g.body);
            if (!planet || !g.spec || g.spec->weapons.empty())
                continue;
            for (std::size_t m = 0; m < g.spec->weapons.size(); ++m)
            {
                const ShipSpec::WeaponMount &mount = g.spec->weapons[m];
                const WeaponSpec *spec = sim.weapon_specs.find(mount.weapon);
                if (!spec)
                    continue;
                MountState &state = ghost_mounts_[{g.id, static_cast<int>(m)}];
                state.cooldown = std::max(0.0, state.cooldown - dt);
                state.lock = std::max(0.0, state.lock - dt);
                Muzzle muzzle;
                muzzle.body = g.body;
                muzzle.position = g.position + g.orientation * mount.position;
                muzzle.direction = unit(g.orientation * mount.direction, sl::dvec3(0, 0, -1));
                muzzle.velocity = g.velocity;
                const Ship *listener = sim.active_ship();
                sound_gain_ = listener ? 1.0 / (1.0 + glm::length(muzzle.position - listener->origin()) / 400.0) : 1.0;
                const bool trigger = mount.group == "primary" ? (g.fire & 1) != 0 : mount.group == "secondary" && (g.fire & 2) != 0;
                drive_mount(context, arsenal, nullptr, muzzle, *spec, state, trigger, dt);
            }
        }
        for (auto it = ghost_mounts_.begin(); it != ghost_mounts_.end();)
            it = present.count(it->first.first) || dt <= 0.0 ? std::next(it) : ghost_mounts_.erase(it);
    }

    void ShipWeapons::update(ArsenalContext &context, Arsenal &arsenal, double dt, double real_dt)
    {
        Sim &sim = *context.sim;
        beams_.clear();
        for (std::size_t i = 0; i < bolts_.size();)
        {
            Bolt &b = bolts_[i];
            const sl::dvec3 next = b.position + b.velocity * dt;
            b.age += dt;
            carry_ = b.carry;
            const Arsenal::RayHit ray = arsenal.cast_ray(context, b.body, b.position, next, b.owner);
            bool done = false;
            if (ray.hit)
            {
                hit(context, arsenal, *b.spec, b.body, ray.point, ray, b.owner ? b.spec->ship_damage * (b.burst ? 0.25 : 1.0) : 0.0, false);
                if (b.burst)
                    burst(context, arsenal, *b.spec, b.body, ray.point, 0.5);
                done = true;
            }
            else
            {
                b.position = next;
                if (random(0, 1) < b.spec->look.sparkle * (b.burst || b.spec->fire == "volley" ? 0.25 : 1.0) * std::min(1.0, dt * 40.0))
                    add_sparks(*b.spec, b.body, b.position, sl::dvec3(0.0), 1, 6.0);
                if (b.age >= b.life)
                {
                    if (b.burst)
                        burst(context, arsenal, *b.spec, b.body, b.position, 1.0);
                    done = true;
                }
            }
            if (done)
                bolts_.erase(bolts_.begin() + static_cast<std::ptrdiff_t>(i));
            else
                ++i;
        }

        for (std::size_t i = 0; i < streaks_.size();)
        {
            streaks_[i].age += real_dt;
            streaks_[i].a += streaks_[i].carry * real_dt;
            streaks_[i].b += streaks_[i].carry * real_dt;
            if (streaks_[i].age >= streaks_[i].life)
                streaks_.erase(streaks_.begin() + static_cast<std::ptrdiff_t>(i));
            else
                ++i;
        }
        const double drag = std::exp(-1.2 * real_dt);
        for (std::size_t i = 0; i < sparks_.size();)
        {
            Spark &s = sparks_[i];
            s.age += real_dt;
            s.position += (s.velocity + s.carry) * real_dt;
            s.velocity *= drag;
            if (s.age >= s.life)
                sparks_.erase(sparks_.begin() + static_cast<std::ptrdiff_t>(i));
            else
                ++i;
        }
        for (std::size_t i = 0; i < flares_.size();)
        {
            flares_[i].age += real_dt;
            flares_[i].position += flares_[i].carry * real_dt;
            if (flares_[i].age >= flares_[i].life)
                flares_.erase(flares_.begin() + static_cast<std::ptrdiff_t>(i));
            else
                ++i;
        }
        // New shots are made last so they are not carried a frame's travel on the frame they appear.
        std::set<const Ship *> alive;
        if (dt > 0.0)
            for (int i = 0; i < sim.ship_count(); ++i)
            {
                Ship *ship = sim.ship(i);
                alive.insert(ship);
                ship->shield_tick(dt);
                if (ship->spec().weapons.empty() || ship->health() <= 0.0 || ship->on_rails())
                    continue;
                for (std::size_t m = 0; m < ship->spec().weapons.size(); ++m)
                {
                    const ShipSpec::WeaponMount &mount = ship->spec().weapons[m];
                    const WeaponSpec *spec = sim.weapon_specs.find(mount.weapon);
                    if (!spec)
                        continue;
                    MountState &state = mounts_[{ship, static_cast<int>(m)}];
                    const sl::FlightBody *planet = sim.universe.world.body(ship->telemetry().soi);
                    if (!planet)
                        continue;
                    const sl::dvec3 world = ship->origin() + ship->body().direction_to_world(mount.position);
                    Muzzle muzzle;
                    muzzle.body = ship->telemetry().soi;
                    muzzle.position = world;
                    muzzle.direction = unit(ship->body().direction_to_world(mount.direction), sl::dvec3(0, 0, -1));
                    muzzle.velocity = ship->point_velocity(world) - planet->velocity;
                    state.cooldown = std::max(0.0, state.cooldown - dt);
                    state.lock = std::max(0.0, state.lock - dt);
                    const bool trigger = fire_input(sim, ship, i, mount.group) > 0.5;
                    const Ship *listener = sim.active_ship();
                    sound_gain_ = listener ? 1.0 / (1.0 + glm::length(ship->origin() - listener->origin()) / 400.0) : 1.0;
                    drive_mount(context, arsenal, ship, muzzle, *spec, state, trigger, dt);
                }
            }
        for (auto it = mounts_.begin(); it != mounts_.end();)
            it = alive.count(it->first.first) || dt <= 0.0 ? std::next(it) : mounts_.erase(it);
    }

    // ---- Drawing

    namespace
    {
        // A strip along a..b (eye-relative) facing the eye, `width` wide.
        glm::mat4 strip_basis(const glm::vec3 &a, const glm::vec3 &b, float width)
        {
            const glm::vec3 mid = (a + b) * 0.5f, dir = b - a;
            const float length = glm::length(dir);
            if (length < 1.0e-3f || glm::length(mid) < 1.0e-3f)
                return glm::mat4(0.0f);
            const glm::vec3 d = dir / length, to_eye = -glm::normalize(mid);
            glm::vec3 side = glm::cross(d, to_eye);
            if (glm::length(side) < 1.0e-3f)
                return glm::mat4(0.0f);
            side = glm::normalize(side) * (width * 0.5f);
            return glm::mat4(glm::vec4(side, 0.0f), glm::vec4(to_eye, 0.0f), glm::vec4(d * (length * 0.5f), 0.0f), glm::vec4(mid, 1.0f));
        }

        glm::mat4 facing_basis(const glm::vec3 &at, float half)
        {
            const glm::vec3 to_eye = -glm::normalize(at);
            const glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), to_eye));
            const glm::vec3 up = glm::cross(to_eye, right);
            return glm::mat4(glm::vec4(right * half, 0.0f), glm::vec4(to_eye, 0.0f), glm::vec4(up * half, 0.0f), glm::vec4(at, 1.0f));
        }

        // Alpha across a beam: soft falling edges with a bright middle, white so the material tint sets the colour.
        sl::Bitmap *make_beam_texture()
        {
            const int w = 64, h = 4;
            sl::Bitmap *bitmap = sl::create_bitmap(w, h);
            if (!bitmap)
                return nullptr;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const double u = std::fabs((x + 0.5) / w * 2.0 - 1.0);
                    const double a = std::clamp(std::exp(-u * u * 4.0) * (1.0 - std::pow(u, 6.0)), 0.0, 1.0);
                    sl::putpixel(bitmap, x, y, {255, 255, 255, static_cast<std::uint8_t>(a * 255.0)});
                }
            bitmap->smooth = true;
            return bitmap;
        }
    } // namespace

    void ShipWeapons::draw(const Universe &universe, const sl::dvec3 &eye_world, sl::Mesh *quad, sl::Bitmap *glow)
    {
        if (!quad || (beams_.empty() && streaks_.empty() && bolts_.empty() && sparks_.empty() && flares_.empty()))
            return;
        if (!beam_texture_)
            beam_texture_ = make_beam_texture();
        sl::Material m;
        m.lit = false;
        m.fog = false;
        m.cull = sl::CullMode::none;
        m.blend = sl::MaterialBlend::additive;

        auto rel_of = [&](int body, const sl::dvec3 &p) {
            return to_f(universe.position(body) + p - eye_world);
        };
        // Wide halo, bright body and a white-hot core, so a beam reads as light even at a distance.
        auto glowing_strip = [&](const glm::vec3 &a, const glm::vec3 &b, const WeaponLook &look, double strength) {
            const float distance = std::min(glm::length(a), glm::length(b));
            const float core = std::max(static_cast<float>(look.width), distance * 0.01f);
            const float halo = core * static_cast<float>(look.glow);
            int hot[3];
            brighten(look.colour, 0.5, hot);
            m.texture = beam_texture_;
            m.tint = tint_of(look.colour, 0.45 * strength);
            sl::draw_mesh(quad, strip_basis(a, b, halo), m);
            m.tint = tint_of(look.colour, 0.85 * strength);
            sl::draw_mesh(quad, strip_basis(a, b, halo * 0.45f), m);
            m.tint = tint_of(hot, 0.9 * strength);
            sl::draw_mesh(quad, strip_basis(a, b, core * 1.6f), m);
            m.tint = tint_of(look.core, strength);
            sl::draw_mesh(quad, strip_basis(a, b, core), m);
        };
        auto round_glow = [&](const glm::vec3 &at, float size, const int colour[3], double alpha) {
            if (glm::length(at) < 0.5f)
                return;
            const float floor_size = glm::length(at) * 0.009f;
            m.texture = glow;
            m.tint = tint_of(colour, alpha);
            sl::draw_mesh(quad, facing_basis(at, std::max(size, floor_size)), m);
        };

        for (const Segment &s : beams_)
        {
            const glm::vec3 a = rel_of(s.body, s.a);
            glm::vec3 b = rel_of(s.body, s.b);
            const double flicker = 0.9 + 0.1 * std::sin(a.x * 3.1 + a.z * 1.7 + b.y);
            // A beam that met nothing runs on past its range and fades away, so in perspective it dies into the distance rather
            // than stopping square. Pieces are cut so each is as wide as its distance calls for.
            const float reach = s.ended ? 1.0f : 20.0f;
            const glm::vec3 far_end = a + (b - a) * reach;
            const int pieces = 40;
            for (int k = 0; k < pieces; ++k)
            {
                const float s0 = std::pow(static_cast<float>(k) / pieces, 3.0f), s1 = std::pow(static_cast<float>(k + 1) / pieces, 3.0f);
                const float mid = 0.5f * (s0 + s1);
                const float fade = s.ended ? 1.0f : 1.0f - std::clamp((mid - 0.04f) / 0.06f, 0.0f, 1.0f);
                if (fade > 0.01f)
                    glowing_strip(a + (far_end - a) * s0, a + (far_end - a) * s1, s.spec->look, flicker * fade * fade * s.strength);
            }
            round_glow(a, static_cast<float>(s.spec->look.width * s.spec->look.glow * 1.4), s.spec->look.colour, 0.8 * s.strength);
            if (s.ended)
                round_glow(b, static_cast<float>(s.spec->look.width * s.spec->look.glow * 2.2), s.spec->look.colour, 0.9 * s.strength);
        }
        for (const Segment &s : streaks_)
        {
            const double fade = 1.0 - s.age / std::max(s.life, 1.0e-3);
            const glm::vec3 a = rel_of(s.body, s.a), b = rel_of(s.body, s.b);
            glowing_strip(a, b, s.spec->look, fade * fade);
            if (s.ended)
                round_glow(b, static_cast<float>(s.spec->look.width * s.spec->look.glow * 2.0), s.spec->look.colour, fade);
        }
        for (const Bolt &b : bolts_)
        {
            const WeaponLook &look = b.spec->look;
            // The trail follows the motion relative to the shooter; the planet-relative velocity would include the orbital speed.
            const sl::dvec3 dir = unit(b.velocity - b.carry, sl::dvec3(0, 0, -1));
            const double length = b.burst ? look.length * 0.4 : look.length;
            const glm::vec3 head = rel_of(b.body, b.position);
            const double on_screen = glm::length(head) * 0.05;
            const glm::vec3 tail = rel_of(b.body, b.position - dir * std::max(length, on_screen));
            WeaponLook trail = look;
            trail.glow = std::min(trail.glow, 2.2);
            glowing_strip(tail, head, trail, 0.9);
            round_glow(head, static_cast<float>(look.width * look.glow * 1.6), look.colour, 0.8 + 0.2 * std::sin(b.age * 70.0 + head.x));
            int hot[3];
            brighten(look.colour, 0.8, hot);
            round_glow(head, static_cast<float>(look.width * look.glow * 0.6), hot, 1.0);
        }
        for (const Flare &f : flares_)
        {
            const double t = f.age / std::max(f.life, 1.0e-3);
            const double fade = (1.0 - t) * (1.0 - t);
            const glm::vec3 at = rel_of(f.body, f.position);
            if (f.shield)
            {
                round_glow(at, static_cast<float>(f.size * (0.6 + 0.8 * t)), f.colour, 0.55 * fade);
                continue;
            }
            int hot[3];
            brighten(f.colour, 0.75, hot);
            round_glow(at, static_cast<float>(f.size * (0.7 + 0.5 * t)), f.colour, 0.85 * fade);
            round_glow(at, static_cast<float>(f.size * 0.35), hot, fade);
        }
        for (const Spark &s : sparks_)
        {
            const double fade = 1.0 - s.age / std::max(s.life, 1.0e-3);
            const glm::vec3 a = rel_of(s.body, s.position), b = rel_of(s.body, s.position - unit(s.velocity, sl::dvec3(0, 1, 0)) * std::max(std::min(glm::length(s.velocity) * 0.04, 4.0), static_cast<double>(glm::length(a)) * 0.006));
            const float width = std::max(s.size * 2.0f, glm::length(a) * 0.004f);
            // A short streak is a hard-edged chip at distance; draw those as soft points instead.
            if (glm::length(a - b) > width * 3.0f)
            {
                m.texture = beam_texture_;
                m.tint = tint_of(s.colour, fade);
                sl::draw_mesh(quad, strip_basis(a, b, width), m);
            }
            round_glow(a, width * 2.0f, s.colour, 0.3 * fade + (glm::length(a - b) > width * 3.0f ? 0.0 : 0.5 * fade));
        }
    }

    // ---- Lights

    void ShipWeapons::collect_lights(const Universe &universe, const sl::dvec3 &eye_world, int max_lights, std::vector<PlacedLight> &out) const
    {
        if (max_lights <= 0)
            return;
        struct Candidate
        {
            PlacedLight light;
            float score;
        };
        std::vector<Candidate> found;
        auto add = [&](int body, const sl::dvec3 &p, const int colour[3], double intensity, double range) {
            if (intensity <= 0.01)
                return;
            const sl::dvec3 rel = universe.position(body) + p - eye_world;
            const float distance = static_cast<float>(glm::length(rel));
            if (distance > 4000.0f)
                return;
            Candidate c;
            c.light.light.position = to_f(rel);
            c.light.light.colour = glm::vec3(static_cast<float>(colour[0]), static_cast<float>(colour[1]), static_cast<float>(colour[2])) / 255.0f;
            c.light.light.intensity = static_cast<float>(intensity);
            c.light.light.range = static_cast<float>(range);
            c.light.glow_size = 0.0f;
            c.light.distance = distance;
            const float reach = distance / std::max(1.0f, static_cast<float>(range));
            c.score = static_cast<float>(intensity) / (1.0f + reach * reach);
            found.push_back(c);
        };
        auto along = [&](const Segment &s, double strength) {
            const WeaponLook &look = s.spec->look;
            add(s.body, s.a, look.colour, look.light * strength, look.light_range);
            if (s.ended)
                add(s.body, s.b, look.colour, look.light * 1.3 * strength, look.light_range);
            if (glm::length(s.b - s.a) > 80.0)
                add(s.body, (s.a + s.b) * 0.5, look.colour, look.light * 0.6 * strength, look.light_range);
        };
        for (const Segment &s : beams_)
            along(s, s.strength);
        for (const Segment &s : streaks_)
        {
            const double fade = 1.0 - s.age / std::max(s.life, 1.0e-3);
            along(s, fade * fade);
        }
        for (const Bolt &b : bolts_)
            add(b.body, b.position, b.spec->look.colour, b.spec->look.light * (b.burst ? 0.3 : 0.6), b.spec->look.light_range * 0.6);
        for (const Flare &f : flares_)
        {
            const double t = f.age / std::max(f.life, 1.0e-3);
            add(f.body, f.position, f.colour, f.light * (1.0 - t), f.range);
        }
        std::sort(found.begin(), found.end(), [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
        for (std::size_t i = 0; i < found.size() && static_cast<int>(i) < max_lights; ++i)
            out.push_back(found[i].light);
    }
} // namespace kosmos
