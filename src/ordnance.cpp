#include "ordnance.h"

#include "props3d.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kosmos
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr double owner_grace = 1.5;
        constexpr std::size_t max_scars = 50;

        double ground_radius(const sl::Terrain *terrain, double body_radius, const sl::dvec3 &position)
        {
            const double d = glm::length(position);
            if (!terrain || d < 1.0)
                return body_radius;
            return body_radius + terrain->height_at_direction(position / d);
        }

        void put_le(std::vector<std::uint8_t> &out, std::uint32_t v, int bytes)
        {
            for (int i = 0; i < bytes; ++i)
                out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
        }

        // A blast as a mono WAV in memory. Kind 0 is a bomb: a sharp crack and a deep thump. Kind 1 is a nuclear burst: a long,
        // bassy, rolling rumble under the thump.
        std::vector<std::uint8_t> make_blast_wav(int kind)
        {
            const int rate = 22050;
            const double seconds = kind == 0 ? 5.0 : 16.0;
            const int count = static_cast<int>(rate * seconds);
            std::uint32_t seed = 4242u + 99u * static_cast<std::uint32_t>(kind);
            const auto noise = [&seed]
            {
                seed = seed * 1664525u + 1013904223u;
                return static_cast<double>(static_cast<std::int32_t>(seed)) / 2147483648.0;
            };
            std::vector<std::int16_t> pcm(static_cast<std::size_t>(count));
            double low1 = 0.0, low2 = 0.0, brown = 0.0, phase = 0.0, phase2 = 0.0;
            for (int i = 0; i < count; ++i)
            {
                const double t = static_cast<double>(i) / rate;
                // The thump: a sine that sinks in pitch as it dies away, with a second octave to carry on small speakers.
                const double f = kind == 0 ? 38.0 + 70.0 * std::exp(-t * 6.0) : 24.0 + 40.0 * std::exp(-t * 1.6);
                phase += 2.0 * pi * f / rate;
                phase2 += 2.0 * pi * f * 2.01 / rate;
                const double attack = kind == 0 ? 0.004 : 0.35;
                const double rise = std::min(1.0, t / attack);
                const double body = std::exp(-t / (kind == 0 ? 0.55 : 3.6));
                const double thump = (std::sin(phase) + 0.45 * std::sin(phase2) * std::exp(-t * 1.5)) * rise * body;

                // Rolling rumble: low-passed noise whose cutoff sinks, swelled by slow irregular waves.
                const double cutoff = (kind == 0 ? 240.0 : 150.0) * std::exp(-t * (kind == 0 ? 2.0 : 0.35)) + 40.0;
                const double a = 1.0 - std::exp(-2.0 * pi * cutoff / rate);
                const double white = noise();
                low1 += a * (white - low1);
                low2 += a * (low1 - low2);
                brown = brown * 0.999 + white * 0.02;
                const double roll = 0.7 + 0.3 * std::sin(2.0 * pi * 1.7 * t + 0.6) * std::sin(2.0 * pi * 0.63 * t + 1.9);
                const double rumble = (low2 * 5.0 + brown * 1.6) * roll * rise * std::exp(-t / (kind == 0 ? 1.3 : 6.0));
                const double crack = kind == 0 ? white * std::exp(-t * 40.0) * 0.5 : white * std::exp(-t * 14.0) * 0.12;

                const double v = (thump * 0.8 + rumble * 0.55 + crack) * 0.95;
                pcm[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(std::clamp(std::tanh(v * 1.4), -1.0, 1.0) * 30000.0);
            }
            std::vector<std::uint8_t> wav = {'R', 'I', 'F', 'F'};
            put_le(wav, 36u + static_cast<std::uint32_t>(count) * 2u, 4);
            for (char c : {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '})
                wav.push_back(static_cast<std::uint8_t>(c));
            put_le(wav, 16, 4);
            put_le(wav, 1, 2);
            put_le(wav, 1, 2);
            put_le(wav, static_cast<std::uint32_t>(rate), 4);
            put_le(wav, static_cast<std::uint32_t>(rate) * 2u, 4);
            put_le(wav, 2, 2);
            put_le(wav, 16, 2);
            for (char c : {'d', 'a', 't', 'a'})
                wav.push_back(static_cast<std::uint8_t>(c));
            put_le(wav, static_cast<std::uint32_t>(count) * 2u, 4);
            for (std::int16_t s : pcm)
                put_le(wav, static_cast<std::uint16_t>(s), 2);
            return wav;
        }

        sl::dvec3 unit(const sl::dvec3 &v, const sl::dvec3 &fallback)
        {
            const double l = glm::length(v);
            return l > 1e-9 ? v / l : fallback;
        }

        glm::vec3 to_f(const sl::dvec3 &v) { return glm::vec3(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)); }

        sl::dquat turn_to(const sl::dvec3 &from, const sl::dvec3 &to)
        {
            const double c = glm::dot(from, to);
            if (c < -0.99999)
                return glm::angleAxis(pi, unit(glm::cross(from, std::fabs(from.x) < 0.9 ? sl::dvec3(1, 0, 0) : sl::dvec3(0, 1, 0)), sl::dvec3(0, 0, 1)));
            return glm::normalize(sl::dquat(1.0 + c, glm::cross(from, to)));
        }

        /** A rough size for hit tests and blast distances: the half-length and radius of the stages' bounding box. */
        void ship_box(const Ship &ship, double &z_min, double &z_max, double &radius)
        {
            z_min = 1e9;
            z_max = -1e9;
            radius = 1.0;
            for (const StageSpec &st : ship.spec().stages)
            {
                z_min = std::min(z_min, st.z - 0.5 * st.length);
                z_max = std::max(z_max, st.z + 0.5 * st.length);
                radius = std::max(radius, st.radius);
            }
            if (z_min > z_max)
            {
                z_min = -3.0;
                z_max = 3.0;
            }
        }

        sl::dvec3 ship_local_position(const Universe &universe, const Ship &ship)
        {
            return ship.body().position - universe.position(ship.telemetry().soi);
        }

        const sl::Colour glow_colour(const EquipmentSpec &spec) { return spec.effect == "plasma" ? sl::Colour{90, 255, 190, 255} : sl::Colour{255, 214, 120, 255}; }
    } // namespace

    struct Arsenal::Meshes
    {
        sl::Mesh *cube = nullptr, *cylinder = nullptr, *sphere = nullptr, *bomb = nullptr, *fatman = nullptr;

        void build()
        {
            if (cube)
                return;
            cube = sl::create_cube_mesh(1.0f);
            sphere = sl::create_sphere_mesh(0.5f, 14, 8);
            sl::MeshData cyl;
            const int n = 16;
            for (int i = 0; i <= n; ++i)
            {
                const float a = 2.0f * static_cast<float>(pi) * i / n;
                const float c = std::cos(a), s = std::sin(a);
                for (int end = 0; end < 2; ++end)
                {
                    sl::Vertex3D v;
                    v.position[0] = 0.5f * c;
                    v.position[1] = 0.5f * s;
                    v.position[2] = end ? 0.5f : -0.5f;
                    v.normal[0] = c;
                    v.normal[1] = s;
                    v.normal[2] = 0.0f;
                    cyl.vertices.push_back(v);
                }
            }
            for (int i = 0; i < n; ++i)
            {
                const std::uint32_t a = static_cast<std::uint32_t>(2 * i), b = a + 1, c = a + 2, d = a + 3;
                cyl.indices.insert(cyl.indices.end(), {a, c, b, b, c, d});
            }
            for (int end = 0; end < 2; ++end)
            {
                const std::uint32_t centre = static_cast<std::uint32_t>(cyl.vertices.size());
                sl::Vertex3D v;
                v.position[2] = end ? 0.5f : -0.5f;
                v.normal[2] = end ? 1.0f : -1.0f;
                v.normal[1] = 0.0f;
                cyl.vertices.push_back(v);
                for (int i = 0; i < n; ++i)
                {
                    const std::uint32_t a = static_cast<std::uint32_t>(2 * i + end), c = static_cast<std::uint32_t>(2 * (i + 1) + end);
                    cyl.indices.insert(cyl.indices.end(), end ? std::initializer_list<std::uint32_t>{centre, a, c} : std::initializer_list<std::uint32_t>{centre, c, a});
                }
            }
            cylinder = sl::create_mesh(cyl.vertices, cyl.indices);
            const sl::MeshData b = sl::make_bomb_data();
            bomb = sl::create_mesh(b.vertices, b.indices);
            const sl::MeshData f = sl::make_fatman_data();
            fatman = sl::create_mesh(f.vertices, f.indices);
        }
    };

    Arsenal::Arsenal() : meshes_(new Meshes) {}
    // The blast samples are left for the process to reclaim: the audio worker may already be shut down when this runs.
    Arsenal::~Arsenal() = default;

    void Arsenal::reset()
    {
        pack_.clear();
        selected_ = -1;
        cooldown_ = recoil_ = 0.0;
        rounds_.clear();
        bangs_.clear();
        shocks_.clear();
        effects_.clear();
        scars_.clear();
        racks_.clear();
        detonations_ = 0;
        particles_.clear();
        particle_body_ = -1;
        readout_.clear();
        survey_.clear();
        hurt_ = 0.0;
        last_event_.clear();
        last_event_time_ = 0.0;
    }

    // ---- A person's equipment

    void Arsenal::equip_person(const Sim &sim, const Ship *home)
    {
        pack_.clear();
        const auto add = [&](const EquipmentSpec &spec)
        {
            for (Stack &s : pack_)
                if (s.id == spec.id)
                {
                    if (spec.ammo < 0 && spec.kind == EquipKind::weapon)
                        return;
                    s.count += spec.ammo > 0 ? spec.ammo : 1;
                    return;
                }
            int count = 1;
            if (spec.kind == EquipKind::weapon)
                count = spec.ammo;
            else if (spec.kind == EquipKind::explosive)
                count = spec.ammo > 0 ? spec.ammo : 3;
            pack_.push_back({spec.id, count});
        };
        for (const EquipmentSpec &spec : sim.equipment.all())
            if (spec.standard)
                add(spec);
        if (home)
            for (const std::string &id : home->spec().locker)
                if (const EquipmentSpec *spec = sim.equipment.find(id))
                    add(*spec);
        selected_ = pack_.empty() ? -1 : 0;
        readout_.clear();
    }

    void Arsenal::select(int slot)
    {
        if (slot < 0 || slot >= static_cast<int>(pack_.size()) || slot == selected_)
            return;
        selected_ = slot;
        readout_.clear();
        recoil_ = 0.6;
    }

    void Arsenal::cycle(int direction)
    {
        if (pack_.empty())
            return;
        const int n = static_cast<int>(pack_.size());
        select(((selected_ < 0 ? 0 : selected_) + direction + n) % n);
    }

    const EquipmentSpec *Arsenal::held(const Sim &sim) const
    {
        if (selected_ < 0 || selected_ >= static_cast<int>(pack_.size()))
            return nullptr;
        return sim.equipment.find(pack_[static_cast<std::size_t>(selected_)].id);
    }

    int Arsenal::held_count() const
    {
        if (selected_ < 0 || selected_ >= static_cast<int>(pack_.size()))
            return 0;
        return pack_[static_cast<std::size_t>(selected_)].count;
    }

    void Arsenal::use_held(ArsenalContext &context, bool pressed, bool down)
    {
        const EquipmentSpec *spec = held(*context.sim);
        Astronaut &eva = *context.eva;
        if (!spec || !eva.active || eva.body < 0)
            return;
        Stack &stack = pack_[static_cast<std::size_t>(selected_)];

        if (spec->kind == EquipKind::instrument)
        {
            if (pressed)
                instrument(context, *spec, true);
            return;
        }
        if (cooldown_ > 0.0 || !(down || pressed))
            return;

        const Universe &universe = context.sim->universe;
        const sl::dvec3 up = eva.local_up();
        const sl::dvec3 fwd = eva.forward();
        const sl::dvec3 right = glm::normalize(glm::cross(fwd, up));
        const sl::dvec3 eye = eva.position + up * (Astronaut::eye_height * (1.0 - 0.35 * eva.crouch));

        if (spec->kind == EquipKind::weapon)
        {
            if (spec->ammo >= 0 && stack.count <= 0)
            {
                if (pressed)
                {
                    last_event_ = spec->name + ": empty";
                    last_event_time_ = 0.0;
                }
                return;
            }
            if (spec->ammo >= 0)
                --stack.count;
            cooldown_ = 1.0 / std::max(spec->rate, 0.1);
            recoil_ = 1.0;

            sl::dvec3 aim = fwd;
            if (spec->spread > 0.0)
            {
                const auto noise = [&]() { return (static_cast<double>(std::rand()) / RAND_MAX - 0.5) * 2.0; };
                aim = unit(fwd + (right * noise() + glm::cross(right, fwd) * noise()) * spec->spread, fwd);
            }
            const sl::dvec3 muzzle = eye + fwd * 0.7 + right * 0.15 - up * 0.12;
            add_effect(universe, sl::Effect3DKind::muzzle_flash, eva.body, muzzle, spec->effect == "plasma" ? 1.6 : 1.0, aim);

            if (spec->fire == "bolt")
            {
                Round r;
                r.spec = spec;
                r.body = eva.body;
                r.position = muzzle;
                r.velocity = eva.velocity + aim * spec->speed;
                r.bolt = true;
                r.by_person = true;
                rounds_.push_back(r);
            }
            else
            {
                const Hit hit = trace(context, eva.body, eye, eye + aim * spec->range, nullptr);
                if (hit.kind != Hit::none)
                    impact(context, *spec, eva.body, hit.point, hit, true);
            }
            return;
        }

        if (spec->kind == EquipKind::explosive)
        {
            if (!pressed || stack.count <= 0)
                return;
            --stack.count;
            cooldown_ = 0.5;
            Round r;
            r.spec = spec;
            r.body = eva.body;
            r.position = eye + fwd * 0.6 - up * 0.2;
            r.velocity = eva.velocity + fwd * 12.0 + up * 2.0;
            r.fuse = spec->fuse_time;
            r.by_person = true;
            rounds_.push_back(r);
            recoil_ = 1.0;
            if (stack.count <= 0)
                cycle(1);
        }
    }

    // ---- Instruments

    void Arsenal::instrument(ArsenalContext &context, const EquipmentSpec &spec, bool pressed)
    {
        const Astronaut &eva = *context.eva;
        const Universe &universe = context.sim->universe;
        if (!eva.active || eva.body < 0)
            return;
        const Body &body = universe.bodies[static_cast<std::size_t>(eva.body)];
        const double r = glm::length(eva.position);
        const sl::Terrain *terrain = context.terrain ? context.terrain(eva.body) : nullptr;
        const double ground = ground_radius(terrain, body.radius, eva.position);
        char line[160];
        std::vector<std::string> lines;

        if (spec.sensor == "tricorder")
        {
            const double rho = universe.world.air_density_at(universe.position(eva.body) + eva.position);
            const double g = body.gm / (r * r);
            const double sun = glm::length(universe.position(eva.body) + eva.position - universe.position(0));
            const double temperature = 278.0 / std::sqrt(std::max(sun / 1.495978707e11, 0.01)) * (1.0 + std::min(rho, 5.0) * 0.12);
            std::snprintf(line, sizeof line, "BODY      %s", body.name.c_str());
            lines.push_back(line);
            std::snprintf(line, sizeof line, "ELEVATION %.0f m", r - body.radius);
            lines.push_back(line);
            std::snprintf(line, sizeof line, "GRAVITY   %.2f m/s2", g);
            lines.push_back(line);
            std::snprintf(line, sizeof line, "AIR       %.4f kg/m3", rho);
            lines.push_back(line);
            std::snprintf(line, sizeof line, "TEMP      %.0f K (est)", temperature);
            lines.push_back(line);
            lines.push_back(rho > 0.6 && rho < 1.6 ? "ATMOS     BREATHABLE DENSITY" : rho > 0.001 ? "ATMOS     NOT BREATHABLE" : "ATMOS     VACUUM");
            readout_ = lines;
            return;
        }
        if (!pressed)
            return;

        if (spec.sensor == "geology")
        {
            const sl::dvec3 up = unit(eva.position, sl::dvec3(0, 1, 0));
            const double h = terrain ? terrain->height_at_direction(up) : 0.0;
            const sl::dvec3 ahead = unit(eva.position + (eva.forward() - up * glm::dot(eva.forward(), up)) * 10.0, up);
            const double dh = terrain ? terrain->height_at_direction(ahead) - h : 0.0;
            const double slope = std::atan2(std::fabs(dh), 10.0) * 180.0 / pi;
            const double rho = universe.world.air_density_at(universe.position(eva.body) + eva.position);
            const char *kinds[] = {"basaltic regolith", "silicate sand", "iron-rich dust", "fractured bedrock", "carbonaceous clay", "ice-bonded gravel"};
            const std::size_t pick = static_cast<std::size_t>(std::fabs(std::floor(h * 0.07 + eva.position.x * 0.0005 + eva.position.z * 0.0007))) % 6;
            lines.push_back("GEOLOGY SAMPLE");
            std::snprintf(line, sizeof line, "SURFACE   %s", rho > 0.5 && eva.body >= 0 && body.name == "Earth" ? "soil and rock" : kinds[pick]);
            lines.push_back(line);
            std::snprintf(line, sizeof line, "HEIGHT    %.1f m   SLOPE %.0f deg", h, slope);
            lines.push_back(line);
            std::snprintf(line, sizeof line, "DENSITY   %.0f kg/m3", 1200.0 + std::fmod(std::fabs(h) * 7.0, 1800.0));
            lines.push_back(line);
            readout_ = lines;
            Sample s;
            s.text = readout_[1];
            s.body = eva.body;
            s.position = eva.position;
            survey_.push_back(s);
            last_event_ = "Sample logged";
            last_event_time_ = 0.0;
            return;
        }
        if (spec.sensor == "survey")
        {
            Sample s;
            s.body = eva.body;
            s.position = eva.position * (ground / std::max(r, 1.0));
            lines.push_back("SURVEY STATION " + std::to_string(survey_.size() + 1));
            std::snprintf(line, sizeof line, "ELEVATION %.1f m", glm::length(s.position) - body.radius);
            lines.push_back(line);
            if (!survey_.empty() && survey_.back().body == eva.body)
            {
                const sl::dvec3 d = s.position - survey_.back().position;
                const double dist = glm::length(d);
                const sl::dvec3 up = unit(s.position, sl::dvec3(0, 1, 0));
                const double rise = glm::dot(d, up);
                std::snprintf(line, sizeof line, "FROM LAST  %.1f m   RISE %+.1f m", dist, rise);
                lines.push_back(line);
            }
            s.text = lines[0];
            survey_.push_back(s);
            readout_ = lines;
            last_event_ = "Survey point marked";
            last_event_time_ = 0.0;
        }
    }

    // ---- Racks

    Arsenal::Rack &Arsenal::rack_of(const Ship &ship)
    {
        for (Rack &r : racks_)
            if (r.ship == &ship)
                return r;
        Rack r;
        r.ship = &ship;
        for (const ShipSpec::Hardpoint &hp : ship.spec().hardpoints)
            r.counts.push_back(hp.count);
        racks_.push_back(r);
        return racks_.back();
    }

    int Arsenal::rack_count(const Ship &ship, std::size_t index)
    {
        Rack &r = rack_of(ship);
        return index < r.counts.size() ? r.counts[index] : 0;
    }

    int Arsenal::rack_total(const Ship &ship)
    {
        int n = 0;
        for (int c : rack_of(ship).counts)
            n += c;
        return n;
    }

    void Arsenal::sync_payload(const Sim &sim, Ship &ship, Rack &rack)
    {
        double mass = 0.0;
        for (std::size_t i = 0; i < rack.counts.size() && i < ship.spec().hardpoints.size(); ++i)
            if (const EquipmentSpec *spec = sim.equipment.find(ship.spec().hardpoints[i].equipment))
                mass += spec->mass * rack.counts[i];
        if (std::fabs(mass - rack.synced_mass) > 1e-6)
        {
            ship.set_payload_mass(mass);
            rack.synced_mass = mass;
        }
    }

    bool Arsenal::release(ArsenalContext &context, Ship &ship, int index)
    {
        if (ship.spec().hardpoints.empty() || ship.health() <= 0.0)
            return false;
        Rack &rack = rack_of(ship);
        if (index < 0)
        {
            for (std::size_t i = 0; i < rack.counts.size(); ++i)
                if (rack.counts[i] > 0)
                {
                    index = static_cast<int>(i);
                    break;
                }
        }
        if (index < 0 || index >= static_cast<int>(rack.counts.size()) || rack.counts[static_cast<std::size_t>(index)] <= 0)
            return false;
        const ShipSpec::Hardpoint &hp = ship.spec().hardpoints[static_cast<std::size_t>(index)];
        const EquipmentSpec *spec = context.sim->equipment.find(hp.equipment);
        if (!spec)
            return false;
        if (spec->blast_effect == "mushroom" && !nuke_tried_ && sound_on_)
        {
            nuke_tried_ = true;
            const std::string path = context.sim->root() + "/data/reference/nuke.mp3";
            if (FILE *f = std::fopen(path.c_str(), "rb"))
            {
                std::vector<std::uint8_t> bytes;
                std::uint8_t buffer[65536];
                for (std::size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) > 0;)
                    bytes.insert(bytes.end(), buffer, buffer + n);
                std::fclose(f);
                nuke_sample_ = sl::load_sample_from_memory(bytes.data(), bytes.size());
            }
        }
        --rack.counts[static_cast<std::size_t>(index)];
        sync_payload(*context.sim, ship, rack);

        const Universe &universe = context.sim->universe;
        const int soi = ship.telemetry().soi;
        const sl::dvec3 world = ship.origin() + ship.body().direction_to_world(hp.position);
        const sl::dvec3 down = ship.body().direction_to_world(sl::dvec3(0.0, -1.0, 0.0));
        Round r;
        r.spec = spec;
        r.body = soi;
        r.position = world - universe.position(soi);
        r.velocity = ship.point_velocity(world) - universe.world.body(soi)->velocity + down * 1.5;
        r.orientation = turn_to(sl::dvec3(0, 1, 0), unit(r.velocity, down));
        r.fuse = spec->fuse_time;
        r.owner = &ship;
        rounds_.push_back(r);
        last_event_ = spec->name + " away";
        last_event_time_ = 0.0;
        return true;
    }

    // ---- The world

    bool Arsenal::anchor_for(int body, const sl::dvec3 &position)
    {
        if (particles_.size() == 0 && effects_.empty())
        {
            particle_body_ = body;
            anchor_ = position;
            return true;
        }
        return body == particle_body_;
    }

    void Arsenal::add_effect(const Universe &universe, sl::Effect3DKind kind, int body, const sl::dvec3 &position, double scale, const sl::dvec3 &direction)
    {
        (void)universe;
        if (body < 0 || !anchor_for(body, position))
            return;
        const sl::dvec3 up = unit(position, sl::dvec3(0, 1, 0));
        // One standing cloud at a time: a new one replaces the old, which then thins away with its remaining particles.
        if (kind == sl::Effect3DKind::mushroom)
            effects_.erase(std::remove_if(effects_.begin(), effects_.end(), [](const Active &a) { return a.cloud; }), effects_.end());
        effects_.push_back({sl::Effect3D(kind, to_f(position - anchor_), to_f(up), static_cast<float>(scale), static_cast<std::uint32_t>(detonations_ * 31 + effects_.size() + 7),
                                         glm::length(direction) > 0.0 ? to_f(unit(direction, up)) : glm::vec3(0.0f)),
                            body, kind == sl::Effect3DKind::mushroom});
    }

    Arsenal::Hit Arsenal::trace(ArsenalContext &context, int body, const sl::dvec3 &from, const sl::dvec3 &to, const Ship *ignore) const
    {
        Hit hit;
        Sim &sim = *context.sim;
        const Universe &universe = sim.universe;
        const sl::Terrain *terrain = context.terrain ? context.terrain(body) : nullptr;
        const double body_radius = universe.bodies[static_cast<std::size_t>(body)].radius;
        const sl::dvec3 planet = universe.position(body);

        struct Candidate
        {
            Ship *ship;
            double z_min, z_max, radius;
        };
        std::vector<Candidate> targets;
        for (int i = 0; i < sim.ship_count(); ++i)
        {
            Ship *s = sim.ship(i);
            if (s == ignore || s->telemetry().soi != body)
                continue;
            Candidate c{s, 0, 0, 0};
            ship_box(*s, c.z_min, c.z_max, c.radius);
            targets.push_back(c);
        }

        const double length = glm::length(to - from);
        const int steps = std::clamp(static_cast<int>(std::ceil(length / 1.0)), 1, 1500);
        for (int i = 1; i <= steps; ++i)
        {
            const sl::dvec3 p = from + (to - from) * (static_cast<double>(i) / steps);
            const double ground = ground_radius(terrain, body_radius, p);
            if (glm::length(p) <= ground)
            {
                hit.kind = Hit::ground;
                hit.point = unit(p, sl::dvec3(0, 1, 0)) * ground;
                return hit;
            }
            for (const Candidate &c : targets)
            {
                const sl::dvec3 local = c.ship->body().to_local(planet + p) + c.ship->com_offset();
                if (std::fabs(local.x) <= c.radius + 0.3 && std::fabs(local.y) <= c.radius + 0.3 && local.z >= c.z_min - 0.3 && local.z <= c.z_max + 0.3)
                {
                    hit.kind = Hit::ship;
                    hit.point = p;
                    hit.target = c.ship;
                    return hit;
                }
            }
        }
        return hit;
    }

    void Arsenal::damage_person(ArsenalContext &context, double amount)
    {
        Suit &suit = context.eva->suit;
        if (amount <= 0.0 || !context.eva->active)
            return;
        suit.health = std::max(0.0, suit.health - amount);
        hurt_ = std::min(1.0, hurt_ + 0.4 + amount);
    }

    void Arsenal::impact(ArsenalContext &context, const EquipmentSpec &spec, int body, const sl::dvec3 &point, const Hit &hit, bool by_person)
    {
        const Universe &universe = context.sim->universe;
        const sl::dvec3 up = unit(point, sl::dvec3(0, 1, 0));
        if (spec.effect == "plasma")
            add_effect(universe, sl::Effect3DKind::plasma, body, point + up * 0.1, 1.0, up);
        else
            add_effect(universe, sl::Effect3DKind::smoke, body, point + up * 0.1, 0.5, up);
        if (hit.kind == Hit::ship && hit.target)
            hit.target->damage(spec.ship_damage);
        Astronaut &eva = *context.eva;
        if (!by_person && eva.active && eva.body == body && glm::length(eva.position + up * 1.0 - point) < 1.0)
            damage_person(context, spec.damage);
    }

    void Arsenal::detonate(ArsenalContext &context, const Round &round, const sl::dvec3 &point)
    {
        const EquipmentSpec &spec = *round.spec;
        const Universe &universe = context.sim->universe;
        const bool nuclear = spec.blast_effect == "mushroom";
        ++detonations_;
        if (sound_on_)
        {
            const double d = have_eye_ ? glm::length(universe.position(round.body) + point - eye_world_) : 0.0;
            Bang bang;
            bang.kind = nuclear ? 1 : 0;
            bang.delay = nuclear && nuke_sample_ ? 0.0 : d / 340.0;
            const double reach = nuclear ? 80000.0 : std::max(4000.0, spec.blast_radius * 60.0);
            bang.volume = nuclear && nuke_sample_ ? 1.0 : std::clamp(1.0 / (1.0 + d / reach), 0.0, 1.0);
            if (bang.volume > 0.02)
                bangs_.push_back(bang);
        }
        const sl::Terrain *terrain = context.terrain ? context.terrain(round.body) : nullptr;
        const double body_radius = universe.bodies[static_cast<std::size_t>(round.body)].radius;
        const double ground = ground_radius(terrain, body_radius, point);
        const double height = glm::length(point) - ground;
        const sl::dvec3 up = unit(point, sl::dvec3(0, 1, 0));
        const sl::dvec3 base = up * ground;

        if (nuclear)
            add_effect(universe, sl::Effect3DKind::mushroom, round.body, height > 5.0 ? base : point, spec.yield);
        else
            add_effect(universe, sl::Effect3DKind::explosion, round.body, point, std::max(2.0, spec.blast_radius * 0.25));

        Shock shock;
        shock.spec = &spec;
        shock.body = round.body;
        shock.centre = point;
        shock.speed = nuclear ? 600.0 : 340.0;
        shocks_.push_back(shock);

        if (height < spec.blast_radius + 30.0 || nuclear)
        {
            const double r = nuclear ? std::min(spec.blast_radius * 0.25, 600.0) : std::max(2.0, spec.blast_radius * 0.15);
            scars_.push_back({round.body, base, r});
            if (scars_.size() > max_scars)
                scars_.erase(scars_.begin());
        }

        Astronaut &eva = *context.eva;
        if (nuclear && spec.thermal_radius > 0.0 && eva.active && eva.body == round.body && glm::length(eva.position - point) < spec.thermal_radius)
            damage_person(context, 2.0);
    }

    void Arsenal::update(ArsenalContext &context, double dt, double real_dt)
    {
        Sim &sim = *context.sim;
        Universe &universe = sim.universe;
        cooldown_ = std::max(0.0, cooldown_ - real_dt);
        recoil_ = std::max(0.0, recoil_ - real_dt * 6.0);
        hurt_ = std::max(0.0, hurt_ - real_dt * 0.6);
        last_event_time_ += real_dt;

        // Racks of ships that are gone are dropped; the rest keep their payload mass in step.
        racks_.erase(std::remove_if(racks_.begin(), racks_.end(),
                                    [&](const Rack &r)
                                    {
                                        for (int i = 0; i < sim.ship_count(); ++i)
                                            if (sim.ship(i) == r.ship)
                                                return false;
                                        return true;
                                    }),
                     racks_.end());
        for (int i = 0; i < sim.ship_count(); ++i)
            if (!sim.ship(i)->spec().hardpoints.empty())
                sync_payload(sim, *sim.ship(i), rack_of(*sim.ship(i)));

        // Things fixed to the ground (burst clouds, scorch marks, blast centres, charges at rest) turn with the planet.
        if (dt > 0.0)
        {
            const auto spin_of = [&](int body, const sl::dvec3 &p)
            {
                const sl::FlightBody &pb = *universe.world.body(body);
                const double angle = pb.spin_rate * dt;
                return std::fabs(angle) > 0.0 ? sl::dvec3(glm::angleAxis(angle, pb.spin_axis) * p) : p;
            };
            if (particle_body_ >= 0 && (particles_.size() > 0 || !effects_.empty()))
                anchor_ = spin_of(particle_body_, anchor_);
            for (Scar &sc : scars_)
                sc.position = spin_of(sc.body, sc.position);
            for (Shock &sh : shocks_)
                sh.centre = spin_of(sh.body, sh.centre);
            for (Round &r : rounds_)
                if (r.stuck)
                    r.position = spin_of(r.body, r.position);
            for (Sample &sm : survey_)
                if (sm.body >= 0)
                    sm.position = spin_of(sm.body, sm.position);
        }

        if (context.eva->active && held(sim) && held(sim)->sensor == "tricorder")
            instrument(context, *held(sim), false);

        if (dt > 0.0)
        {
            std::vector<Round> detonating;
            for (std::size_t i = 0; i < rounds_.size();)
            {
                Round &r = rounds_[i];
                const EquipmentSpec &spec = *r.spec;
                bool remove = false, boom = false;
                sl::dvec3 boom_at = r.position;
                const sl::FlightBody &pb = *universe.world.body(r.body);
                const double gm = universe.bodies[static_cast<std::size_t>(r.body)].gm;
                const sl::Terrain *terrain = context.terrain ? context.terrain(r.body) : nullptr;
                const double body_radius = universe.bodies[static_cast<std::size_t>(r.body)].radius;
                r.age += dt;
                if (r.stuck)
                {
                    r.fuse -= dt;
                    boom = r.fuse <= 0.0;
                    remove = boom;
                }
                else
                {
                    const int n = std::clamp(static_cast<int>(std::ceil(glm::length(r.velocity) * dt / 3.0)), 1, 200);
                    const double h = dt / n;
                    for (int s = 0; s < n && !remove; ++s)
                    {
                        const sl::dvec3 from = r.position;
                        if (!r.bolt)
                        {
                            const double d = glm::length(r.position);
                            r.velocity -= r.position / d * (gm / (d * d)) * h;
                            const double rho = universe.world.air_density_at(pb.position + r.position);
                            const sl::dvec3 air = r.velocity - glm::cross(pb.spin_axis * pb.spin_rate, r.position);
                            const double v = glm::length(air);
                            if (rho > 0.0 && v > 0.0 && spec.mass > 0.0)
                                r.velocity -= air * (0.5 * rho * v * spec.drag_area / spec.mass * h);
                            if (!spec.prop.empty() && glm::length(air) > 5.0)
                            {
                                const sl::dquat want = turn_to(sl::dvec3(0, 1, 0), unit(air, sl::dvec3(0, -1, 0)));
                                r.orientation = glm::slerp(r.orientation, want, std::min(1.0, h * 3.0));
                            }
                        }
                        r.position += r.velocity * h;
                        const Hit hit = trace(context, r.body, from, r.position, r.age < owner_grace ? r.owner : nullptr);
                        if (r.bolt)
                        {
                            if (hit.kind != Hit::none)
                            {
                                impact(context, spec, r.body, hit.point, hit, r.by_person);
                                remove = true;
                            }
                            else if (r.age * spec.speed > spec.range)
                                remove = true;
                            else if (anchor_for(r.body, r.position))
                            {
                                sl::Particle3D p;
                                p.position = to_f(r.position - anchor_);
                                p.life = 0.35f;
                                p.size_start = 0.35f;
                                p.size_end = 0.05f;
                                p.colour_start = {120, 255, 200, 200};
                                p.colour_end = {20, 120, 255, 0};
                                p.additive = true;
                                particles_.spawn(p);
                            }
                            continue;
                        }
                        if (spec.fuse == "airburst")
                        {
                            const double gr = ground_radius(terrain, body_radius, r.position);
                            if (glm::length(r.position) - gr <= spec.burst_height && r.age > 2.0)
                            {
                                boom = remove = true;
                                boom_at = r.position;
                                continue;
                            }
                        }
                        if (hit.kind != Hit::none)
                        {
                            if (spec.fuse == "timer")
                            {
                                r.position = hit.point + unit(hit.point, sl::dvec3(0, 1, 0)) * 0.1;
                                r.velocity = sl::dvec3(0.0);
                                r.stuck = true;
                                break;
                            }
                            boom = remove = true;
                            boom_at = hit.point;
                            if (hit.kind == Hit::ship && hit.target && spec.fuse != "timer")
                                boom_at = hit.point;
                            break;
                        }
                        if (spec.fuse == "timer")
                        {
                            r.fuse -= h;
                            if (r.fuse <= 0.0)
                                boom = remove = true;
                        }
                    }
                }
                if (r.age > 900.0)
                    remove = true;
                if (boom)
                    detonating.push_back(r), detonating.back().position = boom_at;
                if (remove)
                    rounds_.erase(rounds_.begin() + static_cast<std::ptrdiff_t>(i));
                else
                    ++i;
            }
            for (const Round &r : detonating)
                detonate(context, r, r.position);

            // Blast waves spread, damaging each thing once as they reach it.
            for (Shock &s : shocks_)
            {
                s.radius += s.speed * dt;
                const double reach = std::min(s.radius, s.spec->blast_radius);
                const sl::dvec3 planet = universe.position(s.body);
                for (int i = 0; i < sim.ship_count(); ++i)
                {
                    Ship *ship = sim.ship(i);
                    if (ship->telemetry().soi != s.body || s.hit.count(ship))
                        continue;
                    double z0, z1, rad;
                    ship_box(*ship, z0, z1, rad);
                    const double extent = std::max(0.5 * (z1 - z0), rad);
                    const double d = std::max(0.0, glm::length(ship_local_position(universe, *ship) - s.centre) - extent);
                    if (d <= reach)
                    {
                        s.hit.insert(ship);
                        ship->damage(s.spec->blast_damage * (1.0 - std::clamp(d / s.spec->blast_radius, 0.0, 1.0)));
                    }
                }
                Astronaut &eva = *context.eva;
                if (eva.active && eva.body == s.body && !s.hit_person)
                {
                    const double d = glm::length(eva.position + eva.local_up() - s.centre);
                    if (d <= reach)
                    {
                        s.hit_person = true;
                        damage_person(context, 2.0 * (1.0 - std::clamp(d / s.spec->blast_radius, 0.0, 1.0)));
                    }
                }
            }
            shocks_.erase(std::remove_if(shocks_.begin(), shocks_.end(), [](const Shock &s) { return s.radius > s.spec->blast_radius; }), shocks_.end());

            for (std::size_t i = 0; i < bangs_.size();)
            {
                bangs_[i].delay -= dt;
                if (bangs_[i].delay > 0.0)
                {
                    ++i;
                    continue;
                }
                sl::Sample *&sample = bangs_[i].kind == 1 && nuke_sample_ ? nuke_sample_ : bang_samples_[bangs_[i].kind];
                if (!sample)
                {
                    const std::vector<std::uint8_t> wav = make_blast_wav(bangs_[i].kind);
                    sample = sl::load_sample_from_memory(wav.data(), wav.size());
                }
                if (sample)
                    sl::play_sample(sample, static_cast<int>(255 * bangs_[i].volume), 128, 1000, 0);
                bangs_.erase(bangs_.begin() + static_cast<std::ptrdiff_t>(i));
            }

            for (Active &a : effects_)
                a.effect.update(particles_, static_cast<float>(dt));
            effects_.erase(std::remove_if(effects_.begin(), effects_.end(), [](const Active &a) { return a.effect.done(); }), effects_.end());
            particles_.update(static_cast<float>(dt));
        }
    }

    // ---- Drawing

    const sl::Mesh *Arsenal::round_mesh(const std::string &prop, bool box)
    {
        meshes_->build();
        if (prop == "bomb")
            return meshes_->bomb;
        if (prop == "fatman")
            return meshes_->fatman;
        return box ? meshes_->cube : meshes_->sphere;
    }

    void Arsenal::draw_world(const Universe &universe, const sl::dvec3 &eye_world, const sl::Mesh *unit_sphere)
    {
        meshes_->build();
        eye_world_ = eye_world;
        have_eye_ = true;
        for (const Round &r : rounds_)
        {
            const EquipmentSpec &spec = *r.spec;
            const sl::dvec3 rel = universe.position(r.body) + r.position - eye_world;
            if (glm::length(rel) > 20000.0)
                continue;
            glm::mat4 model = glm::translate(glm::mat4(1.0f), to_f(rel));
            sl::Material m;
            if (r.bolt)
            {
                const glm::vec3 dir = to_f(unit(r.velocity, sl::dvec3(0, 0, -1)));
                model *= glm::mat4_cast(glm::quat(glm::vec3(0, 0, 1), dir)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.35f, 0.35f, 1.6f));
                m.tint = glow_colour(spec);
                m.lit = false;
                m.fog = false;
                m.blend = sl::MaterialBlend::additive;
                sl::draw_mesh(unit_sphere, model * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f)), m);
                continue;
            }
            if (!spec.prop.empty())
            {
                const bool fat = spec.prop == "fatman";
                const float rs = static_cast<float>(spec.radius / (fat ? 0.75 : 0.25)), ls = static_cast<float>(spec.length / (fat ? 3.3 : 3.0));
                model *= glm::mat4_cast(glm::quat(static_cast<float>(r.orientation.w), static_cast<float>(r.orientation.x), static_cast<float>(r.orientation.y), static_cast<float>(r.orientation.z))) *
                         glm::scale(glm::mat4(1.0f), glm::vec3(rs, ls, rs));
                sl::draw_mesh(round_mesh(spec.prop, false), model, m);
                continue;
            }
            // A thrown charge: a small block with a light that blinks faster as the fuse runs down.
            sl::draw_mesh(meshes_->cube, model * glm::scale(glm::mat4(1.0f), glm::vec3(0.18f, 0.12f, 0.12f)), [&] { m.tint = {70, 80, 60, 255}; return m; }());
            const double blink = r.fuse < 1.0 ? 0.15 : 0.5;
            if (std::fmod(r.age, blink * 2.0) < blink)
            {
                sl::Material led;
                led.tint = {255, 40, 20, 255};
                led.lit = false;
                led.fog = false;
                led.blend = sl::MaterialBlend::additive;
                sl::draw_mesh(unit_sphere, model * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.08f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.08f)), led);
            }
        }

        for (const Scar &s : scars_)
        {
            const sl::dvec3 rel = universe.position(s.body) + s.position - eye_world;
            if (glm::length(rel) > 30000.0)
                continue;
            const glm::vec3 up = to_f(unit(s.position, sl::dvec3(0, 1, 0)));
            sl::Material m;
            m.tint = {18, 16, 14, 190};
            m.lit = false;
            m.cull = sl::CullMode::none;
            m.blend = sl::MaterialBlend::alpha;
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), to_f(rel) + up * 0.15f) * glm::mat4_cast(glm::quat(glm::vec3(0, 1, 0), up)) *
                                    glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(s.radius) * 2.0f, 0.05f, static_cast<float>(s.radius) * 2.0f));
            sl::draw_mesh(meshes_->sphere, model, m);
        }

        if (particle_body_ >= 0 && particles_.size() > 0)
        {
            const sl::dvec3 origin = universe.position(particle_body_) + anchor_ - eye_world;
            particles_.draw(unit_sphere, glm::translate(glm::mat4(1.0f), to_f(origin)));
        }
    }

    void Arsenal::draw_held(const Sim &sim, const glm::mat4 &eye_to_world)
    {
        const EquipmentSpec *spec = held(sim);
        if (!spec)
            return;
        meshes_->build();
        const glm::mat4 kick = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.01f * static_cast<float>(recoil_), 0.06f * static_cast<float>(recoil_)));
        for (const ViewShape &s : spec->shapes)
        {
            sl::Material m;
            m.tint = {s.colour[0], s.colour[1], s.colour[2], 255};
            m.lit = !s.glow;
            m.fog = false;
            glm::mat4 model = eye_to_world * kick * glm::translate(glm::mat4(1.0f), s.position);
            const sl::Mesh *mesh = meshes_->cube;
            glm::vec3 scale = s.size;
            if (s.type == ViewShape::cylinder)
                mesh = meshes_->cylinder;
            else if (s.type == ViewShape::sphere)
            {
                mesh = meshes_->sphere;
                scale = glm::vec3(s.size.x);
            }
            if (s.glow)
            {
                m.blend = sl::MaterialBlend::additive;
                m.tint.alpha = 255;
            }
            sl::draw_mesh(mesh, model * glm::scale(glm::mat4(1.0f), scale), m);
        }
    }

    double Arsenal::flash(const Universe &universe, const sl::dvec3 &eye_world) const
    {
        double f = 0.0;
        for (const Active &a : effects_)
        {
            const double radius = a.effect.light_radius();
            if (radius <= 0.0)
                continue;
            const sl::dvec3 at = universe.position(a.body) + anchor_ + sl::dvec3(a.effect.position().x, a.effect.position().y, a.effect.position().z);
            const double d = glm::length(at - eye_world);
            if (d < radius)
                f = std::max(f, static_cast<double>(a.effect.light()) * (1.0 - d / radius));
        }
        return std::clamp(f, 0.0, 1.0);
    }

    void Arsenal::draw_hud(const Sim &sim, const Astronaut &eva, int screen_w, int screen_h, const sl::dvec3 &eye_world)
    {
        const double white = flash(sim.universe, eye_world);
        if (white > 0.01)
            sl::rectfill(sl::screen, 0.0f, 0.0f, static_cast<float>(screen_w), static_cast<float>(screen_h), {255, 250, 235, static_cast<Uint8>(std::min(255.0, white * 255.0))});
        if (hurt_ > 0.01)
            sl::rectfill(sl::screen, 0.0f, 0.0f, static_cast<float>(screen_w), static_cast<float>(screen_h), {200, 0, 0, static_cast<Uint8>(std::min(160.0, hurt_ * 160.0))});
        if (!eva.active)
            return;

        const EquipmentSpec *spec = held(sim);
        const sl::Colour green{140, 255, 170, 255}, dim{90, 150, 110, 255};
        const int cx = screen_w / 2, cy = screen_h / 2;
        if (spec && spec->kind == EquipKind::weapon)
        {
            sl::line(sl::screen, cx - 9, cy, cx - 3, cy, green);
            sl::line(sl::screen, cx + 3, cy, cx + 9, cy, green);
            sl::line(sl::screen, cx, cy - 9, cx, cy - 3, green);
            sl::line(sl::screen, cx, cy + 3, cx, cy + 9, green);
        }
        else if (spec)
            sl::line(sl::screen, cx - 2, cy, cx + 2, cy, dim);

        int y = screen_h - 26 - 18 * static_cast<int>(pack_.size());
        for (std::size_t i = 0; i < pack_.size(); ++i, y += 18)
        {
            const EquipmentSpec *item = sim.equipment.find(pack_[i].id);
            if (!item)
                continue;
            char count[24] = "";
            if (item->kind == EquipKind::explosive || (item->kind == EquipKind::weapon && item->ammo >= 0))
                std::snprintf(count, sizeof count, " x%d", pack_[i].count);
            sl::gprintf(screen_w - 230, y, static_cast<int>(i) == selected_ ? green : dim, "%zu %s%s", i + 1, item->name.c_str(), count);
        }
        if (spec)
            sl::gprintf(screen_w - 230, screen_h - 24, green, "%s", spec->description.substr(0, 28).c_str());

        int ry = 120;
        for (const std::string &line : readout_)
        {
            sl::gprintf(20, ry, green, "%s", line.c_str());
            ry += 16;
        }
        if (!last_event_.empty() && last_event_time_ < 3.0)
            sl::gprintf_center(screen_h / 2 + 60, green, "%s", last_event_.c_str());
    }
} // namespace kosmos
