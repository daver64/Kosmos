#include "ship_weapons.h"

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
            s.velocity = velocity + random_direction() * speed * random(0.2, 1.0);
            s.life = random(0.25, 0.9);
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
        add_flare(spec, body, point, (2.0 + spec.look.width * 4.0) * scale, 0.4, spec.look.light);
        add_sparks(spec, body, point, sl::dvec3(0.0), static_cast<int>(18 + 50 * spec.look.sparkle), 45.0 * std::sqrt(scale));
    }

    void ShipWeapons::fire_mount(ArsenalContext &context, Arsenal &arsenal, Ship &ship, const ShipSpec::WeaponMount &mount, const WeaponSpec &spec, MountState &state, double dt)
    {
        Universe &universe = context.sim->universe;
        const int body = ship.telemetry().soi;
        const sl::FlightBody *planet = universe.world.body(body);
        if (!planet)
            return;
        const sl::dvec3 world = ship.origin() + ship.body().direction_to_world(mount.position);
        const sl::dvec3 dir = unit(ship.body().direction_to_world(mount.direction), sl::dvec3(0, 0, -1));
        const sl::dvec3 from = world - universe.position(body) + dir * 0.5;
        const sl::dvec3 vel = ship.point_velocity(world) - planet->velocity;

        const sl::dvec3 helper = std::fabs(dir.y) < 0.9 ? sl::dvec3(0, 1, 0) : sl::dvec3(1, 0, 0);
        const sl::dvec3 e1 = glm::normalize(glm::cross(dir, helper)), e2 = glm::cross(dir, e1);
        auto aim_with = [&](double spread) { return unit(dir + (e1 * random(-1, 1) + e2 * random(-1, 1)) * spread, dir); };

        if (spec.fire == "beam")
        {
            if (state.lock > 0.0)
                return;
            state.beam_time += dt;
            if (state.beam_time > spec.max_time)
            {
                state.lock = spec.cooldown;
                state.beam_time = 0.0;
                return;
            }
            const sl::dvec3 end = from + dir * spec.range;
            const Arsenal::RayHit ray = arsenal.cast_ray(context, body, from, end, &ship);
            Segment seg;
            seg.body = body;
            seg.a = from;
            seg.b = ray.hit ? ray.point : end;
            seg.spec = &spec;
            seg.ended = ray.hit;
            beams_.push_back(seg);
            if (ray.hit)
            {
                hit(context, arsenal, spec, body, ray.point, ray, spec.ship_damage * dt, true);
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

        if (state.cooldown > 0.0)
            return;
        state.cooldown = 1.0 / spec.rate;
        ++shots_;
        add_flare(spec, body, from, spec.look.width * spec.look.glow * 1.6, 0.12, spec.look.light * 0.8);
        add_sparks(spec, body, from, vel, static_cast<int>(2 + 6 * spec.look.sparkle), 15.0);

        if (spec.fire == "pulse")
        {
            const sl::dvec3 end = from + aim_with(spec.spread) * spec.range;
            const Arsenal::RayHit ray = arsenal.cast_ray(context, body, from, end, &ship);
            Segment seg;
            seg.body = body;
            seg.a = from;
            seg.b = ray.hit ? ray.point : end;
            seg.spec = &spec;
            seg.life = spec.duration;
            seg.ended = ray.hit;
            streaks_.push_back(seg);
            if (ray.hit)
                hit(context, arsenal, spec, body, ray.point, ray, spec.ship_damage, false);
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
                b.owner = &ship;
                b.life = flak && spec.lifetime > 0.0 ? spec.lifetime : spec.range / spec.speed;
                b.burst = flak;
                if (bolts_.size() >= max_bolts)
                    bolts_.erase(bolts_.begin());
                bolts_.push_back(b);
            }
        }
    }

    void ShipWeapons::update(ArsenalContext &context, Arsenal &arsenal, double dt, double real_dt)
    {
        Sim &sim = *context.sim;
        beams_.clear();
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
                    state.cooldown = std::max(0.0, state.cooldown - dt);
                    state.lock = std::max(0.0, state.lock - dt);
                    if (fire_input(sim, ship, i, mount.group) > 0.5)
                        fire_mount(context, arsenal, *ship, mount, *spec, state, dt);
                    else
                    {
                        state.beam_time = std::max(0.0, state.beam_time - dt * spec->max_time / std::max(spec->cooldown, 0.1));
                        state.effect_timer = 0.0;
                    }
                }
            }
        for (auto it = mounts_.begin(); it != mounts_.end();)
            it = alive.count(it->first.first) || dt <= 0.0 ? std::next(it) : mounts_.erase(it);

        for (std::size_t i = 0; i < bolts_.size();)
        {
            Bolt &b = bolts_[i];
            const sl::dvec3 next = b.position + b.velocity * dt;
            b.age += dt;
            const Arsenal::RayHit ray = arsenal.cast_ray(context, b.body, b.position, next, b.owner);
            bool done = false;
            if (ray.hit)
            {
                hit(context, arsenal, *b.spec, b.body, ray.point, ray, b.spec->ship_damage * (b.burst ? 0.25 : 1.0), false);
                if (b.burst)
                    burst(context, arsenal, *b.spec, b.body, ray.point, 0.5);
                done = true;
            }
            else
            {
                b.position = next;
                if (random(0, 1) < b.spec->look.sparkle * std::min(1.0, dt * 40.0))
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
            s.position += s.velocity * real_dt;
            s.velocity *= drag;
            if (s.age >= s.life)
                sparks_.erase(sparks_.begin() + static_cast<std::ptrdiff_t>(i));
            else
                ++i;
        }
        for (std::size_t i = 0; i < flares_.size();)
        {
            flares_[i].age += real_dt;
            if (flares_[i].age >= flares_[i].life)
                flares_.erase(flares_.begin() + static_cast<std::ptrdiff_t>(i));
            else
                ++i;
        }
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

        auto rel_of = [&](int body, const sl::dvec3 &p) { return to_f(universe.position(body) + p - eye_world); };
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
            const float floor_size = glm::length(at) * 0.004f;
            m.texture = glow;
            m.tint = tint_of(colour, alpha);
            sl::draw_mesh(quad, facing_basis(at, std::max(size, floor_size)), m);
        };

        for (const Segment &s : beams_)
        {
            const glm::vec3 a = rel_of(s.body, s.a), b = rel_of(s.body, s.b);
            const double flicker = 0.9 + 0.1 * std::sin(a.x * 3.1 + a.z * 1.7 + b.y);
            glowing_strip(a, b, s.spec->look, flicker);
            round_glow(a, static_cast<float>(s.spec->look.width * s.spec->look.glow * 1.4), s.spec->look.colour, 0.8);
            if (s.ended)
                round_glow(b, static_cast<float>(s.spec->look.width * s.spec->look.glow * 2.2), s.spec->look.colour, 0.9);
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
            const sl::dvec3 dir = unit(b.velocity, sl::dvec3(0, 0, -1));
            const double length = b.burst ? look.length * 0.4 : look.length;
            const glm::vec3 head = rel_of(b.body, b.position), tail = rel_of(b.body, b.position - dir * length);
            if (glm::length(head) > 60000.0f)
                continue;
            glowing_strip(tail, head, look, 1.0);
            round_glow(head, static_cast<float>(look.width * look.glow * 1.3 * (b.burst ? 0.6 : 1.0)), look.colour, 0.9);
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
            const glm::vec3 a = rel_of(s.body, s.position), b = rel_of(s.body, s.position - unit(s.velocity, sl::dvec3(0, 1, 0)) * std::min(glm::length(s.velocity) * 0.04, 4.0));
            m.texture = beam_texture_;
            m.tint = tint_of(s.colour, fade);
            const float width = std::max(s.size, glm::length(a) * 0.002f);
            sl::draw_mesh(quad, strip_basis(a, b, width), m);
            round_glow(a, width * 1.6f, s.colour, 0.6 * fade);
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
            along(s, 1.0);
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
