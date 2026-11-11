#pragma once

#include "app.h"
#include "rcs_visual.h"

namespace kosmos::app
{
    // Exhaust as glowing particles that live in the ship's frame, each a hot core in a fainter halo (as in the exreel orbit scene).
    struct ExhaustFx
    {
        enum Kind : std::uint8_t { kerosene, hydrogen, methane, hypergolic, solid, nuclear, scramjet, afterburner, rcs, kinds, none = 255 };

        // How each plume looks: length and width relative to the engine's `plume`, the particle lifetime, and the colours
        // of the young core, the cooled tail and the halo.
        struct Style
        {
            float length, width, life, spread, halo;
            sl::Colour core, tail, glow;
        };

        static const Style &style(int kind)
        {
            static const Style table[kinds] = {
                {1.0f, 1.0f, 1.0f, 1.0f, 0.10f, {255, 235, 190, 255}, {255, 120, 40, 255}, {255, 110, 40, 255}},    // kerosene
                {1.7f, 0.55f, 0.9f, 0.5f, 0.07f, {235, 242, 255, 255}, {255, 165, 90, 255}, {120, 150, 255, 255}},  // hydrogen
                {1.15f, 0.8f, 1.0f, 0.8f, 0.09f, {255, 240, 225, 255}, {255, 150, 70, 255}, {110, 145, 255, 255}},  // methane
                {0.8f, 0.85f, 0.9f, 0.8f, 0.06f, {255, 225, 190, 255}, {255, 150, 90, 255}, {255, 150, 80, 255}},   // hypergolic
                {0.95f, 1.5f, 1.35f, 1.6f, 0.16f, {255, 245, 205, 255}, {255, 85, 20, 255}, {255, 90, 30, 255}},    // solid
                {1.7f, 0.55f, 0.9f, 0.5f, 0.07f, {220, 160, 255, 255}, {160, 75, 255, 255}, {140, 70, 255, 255}},   // nuclear
                {1.3f, 0.5f, 0.9f, 0.35f, 0.14f, {205, 228, 255, 255}, {70, 120, 255, 255}, {60, 110, 255, 255}},   // scramjet
                {1.1f, 0.5f, 0.8f, 0.4f, 0.12f, {255, 235, 190, 255}, {255, 110, 40, 255}, {255, 120, 50, 255}},    // afterburner
                {1.0f, 1.0f, 1.0f, 1.0f, 0.10f, {255, 235, 190, 255}, {255, 120, 40, 255}, {255, 110, 40, 255}},    // rcs
            };
            return table[kind];
        }

        // Which plume an engine makes, or none: jets are invisible unless the afterburner is lit, and a hyperjump drive has no plume.
        static int plume_kind(const kosmos::EngineSpec &e, double level)
        {
            using kosmos::EngineType;
            switch (e.type)
            {
            case EngineType::jet: return e.afterburner && level > 0.8 ? afterburner : none;
            case EngineType::scramjet: return scramjet;
            case EngineType::solid: return solid;
            case EngineType::nuclear: return nuclear;
            case EngineType::hyperjump: return none;
            case EngineType::rocket: break;
            }
            if (e.fuel == "hydrogen") return hydrogen;
            if (e.fuel == "methane") return methane;
            if (e.fuel == "hypergolic") return hypergolic;
            if (e.fuel == "solid") return solid;
            return kerosene;
        }

        // A shock diamond: a bright knot standing in the plume of a scramjet or an afterburner.
        struct Diamond
        {
            glm::vec3 position, axis;
            float size, strength;
            std::uint8_t kind;
        };

        struct Particle
        {
            glm::vec3 position, velocity;
            float age, life, size, body = 0.55f, bright = 1.0f, stretch = 1.0f;
            std::uint8_t kind = kerosene;
            bool splashed = false;
        };
        std::vector<Particle> particles;
        std::vector<Diamond> diamonds;
        const kosmos::Ship *owner = nullptr;
        std::uint32_t seed = 1;
        float carry_main = 0.0f, carry_rcs[9] = {};

        float random01()
        {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float>(seed >> 8) / 16777216.0f;
        }

        void update(const kosmos::Ship &ship, double vacuum, float dt, bool spawn, bool has_ground = false, float ground_up_height = 0.0f, glm::vec3 up = glm::vec3(0, 1, 0))
        {
            if (owner != &ship)
            {
                particles.clear();
                owner = &ship;
            }
            for (Particle &p : particles)
            {
                p.age += dt;
                p.position += p.velocity * dt;
                if (has_ground)
                {
                    // Fake ground splash: the plume cannot pass the ground plane, so it is turned and sprayed along it.
                    const float height = ground_up_height + glm::dot(up, p.position - glm::vec3(ship.com_offset()));
                    if (height < 0.2f)
                    {
                        p.position += up * (0.2f - height);
                        const float down = -glm::dot(up, p.velocity);
                        if (down > 0.0f)
                        {
                            glm::vec3 along = p.velocity + up * down;
                            glm::vec3 out = glm::vec3(random01() - 0.5f, random01() - 0.5f, random01() - 0.5f);
                            out -= up * glm::dot(up, out);
                            const float flat = glm::length(out);
                            if (flat > 1e-4f)
                                out /= flat;
                            p.velocity = along * 0.25f + out * (down * 0.45f) + up * (down * 0.04f * random01());
                        }
                        if (!p.splashed)
                        {
                            p.splashed = true;
                            p.life = std::min(p.life, p.age + 0.25f + 0.2f * random01());
                            p.size *= 1.2f;
                            p.bright *= 0.3f;
                        }
                    }
                }
            }
            particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle &p) { return p.age >= p.life; }), particles.end());
            diamonds.clear();
            if (!spawn)
                return;
            const sl::FlightBody &fb = ship.body();

            // Thrusters are numbered in the order of the stages' engines.
            std::vector<const kosmos::EngineSpec *> engines;
            double densest = 1.0;
            for (const kosmos::StageSpec &st : ship.spec().stages)
                for (const kosmos::EngineSpec &e : st.engines)
                {
                    engines.push_back(&e);
                    densest = std::max(densest, e.plume_density);
                }
            if (particles.size() > static_cast<std::size_t>(1400.0 * densest))
                return;
            auto plume_of = [&](std::size_t i, const sl::Thruster &t) -> int
            {
                if (t.level <= 0.01 || t.max_thrust <= 0.0 || t.tank < 0 || fb.tanks[static_cast<std::size_t>(t.tank)].mass <= 0.0)
                    return none;
                return i < engines.size() ? plume_kind(*engines[i], t.level) : static_cast<int>(kerosene);
            };

            int firing = 0;
            for (std::size_t i = 0; i < fb.thrusters.size(); ++i)
                if (plume_of(i, fb.thrusters[i]) != none)
                    ++firing;
            for (std::size_t i = 0; i < fb.thrusters.size() && firing > 0; ++i)
            {
                const sl::Thruster &t = fb.thrusters[i];
                const int kind = plume_of(i, t);
                if (kind == none)
                    continue;
                const Style &st = style(kind);
                const double plume = i < engines.size() ? engines[i]->plume : 10.0;
                const float wide = i < engines.size() ? static_cast<float>(engines[i]->plume_width) : 1.0f;
                const float bright = i < engines.size() ? static_cast<float>(engines[i]->plume_glow) : 1.0f;
                // An afterburner only lights above 80% throttle and ramps up from there.
                const float level = kind == afterburner ? static_cast<float>((t.level - 0.8) / 0.2) : static_cast<float>(t.level);
                // Hydrogen and nuclear engines share the same plume shape.
                const bool hydrogen_plume = kind == hydrogen || kind == nuclear;
                const float billow = i < engines.size() ? static_cast<float>(engines[i]->plume_billow) : 0.0f;
                const float crisp = hydrogen_plume ? static_cast<float>(fx::smoothstep(0.3, 0.8, vacuum)) : (kind == scramjet || kind == afterburner ? 1.0f : 0.0f);
                const float tight = crisp * (1.0f - billow);
                const double expansion = vacuum * (kind == kerosene ? 0.6 : 1.0);
                const float length = static_cast<float>(plume) * st.length * level * (1.0f + 0.6f * static_cast<float>(expansion)) * (hydrogen_plume ? 1.0f - 0.5f * crisp : 1.0f);
                const float width = static_cast<float>((1.1 + 3.2 * expansion) * (0.5 + 0.5 * t.level)) * st.width * wide * (hydrogen_plume ? 1.0f - 0.45f * tight : 1.0f);
                const glm::vec3 dir = glm::vec3(-t.direction);
                const glm::vec3 origin = glm::vec3(t.position + ship.com_offset() +
                                                   (i < engines.size() ? engines[i]->plume_offset : sl::dvec3(0.0)));
                glm::vec3 side1 = std::fabs(dir.x) < 0.9f ? glm::cross(dir, glm::vec3(1, 0, 0)) : glm::cross(dir, glm::vec3(0, 1, 0));
                side1 = glm::normalize(side1);
                const glm::vec3 side2 = glm::cross(dir, side1);
                const float density = i < engines.size() ? static_cast<float>(engines[i]->plume_density) : 1.0f;
                const float rate = std::min(420.0f, 1000.0f / static_cast<float>(firing)) * density * level * (hydrogen_plume ? 1.0f - 0.5f * tight : 1.0f);
                float &carry = carry_main;
                carry += rate * dt;
                for (; carry >= 1.0f; carry -= 1.0f)
                {
                    const float angle = random01() * 6.2831853f, spread = (0.1f + 0.2f * static_cast<float>(expansion)) * st.spread * random01() * (hydrogen_plume ? 1.0f - 0.7f * tight : 1.0f);
                    const float life = (0.45f + random01() * 0.35f) * st.life;
                    const float speed = length / (0.62f * st.life) * (0.8f + 0.4f * random01());
                    const glm::vec3 radial = side1 * std::cos(angle) + side2 * std::sin(angle);
                    Particle p;
                    p.position = origin + radial * (0.25f * width * random01());
                    p.velocity = dir * speed + radial * (spread * speed);
                    p.age = random01() * dt;
                    if (hydrogen_plume)
                        p.position += p.velocity * p.age;
                    p.life = life;
                    p.size = width * 0.35f * (0.75f + 0.5f * random01());
                    p.kind = static_cast<std::uint8_t>(kind);
                    p.body = 0.55f - 0.48f * tight;
                    p.bright = bright;
                    p.stretch = hydrogen_plume ? 1.0f + 2.5f * (1.0f - static_cast<float>(vacuum)) : 1.0f;
                    particles.push_back(p);
                }
                if (crisp > 0.05f)
                {
                    // Shock diamonds sit at fixed spacing down the plume, shrinking and fading with distance.
                    const float reach = length * 0.9f, gap = reach / 5.5f;
                    for (int k = 0; k < 5; ++k)
                    {
                        const float fall = 1.0f - 0.16f * static_cast<float>(k);
                        diamonds.push_back({origin + dir * (gap * (0.7f + static_cast<float>(k))), dir, width * 0.42f * fall * (0.95f + 0.1f * random01()), level * fall * crisp, static_cast<std::uint8_t>(kind)});
                    }
                }
            }

            // Reaction-control jets: cold-gas puffs, one jet per commanded translation and per rotation couple.
            double top = 0.0, bottom = 0.0, radius = 1.0;
            bool first_stage = true;
            const auto &all_stages = ship.spec().stages;
            for (std::size_t s = static_cast<std::size_t>(std::clamp(ship.active_stage(), 0, std::max(0, static_cast<int>(all_stages.size()) - 1))); s < all_stages.size(); ++s)
            {
                const kosmos::StageSpec &st = all_stages[s];
                top = first_stage ? st.z + st.length * 0.5 : std::max(top, st.z + st.length * 0.5);
                bottom = first_stage ? st.z - st.length * 0.5 : std::min(bottom, st.z - st.length * 0.5);
                radius = first_stage ? st.radius : std::max(radius, st.radius);
                first_stage = false;
            }
            const float arm = static_cast<float>(std::max(1.5, 0.5 * (top - bottom))), centre = static_cast<float>(0.5 * (top + bottom));
            int slot = 0;
            auto jet = [&](const glm::vec3 &position, const glm::vec3 &push, float amount)
            {
                float &carry = carry_rcs[slot++ % 9];
                carry += 110.0f * amount * dt;
                for (; carry >= 1.0f; carry -= 1.0f)
                {
                    Particle p;
                    const glm::vec3 out = -push;
                    p.position = position;
                    p.velocity = out * (5.0f + random01() * 3.0f) + glm::vec3(random01() - 0.5f, random01() - 0.5f, random01() - 0.5f) * 1.2f;
                    p.age = random01() * dt;
                    p.life = 0.18f + random01() * 0.12f;
                    p.size = 0.09f + random01() * 0.05f;
                    p.kind = rcs;
                    particles.push_back(p);
                }
            };
            // The jets sit at the ends of the stage envelope; a ship whose hull does not reach there turns them off with "rcs_effects": false.
            const bool puffs = ship.spec().rcs_effects;
            const glm::vec3 shove = glm::vec3(ship.rcs_push());
            const glm::vec3 torque = puffs ? glm::vec3(ship.rcs_torque()) : glm::vec3(0.0f);
            const auto [custom_layout, layout] = rcs_visual_layout(all_stages, ship.active_stage());
            if (custom_layout)
            {
                if (puffs && layout)
                    for (const RcsVisualJet &j : rcs_visual_jets(*layout, shove, torque))
                        jet(j.position, j.push, j.amount);
                return;
            }
            if (puffs && glm::length(shove) > 0.01f)
                jet(glm::vec3(0.0f, 0.0f, centre) - shove * static_cast<float>(radius), shove, 1.0f);
            const glm::vec3 arms[3][2] = {{{0, 0, centre + arm}, {0, 0, centre - arm}}, {{0, 0, centre + arm}, {0, 0, centre - arm}}, {{static_cast<float>(radius), 0, centre}, {-static_cast<float>(radius), 0, centre}}};
            for (int axis = 0; axis < 3; ++axis)
            {
                const float amount = std::fabs(torque[axis]);
                if (amount < 0.05f)
                    continue;
                glm::vec3 a(0.0f);
                a[axis] = torque[axis] > 0.0f ? 1.0f : -1.0f;
                for (const glm::vec3 &p : arms[axis])
                {
                    const glm::vec3 force = glm::normalize(glm::cross(a, p - glm::vec3(0.0f, 0.0f, centre)));
                    jet(p, force, std::min(1.0f, amount));
                }
            }
        }

        void draw(sl::Mesh *glow_mesh, const glm::mat4 &hull) const
        {
            sl::Material glow;
            glow.lit = false;
            glow.fog = false;
            glow.blend = sl::MaterialBlend::additive;
            glow.cull = sl::CullMode::none;
            std::vector<sl::InstanceData3D> instances;
            instances.reserve(2 * (particles.size() + diamonds.size()));
            const auto append = [&](const glm::mat4 &model, sl::Colour tint)
            {
                sl::InstanceData3D instance;
                instance.model = model;
                instance.tint = tint;
                instances.push_back(instance);
            };
            for (const Particle &p : particles)
            {
                const float age = p.age / p.life;
                const float fade = (1.0f - age) * (1.0f - age);
                const Style &st = style(p.kind);
                const float size = p.size * (0.6f + 1.2f * age);
                glm::mat4 base = hull * glm::translate(glm::mat4(1.0f), p.position);
                if (!p.splashed && p.stretch > 1.0f && glm::length(p.velocity) > 1.0e-4f)
                {
                    const glm::vec3 axis = glm::normalize(p.velocity);
                    const glm::vec3 side = glm::normalize(glm::cross(axis, std::fabs(axis.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
                    base *= glm::mat4(glm::vec4(side, 0.0f), glm::vec4(glm::cross(axis, side), 0.0f), glm::vec4(axis, 0.0f), glm::vec4(0, 0, 0, 1));
                    base = glm::scale(base, glm::vec3(1.0f, 1.0f, p.stretch));
                }
                sl::Colour tint = st.glow;
                tint.alpha = static_cast<std::uint8_t>(std::clamp(st.halo * p.bright * fade * 255.0f, 0.0f, 255.0f));
                append(base * glm::scale(glm::mat4(1.0f), glm::vec3(size * 2.6f)), tint);
                tint = age < 0.35f ? st.core : st.tail;
                const float body = p.body * p.bright;
                tint.alpha = static_cast<std::uint8_t>(std::clamp(body * fade * 255.0f, 0.0f, 255.0f));
                append(base * glm::scale(glm::mat4(1.0f), glm::vec3(size)), tint);
            }
            for (const Diamond &d : diamonds)
            {
                const Style &st = style(d.kind);
                glm::vec3 side1 = std::fabs(d.axis.x) < 0.9f ? glm::cross(d.axis, glm::vec3(1, 0, 0)) : glm::cross(d.axis, glm::vec3(0, 1, 0));
                side1 = glm::normalize(side1);
                const glm::mat4 frame(glm::vec4(side1, 0.0f), glm::vec4(glm::cross(d.axis, side1), 0.0f), glm::vec4(d.axis, 0.0f), glm::vec4(d.position, 1.0f));
                const glm::mat4 base = hull * frame;
                // Stretched along the flow so each knot reads as a diamond.
                sl::Colour tint = st.glow;
                tint.alpha = static_cast<std::uint8_t>(std::clamp(0.30f * d.strength * 255.0f, 0.0f, 255.0f));
                append(base * glm::scale(glm::mat4(1.0f), glm::vec3(d.size * 1.5f, d.size * 1.5f, d.size * 3.2f)), tint);
                tint = st.core;
                tint.alpha = static_cast<std::uint8_t>(std::clamp(1.0f * d.strength * 255.0f, 0.0f, 255.0f));
                append(base * glm::scale(glm::mat4(1.0f), glm::vec3(d.size * 1.0f, d.size * 1.0f, d.size * 2.6f)), tint);
            }
            if (!instances.empty() && !sl::draw_mesh_instanced(glow_mesh, instances, glow))
            {
                static bool reported = false;
                if (!reported)
                {
                    std::fprintf(stderr, "[kosmos] exhaust instancing unavailable: %s; using individual draws\n", sl::last_error().c_str());
                    reported = true;
                }
                for (const sl::InstanceData3D &instance : instances)
                {
                    glow.tint = instance.tint;
                    sl::draw_mesh(glow_mesh, instance.model, glow);
                }
            }
        }
    };
} // namespace kosmos::app
