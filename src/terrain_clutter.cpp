#include "terrain_clutter.h"

#include <props3d.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace kosmos
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        double smooth(double a, double b, double x)
        {
            const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
            return t * t * (3.0 - 2.0 * t);
        }

        double hash(int x, int y, int z, std::uint32_t seed)
        {
            std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u +
                              static_cast<std::uint32_t>(z) * 2147483647u + seed * 2246822519u;
            h = (h ^ (h >> 13)) * 1274126177u;
            h ^= h >> 16;
            return static_cast<double>(h & 0xffffffu) / 8388607.5 - 1.0;
        }

        /** Smooth value noise in about -1..1. */
        double noise3(double x, double y, double z, std::uint32_t seed)
        {
            const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y)), iz = static_cast<int>(std::floor(z));
            const double fx = x - ix, fy = y - iy, fz = z - iz;
            const double u = fx * fx * (3.0 - 2.0 * fx), v = fy * fy * (3.0 - 2.0 * fy), w = fz * fz * (3.0 - 2.0 * fz);
            double c[2][2][2];
            for (int i = 0; i < 2; ++i)
                for (int j = 0; j < 2; ++j)
                    for (int k = 0; k < 2; ++k)
                        c[i][j][k] = hash(ix + i, iy + j, iz + k, seed);
            const auto lerp = [](double a, double b, double t) { return a + (b - a) * t; };
            return lerp(lerp(lerp(c[0][0][0], c[1][0][0], u), lerp(c[0][1][0], c[1][1][0], u), v),
                        lerp(lerp(c[0][0][1], c[1][0][1], u), lerp(c[0][1][1], c[1][1][1], u), v), w);
        }

        enum class Kind
        {
            none,
            earth,   // Earth itself: land cover, houses and a clear margin round its airfields
            alien,   // an invented world with air and life: procedural alien plants and rocks
            rocky,   // airless or thin air: boulders and the odd outpost
            hostile  // thick atmosphere: boulders only
        };

        Kind kind_of(const Body &b)
        {
            if (b.real_world)
                return Kind::earth;
            if (b.style.type == Archetype::earth || b.terrain == "earth")
                return Kind::alien;
            if (b.style.type == Archetype::gas)
                return Kind::none;
            if (b.style.type == Archetype::venus || b.atmosphere_density > 10.0)
                return Kind::hostile;
            return Kind::rocky;
        }

        /** What the clutter rules look at about a point on the ground. */
        struct Ground
        {
            SurfaceFrame frame;
            double radius = 1.0;
            std::vector<ClutterZone> zones;
            /** Whether the zones are kept free of clutter; off for airless worlds, where they only say where the base is. */
            bool clear_zones = true;
            std::shared_ptr<const Landclass> land;

            /** Metres from a point to the nearest zone's site, minus its margin; negative inside. */
            bool blocked(const sl::dvec3 &p) const
            {
                if (!clear_zones)
                    return false;
                for (const ClutterZone &z : zones)
                {
                    const sl::dvec3 ab = z.b - z.a;
                    const double len2 = glm::dot(ab, ab);
                    const double t = len2 > 1.0e-30 ? std::clamp(glm::dot(p - z.a, ab) / len2, 0.0, 1.0) : 0.0;
                    const sl::dvec3 q = glm::normalize(z.a + ab * t);
                    if (std::acos(std::clamp(glm::dot(p, q), -1.0, 1.0)) * radius < z.radius)
                        return true;
                }
                return false;
            }

            /** Metres from a point to the edge of the nearest zone (negative inside it); huge with no zones. */
            double nearest(const sl::dvec3 &p) const
            {
                double best = 1.0e30;
                for (const ClutterZone &z : zones)
                {
                    const sl::dvec3 ab = z.b - z.a;
                    const double len2 = glm::dot(ab, ab);
                    const double t = len2 > 1.0e-30 ? std::clamp(glm::dot(p - z.a, ab) / len2, 0.0, 1.0) : 0.0;
                    const sl::dvec3 q = glm::normalize(z.a + ab * t);
                    best = std::min(best, std::acos(std::clamp(glm::dot(p, q), -1.0, 1.0)) * radius - z.radius);
                }
                return best;
            }

            double field(const sl::dvec3 &p, double scale, std::uint32_t seed) const
            {
                const sl::dvec3 m = p * radius / scale;
                return noise3(m.x, m.y, m.z, seed);
            }

            /** Land-cover weights (index by LandClass) of an Earth point; guessed from noise and latitude without data. */
            void cover(const sl::dvec3 &p, float w[lc_count]) const
            {
                std::fill(w, w + lc_count, 0.0f);
                double lat, lon;
                frame.to_lat_lon(p, lat, lon);
                if (land && land->valid())
                {
                    // The 1 km cells would show as blocks, so wobble the lookup by a few hundred metres.
                    const double wx = field(p, 2500.0, 31u), wy = field(p, 2500.0, 57u);
                    const double per_degree = radius * pi / 180.0;
                    const LandSample s = land->sample(lon * 180.0 / pi + wx * 700.0 / (per_degree * std::max(std::cos(lat), 0.05)),
                                                      lat * 180.0 / pi + wy * 700.0 / per_degree);
                    if (s.valid)
                    {
                        std::copy(s.weight, s.weight + lc_count, w);
                        return;
                    }
                }
                const double wood = smooth(-0.1, 0.4, field(p, 9000.0, 3u));
                const double scrub = smooth(0.0, 0.5, field(p, 6000.0, 4u)) * (1.0 - wood);
                const double town = smooth(0.55, 0.8, field(p, 5000.0, 5u));
                const double polar = smooth(55.0, 70.0, std::fabs(lat) * 180.0 / pi);
                w[lc_mixed_trees] = static_cast<float>(wood * (1.0 - polar) * (1.0 - town));
                w[lc_shrubs] = static_cast<float>(scrub * (1.0 - town));
                w[lc_urban] = static_cast<float>(town);
                w[lc_cultivated] = static_cast<float>((1.0 - wood) * (1.0 - scrub) * (1.0 - town) * 0.6);
                w[lc_snow] = static_cast<float>(polar);
            }
        };

        sl::Colour rgb(const float c[3], float scale = 1.0f)
        {
            const auto b = [scale](float v) { return static_cast<std::uint8_t>(std::clamp(v * scale, 0.0f, 1.0f) * 255.0f); };
            return {b(c[0]), b(c[1]), b(c[2]), 255};
        }

        sl::ClutterSpecies species(sl::MeshData mesh, double density, float max_slope, float near_distance, float far_distance, std::uint32_t seed)
        {
            sl::ClutterSpecies s;
            s.mesh = std::move(mesh);
            s.density = static_cast<float>(density);
            s.max_slope = max_slope;
            s.near_distance = near_distance;
            s.far_distance = far_distance;
            s.seed = seed;
            s.tint_variation = 0.18f;
            return s;
        }

        // Detail textures for rocks, loaded once and normalised to an even tone so the vertex colours still set the shade.
        std::string rock_texture_root;
        sl::Bitmap *rock_loaded[10] = {};
        bool rock_tried[10] = {};

        std::string &texture_root() { return rock_texture_root; }

        sl::Bitmap *rock_texture(int index)
        {
            static const char *const names[] = {"Rock1", "Rock3", "Rock4", "Rock5", "Rock6", "Rock7", "Rock9", "SpeckledStone", "PebbledWall", "SmallRocks"};
            constexpr int count = static_cast<int>(sizeof names / sizeof *names);
            static_assert(count == 10, "rock_loaded and rock_tried are sized for ten textures");
            sl::Bitmap **loaded = rock_loaded;
            bool *tried = rock_tried;
            const int i = ((index % count) + count) % count;
            if (tried[i])
                return loaded[i];
            tried[i] = true;
            sl::Bitmap *bitmap = sl::load_bitmap((texture_root() + "/data/textures/rocks/" + names[i] + ".png").c_str());
            if (!bitmap || bitmap->pixels.empty())
                return nullptr;
            double sum = 0.0;
            for (std::size_t k = 0; k + 3 < bitmap->pixels.size(); k += 4)
                sum += (bitmap->pixels[k] + bitmap->pixels[k + 1] + bitmap->pixels[k + 2]) / 3.0;
            const double gain = 215.0 / std::max(sum / (static_cast<double>(bitmap->pixels.size()) / 4.0), 1.0);
            for (std::size_t k = 0; k < bitmap->pixels.size(); ++k)
                if (k % 4 != 3)
                    bitmap->pixels[k] = static_cast<std::uint8_t>(std::clamp(215.0 + (bitmap->pixels[k] * gain - 215.0) * 1.8, 40.0, 255.0));
            bitmap->smooth = true;
            bitmap->mipmaps = true;
            bitmap->anisotropy = 8.0f;
            bitmap->ram_dirty = true;
            loaded[i] = bitmap;
            return bitmap;
        }

        // Gives a rock mesh UVs projected along whichever axis its surface faces, `tile` metres to a repeat, and one of the rock textures.
        void texture_rock(sl::ClutterSpecies &s, int index, float tile)
        {
            sl::Bitmap *texture = rock_texture(index);
            if (!texture)
                return;
            for (sl::Vertex3D &v : s.mesh.vertices)
            {
                const float ax = std::fabs(v.normal[0]), ay = std::fabs(v.normal[1]), az = std::fabs(v.normal[2]);
                if (ay >= ax && ay >= az)
                {
                    v.uv[0] = v.position[0] / tile;
                    v.uv[1] = v.position[2] / tile;
                }
                else if (ax >= az)
                {
                    v.uv[0] = v.position[2] / tile;
                    v.uv[1] = v.position[1] / tile;
                }
                else
                {
                    v.uv[0] = v.position[0] / tile;
                    v.uv[1] = v.position[1] / tile;
                }
            }
            s.material.texture = texture;
        }

        sl::ClutterSpecies rock_species(sl::MeshData mesh, double size, int index, double density, float max_slope, float near_distance, float far_distance, std::uint32_t seed)
        {
            sl::ClutterSpecies s = species(std::move(mesh), density, max_slope, near_distance, far_distance, seed);
            texture_rock(s, index, std::max(0.5f, static_cast<float>(size) * 0.7f));
            return s;
        }

        using Rule = std::function<double(const Ground &, const sl::TerrainSample &)>;

        void add(sl::ClutterLayer &layer, const std::shared_ptr<const Ground> &ground, sl::ClutterSpecies s, Rule rule, float min_height = 1.5f)
        {
            s.min_height = min_height;
            s.suitability = [ground, rule](const sl::TerrainSample &sample) -> float {
                if (ground->blocked(sample.position))
                    return 0.0f;
                return static_cast<float>(std::clamp(rule(*ground, sample), 0.0, 1.0));
            };
            layer.add_species(s);
        }

        void add_earth(sl::ClutterLayer &layer, const std::shared_ptr<const Ground> &ground, double density, const Body &body)
        {
            const double d = density;
            // Woods: the land cover says where the trees are, and noise gathers them into stands with clearings between.
            const auto woods = [](const Ground &g, const sl::TerrainSample &s, double (*weight)(const float *)) {
                float w[lc_count];
                g.cover(s.position, w);
                const double p = weight(w);
                if (p <= 0.02)
                    return 0.0;
                return smooth(0.1, 0.6, p) * smooth(-0.2, 0.15, g.field(s.position, 1400.0, 31u));
            };
            const Rule conifers = [woods](const Ground &g, const sl::TerrainSample &s) {
                return woods(g, s, [](const float *w) { return static_cast<double>(w[lc_needleleaf] + 0.5f * w[lc_mixed_trees]); });
            };
            const Rule broadleaf = [woods](const Ground &g, const sl::TerrainSample &s) {
                return woods(g, s, [](const float *w) {
                    return static_cast<double>(w[lc_evergreen_broadleaf] + w[lc_deciduous_broadleaf] + 0.5f * w[lc_mixed_trees] + 0.4f * w[lc_flooded]);
                });
            };
            const Rule scrub = [](const Ground &g, const sl::TerrainSample &s) {
                float w[lc_count];
                g.cover(s.position, w);
                const double p = w[lc_shrubs] + 0.25 * (w[lc_needleleaf] + w[lc_mixed_trees] + w[lc_deciduous_broadleaf]) + 0.12 * w[lc_herbaceous] + 0.06 * w[lc_cultivated];
                return smooth(0.05, 0.6, p) * (0.5 + 0.5 * smooth(-0.4, 0.3, g.field(s.position, 600.0, 77u)));
            };
            const Rule houses = [](const Ground &g, const sl::TerrainSample &s) {
                float w[lc_count];
                g.cover(s.position, w);
                // Towns and suburbs, with the odd farmhouse out in the fields.
                return std::min(1.0, 1.4 * w[lc_urban] + 0.035 * w[lc_cultivated] + 0.012 * w[lc_herbaceous]);
            };
            const Rule blocks = [](const Ground &g, const sl::TerrainSample &s) {
                float w[lc_count];
                g.cover(s.position, w);
                return smooth(0.9, 1.0, w[lc_urban]);
            };
            const Rule stones = [](const Ground &g, const sl::TerrainSample &s) {
                float w[lc_count];
                g.cover(s.position, w);
                const double rocky = w[lc_barren] + 0.04 * (1.0 - w[lc_water] - w[lc_urban]) + 0.5 * smooth(18.0, 40.0, s.slope_degrees);
                return std::min(1.0, rocky) * (0.4 + 0.6 * smooth(-0.3, 0.3, g.field(s.position, 400.0, 91u)));
            };

            const sl::Colour pine = {34, 98, 48, 255}, pine_dark = {26, 78, 44, 255};
            add(layer, ground, species(sl::make_conifer_data(11.0f, 2.6f, {92, 62, 40, 255}, pine, 8), 0.0022 * d, 32.0f, 260.0f, 2600.0f, 11u), conifers);
            add(layer, ground, species(sl::make_conifer_data(15.0f, 3.1f, {88, 60, 38, 255}, pine_dark, 8), 0.0016 * d, 32.0f, 260.0f, 2600.0f, 18u), conifers);
            add(layer, ground, species(sl::make_tree_data(8.0f, 3.0f, {104, 74, 48, 255}, {70, 138, 58, 255}, 1u), 0.0020 * d, 30.0f, 260.0f, 2600.0f, 25u), broadleaf);
            add(layer, ground, species(sl::make_tree_data(12.0f, 4.2f, {98, 70, 46, 255}, {88, 128, 52, 255}, 2u), 0.0014 * d, 30.0f, 260.0f, 2600.0f, 32u), broadleaf);
            add(layer, ground, species(sl::make_bush_data(0.9f, {70, 120, 56, 255}, 1u), 0.0014 * d, 40.0f, 90.0f, 900.0f, 39u), scrub);
            add(layer, ground, species(sl::make_bush_data(1.2f, {104, 120, 62, 255}, 2u), 0.0009 * d, 40.0f, 90.0f, 900.0f, 46u), scrub);
            const sl::Colour walls[3] = {{214, 196, 160, 255}, {198, 168, 140, 255}, {226, 222, 212, 255}};
            const sl::Colour roofs[3] = {{150, 64, 48, 255}, {88, 82, 86, 255}, {120, 78, 56, 255}};
            for (int i = 0; i < 3; ++i)
                add(layer, ground,
                    species(sl::make_house_data(6.0f + 1.5f * i, 5.0f + i, 3.0f + 0.4f * i, walls[i], roofs[i], 1u + static_cast<std::uint32_t>(i)), 0.00016 * d, 10.0f, 300.0f, 2200.0f,
                            53u + 7u * static_cast<std::uint32_t>(i)),
                    houses);
            add(layer, ground, species(sl::make_office_data(14.0f, 10.0f, 5, {170, 172, 176, 255}, 3u), 0.000006 * d, 6.0f, 400.0f, 2600.0f, 71u), blocks);
            add(layer, ground, rock_species(sl::make_rock_data(1.0f, rgb(body.colour, 0.55f), 3u), 1.0, 5, 0.0007 * d, 55.0f, 120.0f, 700.0f, 83u), stones);
        }

        // The mean colour of the terrain around `direction`, so rocks match the ground they lie on; the body's colour if the terrain has no colour source.
        void ground_colour(const sl::Terrain &terrain, const Body &body, const sl::dvec3 &direction, float out[3])
        {
            std::copy(body.colour, body.colour + 3, out);
            const auto &colour_at = terrain.config().colour_function;
            if (!colour_at)
                return;
            sl::dvec3 east = glm::cross(sl::dvec3(0.0, 1.0, 0.0), direction);
            east = glm::length(east) > 1e-6 ? glm::normalize(east) : sl::dvec3(1.0, 0.0, 0.0);
            const sl::dvec3 north = glm::cross(direction, east);
            const double step = 150.0 / body.radius;
            double sum[3] = {0.0, 0.0, 0.0};
            int count = 0;
            for (int i = -3; i <= 3; ++i)
                for (int j = -3; j <= 3; ++j)
                {
                    sl::TerrainSample sample;
                    sample.position = glm::normalize(direction + east * (i * step) + north * (j * step));
                    sample.normal = sample.position;
                    sample.cell_size = 150.0;
                    float rgb[3];
                    if (!colour_at(sample, rgb))
                        continue;
                    for (int k = 0; k < 3; ++k)
                        sum[k] += rgb[k];
                    ++count;
                }
            if (count > 0)
                for (int k = 0; k < 3; ++k)
                    out[k] = static_cast<float>(sum[k] / count);
        }

        void add_rocky(sl::ClutterLayer &layer, const std::shared_ptr<const Ground> &ground, double density, const float colour[3], bool outposts)
        {
            const double d = density;
            const sl::Colour stone = rgb(colour, 1.0f), dark = rgb(colour, 0.78f);
            // Rock fields thicken and thin over a few hundred metres.
            const Rule field = [](const Ground &g, const sl::TerrainSample &s) {
                return 0.3 + 0.7 * smooth(-0.4, 0.4, g.field(s.position, 500.0, 5u)) + 0.5 * smooth(15.0, 40.0, s.slope_degrees);
            };
            const Rule boulders = [](const Ground &g, const sl::TerrainSample &s) {
                return smooth(-0.1, 0.5, g.field(s.position, 1800.0, 9u)) * (0.4 + 0.6 * smooth(10.0, 35.0, s.slope_degrees));
            };
            add(layer, ground, rock_species(sl::make_rock_data(0.5f, stone, 1u), 0.5, 0, 0.0035 * d, 50.0f, 150.0f, 900.0f, 11u), field, -1.0e30f);
            add(layer, ground, rock_species(sl::make_rock_data(0.8f, dark, 2u), 0.8, 1, 0.0020 * d, 50.0f, 150.0f, 900.0f, 17u), field, -1.0e30f);
            add(layer, ground, rock_species(sl::make_rock_cluster_data(1.6f, stone, 3u), 1.6, 2, 0.0006 * d, 50.0f, 200.0f, 1400.0f, 23u), field, -1.0e30f);
            add(layer, ground, rock_species(sl::make_rock_data(3.5f, dark, 4u), 3.5, 3, 0.00025 * d, 55.0f, 300.0f, 2400.0f, 29u), boulders, -1.0e30f);
            add(layer, ground, rock_species(sl::make_rock_data(7.0f, stone, 5u), 7.0, 4, 0.00006 * d, 55.0f, 500.0f, 3600.0f, 37u), boulders, -1.0e30f);
            if (!outposts)
                return;

            // Only a handful of outposts, in a ring around each base just outside its levelled ground.
            const Rule settlement = [](const Ground &g, const sl::TerrainSample &s) {
                const double dist = g.nearest(s.position);
                return smooth(-20.0, 40.0, dist) * (1.0 - smooth(450.0, 650.0, dist)) * (1.0 - smooth(4.0, 9.0, s.slope_degrees));
            };
            const sl::Colour hull = {226, 229, 234, 255};
            add(layer, ground, species(sl::make_pressure_dome_data(8.0f, hull), 0.0000020 * d, 8.0f, 600.0f, 4200.0f, 101u), settlement, -1.0e30f);
            add(layer, ground, species(sl::make_pressure_dome_data(5.0f, {214, 218, 224, 255}), 0.0000015 * d, 8.0f, 500.0f, 3600.0f, 107u), settlement, -1.0e30f);
            add(layer, ground, species(sl::make_habitat_tube_data(12.0f, 2.2f, {222, 224, 226, 255}), 0.0000025 * d, 8.0f, 500.0f, 3600.0f, 113u), settlement, -1.0e30f);
            add(layer, ground, species(sl::make_container_data(6.1f, 2.44f, 2.6f, {190, 190, 186, 255}, 2u), 0.0000020 * d, 8.0f, 300.0f, 2000.0f, 119u), settlement, -1.0e30f);
            add(layer, ground, species(sl::make_tank_data(2.5f, 5.0f, {196, 198, 200, 255}), 0.0000015 * d, 8.0f, 300.0f, 2000.0f, 127u), settlement, -1.0e30f);
            add(layer, ground, species(sl::make_lunar_rover_data(3.5f, {214, 214, 208, 255}), 0.0000010 * d, 10.0f, 250.0f, 1500.0f, 131u), settlement, -1.0e30f);
        }

        /** Flat-shaded mesh building for the alien plants; solids pass a point inside so every facet faces outwards. */
        struct PlantMesh
        {
            sl::MeshData data;

            void tri(glm::vec3 a, glm::vec3 b, glm::vec3 c, const glm::vec3 &inside, const sl::Colour &col, float shade = 1.0f, bool both = false)
            {
                glm::vec3 n = glm::cross(b - a, c - a);
                if (glm::dot(n, n) < 1.0e-12f)
                    return;
                n = glm::normalize(n);
                if (!both && glm::dot(n, (a + b + c) / 3.0f - inside) < 0.0f)
                {
                    std::swap(b, c);
                    n = -n;
                }
                emit(a, b, c, n, col, shade);
                if (both)
                    emit(a, c, b, -n, col, shade);
            }

            void emit(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &n, const sl::Colour &col, float shade)
            {
                const auto base = static_cast<std::uint32_t>(data.vertices.size());
                for (const glm::vec3 &p : {a, b, c})
                {
                    sl::Vertex3D v;
                    v.position[0] = p.x;
                    v.position[1] = p.y;
                    v.position[2] = p.z;
                    v.normal[0] = n.x;
                    v.normal[1] = n.y;
                    v.normal[2] = n.z;
                    v.colour[0] = col.red / 255.0f * shade;
                    v.colour[1] = col.green / 255.0f * shade;
                    v.colour[2] = col.blue / 255.0f * shade;
                    v.colour[3] = 1.0f;
                    data.vertices.push_back(v);
                }
                data.indices.insert(data.indices.end(), {base, base + 1, base + 2});
            }

            /** A tapered ring of facets from (x, y0, z) radius r0 to (x + dx, y1, z + dz) radius r1, shaded lo to hi. */
            void frustum(glm::vec3 lo, float r0, glm::vec3 hi, float r1, int sides, const sl::Colour &col, float shade_lo, float shade_hi, float twist = 0.0f)
            {
                const glm::vec3 inside = (lo + hi) * 0.5f;
                for (int i = 0; i < sides; ++i)
                {
                    const float a0 = 2.0f * static_cast<float>(pi) * i / sides, a1 = 2.0f * static_cast<float>(pi) * (i + 1) / sides;
                    const glm::vec3 p0 = lo + glm::vec3(std::cos(a0), 0, std::sin(a0)) * r0, p1 = lo + glm::vec3(std::cos(a1), 0, std::sin(a1)) * r0;
                    const glm::vec3 q0 = hi + glm::vec3(std::cos(a0 + twist), 0, std::sin(a0 + twist)) * r1;
                    const glm::vec3 q1 = hi + glm::vec3(std::cos(a1 + twist), 0, std::sin(a1 + twist)) * r1;
                    const float shade = 0.85f + 0.15f * std::cos(a0 * 2.0f);
                    tri(p0, p1, q1, inside, col, shade_lo * shade);
                    tri(p0, q1, q0, inside, col, shade_hi * shade);
                }
            }

            /** An ellipsoid of low-poly facets. */
            void blob(glm::vec3 centre, glm::vec3 radii, int sides, int rings, const sl::Colour &col, float shade_lo, float shade_hi, bool lower_half = true)
            {
                for (int j = (lower_half ? 0 : rings / 2); j < rings; ++j)
                {
                    const float t0 = static_cast<float>(pi) * j / rings, t1 = static_cast<float>(pi) * (j + 1) / rings;
                    const float s0 = 1.0f - 2.0f * j / rings * 0.5f, s1 = 1.0f - 2.0f * (j + 1) / rings * 0.5f;
                    for (int i = 0; i < sides; ++i)
                    {
                        const float a0 = 2.0f * static_cast<float>(pi) * i / sides, a1 = 2.0f * static_cast<float>(pi) * (i + 1) / sides;
                        const auto pt = [&](float t, float a) {
                            return centre + glm::vec3(std::sin(t) * std::cos(a) * radii.x, std::cos(t) * radii.y, std::sin(t) * std::sin(a) * radii.z);
                        };
                        const float shade = shade_lo + (shade_hi - shade_lo) * (0.5f + 0.5f * std::cos(t0 + 0.3f)) * (0.9f + 0.1f * s0 * s1);
                        tri(pt(t0, a0), pt(t0, a1), pt(t1, a1), centre, col, shade);
                        tri(pt(t0, a0), pt(t1, a1), pt(t1, a0), centre, col, shade);
                    }
                }
            }

            /** A tapering leaf blade from base to tip, bent by `droop`, two-sided. */
            void blade(glm::vec3 base, glm::vec3 tip, float width, float droop, const sl::Colour &col, float shade_lo, float shade_hi)
            {
                const glm::vec3 axis = tip - base;
                glm::vec3 side = glm::cross(axis, glm::vec3(0, 1, 0));
                side = glm::dot(side, side) < 1.0e-8f ? glm::vec3(1, 0, 0) : glm::normalize(side) * width * 0.5f;
                glm::vec3 prev_l = base - side, prev_r = base + side;
                for (int k = 1; k <= 3; ++k)
                {
                    const float t = k / 3.0f;
                    const glm::vec3 c = base + axis * t - glm::vec3(0, droop * t * t, 0);
                    const float w = 1.0f - t * 0.92f;
                    const glm::vec3 l = c - side * w, r = c + side * w;
                    const float sh = shade_lo + (shade_hi - shade_lo) * t;
                    tri(prev_l, prev_r, r, {}, col, sh, true);
                    tri(prev_l, r, l, {}, col, sh, true);
                    prev_l = l;
                    prev_r = r;
                }
            }
        };

        struct Palette
        {
            sl::Colour trunk, foliage, foliage2, accent;
        };

        sl::Colour hsv(float h, float s, float v)
        {
            h = h - std::floor(h);
            const float f = h * 6.0f;
            const int i = static_cast<int>(f);
            const float fr = f - i, p = v * (1.0f - s), q = v * (1.0f - s * fr), t = v * (1.0f - s * (1.0f - fr));
            const float r[6] = {v, q, p, p, t, v}, g[6] = {t, v, v, q, p, p}, b[6] = {p, p, t, v, v, q};
            const auto c = [](float x) { return static_cast<std::uint8_t>(std::clamp(x, 0.0f, 1.0f) * 255.0f); };
            return {c(r[i % 6]), c(g[i % 6]), c(b[i % 6]), 255};
        }

        /** A world's flora colours, picked from its name so each world is its own colour and the same every run. */
        Palette palette_for(const Body &body)
        {
            std::uint32_t h = 2166136261u;
            for (const char ch : body.name)
                h = (h ^ static_cast<std::uint8_t>(ch)) * 16777619u;
            const float hue = (h % 1000) / 1000.0f;
            const float shift = 0.08f + ((h >> 10) % 100) / 100.0f * 0.2f;
            return {hsv(hue + 0.08f, 0.35f, 0.34f), hsv(hue, 0.62f, 0.62f), hsv(hue + shift, 0.58f, 0.74f), hsv(hue + 0.5f, 0.7f, 0.95f)};
        }

        sl::Colour vary(const sl::Colour &c, std::uint32_t seed, float amount)
        {
            const auto j = [&](int salt) { return 1.0f + amount * static_cast<float>(hash(static_cast<int>(seed), salt, 7, 99u)); };
            const auto b = [](float v) { return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 255.0f)); };
            return {b(c.red * j(1)), b(c.green * j(2)), b(c.blue * j(3)), 255};
        }

        float rnd(std::uint32_t seed, int salt) { return static_cast<float>(0.5 + 0.5 * hash(static_cast<int>(seed), salt, 3, 211u)); }

        Palette vary_palette(const Palette &p, std::uint32_t seed)
        {
            return {vary(p.trunk, seed, 0.1f), vary(p.foliage, seed, 0.18f), vary(p.foliage2, seed, 0.18f), vary(p.accent, seed, 0.12f)};
        }

        /** A tall tapering stalk in stacked bulging segments, tipped with a cone and with a few side shoots. */
        sl::MeshData make_alien_spire(float height, float radius, const Palette &pal, std::uint32_t seed)
        {
            PlantMesh m;
            const int segments = 4 + static_cast<int>(seed % 3);
            float y = 0.0f, r = radius;
            float cx = 0.0f, cz = 0.0f;
            for (int i = 0; i < segments; ++i)
            {
                const float h = height / segments;
                const float r_mid = r * (1.0f + 0.35f * rnd(seed, i));
                const float nx = cx + (rnd(seed, 20 + i) - 0.5f) * radius * 0.6f, nz = cz + (rnd(seed, 40 + i) - 0.5f) * radius * 0.6f;
                m.frustum({cx, y, cz}, r, {nx, y + h * 0.5f, nz}, r_mid, 7, i % 2 ? pal.foliage : pal.foliage2, 0.7f + 0.05f * i, 0.85f + 0.04f * i, 0.4f);
                r = r * (0.78f - 0.06f * rnd(seed, 60 + i));
                m.frustum({nx, y + h * 0.5f, nz}, r_mid, {nx, y + h, nz}, r, 7, i % 2 ? pal.foliage : pal.foliage2, 0.85f + 0.04f * i, 0.95f, 0.4f);
                cx = nx;
                cz = nz;
                y += h;
            }
            m.frustum({cx, y, cz}, r, {cx, y + height * 0.22f, cz}, 0.0f, 7, pal.accent, 0.9f, 1.15f);
            const int shoots = 2 + static_cast<int>(seed % 3);
            for (int k = 0; k < shoots; ++k)
            {
                const float a = rnd(seed, 80 + k) * 6.2831853f, base = height * (0.3f + 0.4f * rnd(seed, 90 + k));
                const glm::vec3 from(std::cos(a) * radius * 0.5f, base, std::sin(a) * radius * 0.5f);
                const glm::vec3 to = from + glm::vec3(std::cos(a) * height * 0.35f, height * 0.22f, std::sin(a) * height * 0.35f);
                m.blade(from, to, radius * 1.1f, height * 0.12f, pal.foliage2, 0.8f, 1.1f);
            }
            return std::move(m.data);
        }

        /** A thin trunk under a broad scalloped cap, with a few hanging fronds. */
        sl::MeshData make_alien_umbrella(float height, float cap_radius, const Palette &pal, std::uint32_t seed)
        {
            PlantMesh m;
            const float trunk = cap_radius * 0.12f;
            const float lean = (rnd(seed, 1) - 0.5f) * cap_radius * 0.5f;
            m.frustum({0, 0, 0}, trunk * 1.8f, {lean * 0.5f, height * 0.5f, 0}, trunk * 1.2f, 6, pal.trunk, 0.6f, 0.8f);
            m.frustum({lean * 0.5f, height * 0.5f, 0}, trunk * 1.2f, {lean, height, 0}, trunk, 6, pal.trunk, 0.8f, 0.95f);
            m.blob({lean, height, 0}, {cap_radius, cap_radius * 0.38f, cap_radius}, 9, 6, pal.foliage, 0.65f, 1.15f, false);
            m.blob({lean, height + cap_radius * 0.18f, 0}, {cap_radius * 0.45f, cap_radius * 0.2f, cap_radius * 0.45f}, 7, 4, pal.accent, 0.85f, 1.2f, false);
            const int fronds = 5 + static_cast<int>(seed % 4);
            for (int k = 0; k < fronds; ++k)
            {
                const float a = 6.2831853f * k / fronds + rnd(seed, k);
                const glm::vec3 from(lean + std::cos(a) * cap_radius * 0.9f, height, std::sin(a) * cap_radius * 0.9f);
                m.blade(from, from + glm::vec3(0, -height * 0.4f * (0.6f + rnd(seed, 30 + k)), 0), cap_radius * 0.12f, 0.0f, pal.foliage2, 0.6f, 1.0f);
            }
            return std::move(m.data);
        }

        /** A low rosette of arching blades. */
        sl::MeshData make_alien_fronds(float radius, const Palette &pal, std::uint32_t seed)
        {
            PlantMesh m;
            const int blades = 8 + static_cast<int>(seed % 5);
            for (int k = 0; k < blades; ++k)
            {
                const float a = 6.2831853f * k / blades + rnd(seed, k) * 0.4f;
                const float len = radius * (0.7f + 0.6f * rnd(seed, 20 + k));
                const float up = radius * (0.5f + 0.9f * rnd(seed, 40 + k));
                m.blade({0, 0.05f, 0}, {std::cos(a) * len, up, std::sin(a) * len}, radius * 0.28f, radius * 0.45f, k % 3 ? pal.foliage : pal.foliage2, 0.65f, 1.1f);
            }
            return std::move(m.data);
        }

        /** A cluster of glowing bulbs on short stems. */
        sl::MeshData make_alien_bulbs(float radius, const Palette &pal, std::uint32_t seed)
        {
            PlantMesh m;
            const int count = 3 + static_cast<int>(seed % 3);
            for (int k = 0; k < count; ++k)
            {
                const float a = 6.2831853f * k / count + rnd(seed, k), d = radius * 0.7f * rnd(seed, 10 + k);
                const float r = radius * (0.35f + 0.35f * rnd(seed, 20 + k)), h = radius * (0.4f + 0.9f * rnd(seed, 30 + k));
                const glm::vec3 at(std::cos(a) * d, 0, std::sin(a) * d);
                m.frustum(at, r * 0.25f, at + glm::vec3(0, h, 0), r * 0.18f, 5, pal.trunk, 0.7f, 0.9f);
                m.blob(at + glm::vec3(0, h + r * 0.6f, 0), {r, r * 0.8f, r}, 6, 5, k % 2 ? pal.accent : pal.foliage2, 0.7f, 1.2f);
            }
            return std::move(m.data);
        }

        void add_alien(sl::ClutterLayer &layer, const std::shared_ptr<const Ground> &ground, double density, const Body &body)
        {
            const double d = density;
            const Palette pal = palette_for(body);
            // No land data for an invented world: biomes come from noise, with groves gathered inside wider forests.
            const Rule groves = [](const Ground &g, const sl::TerrainSample &s) {
                return smooth(-0.35, 0.2, g.field(s.position, 7000.0, 3u)) * smooth(-0.3, 0.1, g.field(s.position, 1300.0, 131u));
            };
            const Rule scrub = [](const Ground &g, const sl::TerrainSample &s) {
                return (0.35 + 0.65 * smooth(-0.2, 0.3, g.field(s.position, 5000.0, 4u))) * (0.4 + 0.6 * smooth(-0.4, 0.3, g.field(s.position, 500.0, 177u)));
            };
            const Rule stones = [](const Ground &g, const sl::TerrainSample &s) {
                return 0.15 + 0.85 * smooth(15.0, 40.0, s.slope_degrees) + 0.3 * smooth(-0.2, 0.4, g.field(s.position, 300.0, 191u));
            };
            for (std::uint32_t v = 0; v < 2; ++v)
            {
                add(layer, ground, species(make_alien_spire(9.0f + 5.0f * v, 0.9f + 0.4f * v, vary_palette(pal, v), 3u + v), 0.0012 * d, 30.0f, 260.0f, 2600.0f, 211u + v), groves);
                add(layer, ground, species(make_alien_umbrella(7.0f + 4.0f * v, 3.2f + 1.2f * v, vary_palette(pal, v + 7), 5u + v), 0.0012 * d, 30.0f, 260.0f, 2600.0f, 223u + v), groves);
                add(layer, ground, species(make_alien_fronds(1.4f + 0.6f * v, vary_palette(pal, v + 3), 9u + v), 0.0016 * d, 40.0f, 90.0f, 900.0f, 233u + v), scrub);
                add(layer, ground, species(make_alien_bulbs(1.0f + 0.5f * v, vary_palette(pal, v + 5), 13u + v), 0.0009 * d, 40.0f, 90.0f, 900.0f, 241u + v), scrub);
            }
            add(layer, ground, rock_species(sl::make_rock_data(0.9f, {118, 108, 98, 255}, 3u), 0.9, 6, 0.0007 * d, 55.0f, 120.0f, 700.0f, 251u), stones);
        }
    } // namespace

    bool has_clutter(const Body &body) { return kind_of(body) != Kind::none; }

    double distance_to_zones(const std::vector<ClutterZone> &zones, const sl::dvec3 &direction, double radius)
    {
        Ground g;
        g.radius = radius;
        g.zones = zones;
        return g.nearest(glm::normalize(direction));
    }

    std::vector<ClutterZone> clutter_zones(const std::vector<Base> &bases, const Universe &universe, int body_id, double radius)
    {
        std::vector<ClutterZone> zones;
        for (const Base &base : bases)
        {
            if (universe.find(base.body) != body_id)
                continue;
            for (const FlattenSite &site : flatten_sites(base, radius))
                zones.push_back({site.a, site.b, site.flat + site.blend + 120.0});
        }
        return zones;
    }

    std::unique_ptr<sl::ClutterLayer> make_clutter(const sl::Terrain &terrain, const ClutterWorld &world)
    {
        texture_root() = world.root;
        if (!world.body)
            return nullptr;
        const Kind kind = kind_of(*world.body);
        if (kind == Kind::none)
            return nullptr;
        auto ground = std::make_shared<Ground>();
        ground->frame = world.frame;
        ground->radius = world.body->radius;
        // Rocks run right through a base on an airless world; only a world with air and runways keeps a clear margin.
        ground->zones = world.zones;
        ground->clear_zones = kind == Kind::earth || kind == Kind::alien;
        ground->land = world.land;

        constexpr float cell = 192.0f;
        sl::ClutterConfig config;
        config.cell_size = cell;
        config.max_cached_cells = 900;
        config.time_budget_ms = 3.0;
        auto layer = std::make_unique<sl::ClutterLayer>(terrain, config);
        if (kind == Kind::earth)
            add_earth(*layer, ground, world.density, *world.body);
        else if (kind == Kind::alien)
            add_alien(*layer, ground, world.density, *world.body);
        else
            {
            float colour[3];
            ground_colour(terrain, *world.body, world.near_direction, colour);
            add_rocky(*layer, ground, world.density, colour, kind == Kind::rocky);
        }
        return layer;
    }
} // namespace kosmos
