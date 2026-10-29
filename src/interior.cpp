#include "interior.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <queue>

namespace kosmos
{
    namespace
    {
        std::uint32_t hash3(int x, int y, int z)
        {
            std::uint32_t h = static_cast<std::uint32_t>(x) * 73856093u ^ static_cast<std::uint32_t>(y) * 19349663u ^ static_cast<std::uint32_t>(z) * 83492791u;
            h ^= h >> 13;
            h *= 0x5bd1e995u;
            return h ^ (h >> 15);
        }
    } // namespace

    std::shared_ptr<Interior> Interior::parse(const nlohmann::json &json, bool upright, double ground_z, std::string &error)
    {
        if (!json.is_object() || !json.contains("levels") || !json["levels"].is_array() || json["levels"].empty())
        {
            error = "interior needs a \"levels\" array of floor plans";
            return nullptr;
        }
        auto in = std::make_shared<Interior>();
        ground_z = json.value("ground_z", ground_z);
        in->cell_ = std::max(0.1, json.value("cell", 0.5));
        const int height = std::clamp(json.value("height", 6), door_cells + 1, 40);

        // One plan may be given as a plain list of rows.
        std::vector<std::vector<std::string>> plans;
        if (json["levels"][0].is_string())
            plans.push_back(json["levels"].get<std::vector<std::string>>());
        else
            for (const nlohmann::json &level : json["levels"])
                plans.push_back(level.get<std::vector<std::string>>());

        int w = 0, d = 0;
        for (const auto &plan : plans)
        {
            d = std::max(d, static_cast<int>(plan.size()));
            for (const std::string &row : plan)
                w = std::max(w, static_cast<int>(row.size()));
        }
        if (w < 1 || d < 1)
        {
            error = "interior floor plan is empty";
            return nullptr;
        }
        const int levels = static_cast<int>(plans.size());
        in->nx_ = w;
        in->nz_ = d;
        in->ny_ = (height + 1) * levels + 1;
        in->cells_.assign(static_cast<std::size_t>(in->nx_) * in->ny_ * in->nz_, Cell::air);

        auto ch = [&](int level, int x, int z) {
            if (level < 0 || level >= levels)
                return ' ';
            const auto &plan = plans[static_cast<std::size_t>(level)];
            if (z >= static_cast<int>(plan.size()) || x >= static_cast<int>(plan[static_cast<std::size_t>(z)].size()))
                return ' ';
            return plan[static_cast<std::size_t>(z)][static_cast<std::size_t>(x)];
        };
        auto set = [&](int x, int y, int z, Cell c) { in->cells_[(static_cast<std::size_t>(y) * in->nz_ + z) * in->nx_ + x] = c; };

        // Windows span roughly 0.9 m to 2.1 m above the floor.
        const int win_lo = static_cast<int>(std::ceil(0.9 / in->cell_)) + 1, win_hi = std::min(height, static_cast<int>(std::floor(2.1 / in->cell_)));
        for (int level = 0; level < levels; ++level)
            for (int z = 0; z < d; ++z)
                for (int x = 0; x < w; ++x)
                {
                    const char c = ch(level, x, z);
                    const int base = level * (height + 1);
                    for (int k = 1; k <= height; ++k)
                    {
                        if (c == '#')
                            set(x, base + k, z, Cell::wall);
                        else if (c == 'W')
                            set(x, base + k, z, k >= win_lo && k <= win_hi ? Cell::glass : Cell::wall);
                        else if ((c == 'D' || c == 'A' || c == 'd') && k > door_cells)
                            set(x, base + k, z, Cell::wall);
                        else if (c == 'A')
                            set(x, base + k, z, Cell::hatch);
                    }
                }
        // Slabs: under every occupied column of a level, and over the top level.
        for (int s = 0; s <= levels; ++s)
            for (int z = 0; z < d; ++z)
                for (int x = 0; x < w; ++x)
                    if (ch(s, x, z) != ' ' || ch(s - 1, x, z) != ' ')
                        set(x, s * (height + 1), z, Cell::slab);

        in->frame_ = upright ? sl::dmat3(sl::dvec3(0, 1, 0), sl::dvec3(0, 0, -1), sl::dvec3(-1, 0, 0)) : sl::dmat3(1.0);
        sl::dvec3 offset(0.0);
        if (json.contains("origin") && json["origin"].is_array() && json["origin"].size() >= 3)
            offset = sl::dvec3(json["origin"][0].get<double>(), json["origin"][1].get<double>(), json["origin"][2].get<double>());
        in->origin_ = sl::dvec3(0.0, 0.0, ground_z) + offset - in->frame_ * sl::dvec3(w * in->cell_ * 0.5, -0.08, d * in->cell_ * 0.5); // floor just above the ground so the terrain does not poke through

        in->door_col_.assign(static_cast<std::size_t>(w) * d, 0);
        for (int z = 0; z < d; ++z)
            for (int x = 0; x < w; ++x)
                in->door_col_[static_cast<std::size_t>(z) * w + x] = ch(0, x, z) == 'd';
        in->classify();
        in->airlock_name_ = json.value("airlock", "");
        in->find_rooms(json, levels);
        // A door is the 'd' columns of the bottom level; each needs a floor cell on both sides.
        in->door_at_.assign(static_cast<std::size_t>(w) * d, -1);
        for (int z = 0; z < d; ++z)
            for (int x = 0; x < w; ++x)
            {
                if (ch(0, x, z) != 'd')
                    continue;
                auto floor_at = [&](int fx, int fz) { const char f = ch(0, fx, fz); return f == '.' || f == 'D' || f == 'd'; };
                Door door;
                door.x = x;
                door.z = z;
                door.along_x = floor_at(x, z - 1) && floor_at(x, z + 1);
                door.centre = sl::dvec3((x + 0.5) * in->cell_, 0.0, (z + 0.5) * in->cell_);
                const int ax = door.along_x ? x : x - 1, az = door.along_x ? z - 1 : z;
                const int bx = door.along_x ? x : x + 1, bz = door.along_x ? z + 1 : z;
                door.room_a = in->room_at(sl::dvec3((ax + 0.5) * in->cell_, 0.1, (az + 0.5) * in->cell_));
                door.room_b = in->room_at(sl::dvec3((bx + 0.5) * in->cell_, 0.1, (bz + 0.5) * in->cell_));
                in->door_at_[static_cast<std::size_t>(z) * w + x] = static_cast<int>(in->doors_.size());
                in->doors_.push_back(door);
            }

        if (json.contains("lights") && json["lights"].is_array())
            for (const nlohmann::json &lj : json["lights"])
                if (lj.contains("pos") && lj["pos"].is_array() && lj["pos"].size() >= 3)
                {
                    Light light;
                    light.pos = sl::dvec3(lj["pos"][0].get<double>(), lj["pos"][1].get<double>(), lj["pos"][2].get<double>());
                    if (lj.contains("colour") && lj["colour"].is_array() && lj["colour"].size() >= 3)
                        light.colour = sl::dvec3(lj["colour"][0].get<double>(), lj["colour"][1].get<double>(), lj["colour"][2].get<double>());
                    light.intensity = lj.value("intensity", light.intensity);
                    light.range = lj.value("range", light.range);
                    in->lights_.push_back(light);
                }
        if (json.contains("mesh") && json["mesh"].is_string())
            in->mesh_path = json["mesh"].get<std::string>();

        if (json.contains("panels") && json["panels"].is_array())
            for (const nlohmann::json &pj : json["panels"])
            {
                Panel panel;
                if (!pj.contains("pos") || !pj["pos"].is_array() || pj["pos"].size() < 3)
                    continue;
                panel.pos = sl::dvec3(pj["pos"][0].get<double>(), pj["pos"][1].get<double>(), pj["pos"][2].get<double>());
                const std::string face = pj.value("facing", "+z");
                panel.normal = face == "+x" ? sl::dvec3(1, 0, 0) : face == "-x" ? sl::dvec3(-1, 0, 0) : face == "-z" ? sl::dvec3(0, 0, -1) : sl::dvec3(0, 0, 1);
                panel.width = pj.value("width", 1.2);
                in->panels_.push_back(panel);
            }

        // The way in is the first hatch on the bottom level, entered from an open floor cell beside it.
        const double c = in->cell_;
        const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int z = 0; z < d && !in->has_door_; ++z)
            for (int x = 0; x < w && !in->has_door_; ++x)
            {
                if (ch(0, x, z) != 'A')
                    continue;
                for (const auto &dir : dirs)
                {
                    const int fx = x + dir[0], fz = z + dir[1];
                    if (fx < 0 || fz < 0 || fx >= w || fz >= d || ch(0, fx, fz) != '.')
                        continue;
                    in->outward_ = sl::dvec3(-dir[0], 0.0, -dir[1]);
                    in->entry_ = sl::dvec3((fx + 0.5) * c, 0.0, (fz + 0.5) * c);
                    in->entry_yaw_ = std::atan2(in->outward_.x, in->outward_.z);
                    const sl::dvec3 hatch((x + 0.5) * c, 0.0, (z + 0.5) * c);
                    in->door_ship_ = in->to_ship(hatch + in->outward_ * c * 1.5);
                    in->has_door_ = true;
                    break;
                }
            }
        if (!in->has_door_)
        {
            error = "interior has no airlock: put an 'A' next to a '.' floor cell on the first level";
            return nullptr;
        }
        return in;
    }

    Interior::Cell Interior::at(int x, int y, int z) const
    {
        if (x < 0 || y < 0 || z < 0 || x >= nx_ || y >= ny_ || z >= nz_)
            return Cell::air;
        return cells_[(static_cast<std::size_t>(y) * nz_ + z) * nx_ + x];
    }

    bool Interior::solid(int x, int y, int z) const
    {
        return at(x, y, z) != Cell::air;
    }

    bool Interior::outdoors(int x, int y, int z) const
    {
        if (x < 0 || y < 0 || z < 0 || x >= nx_ || y >= ny_ || z >= nz_)
            return true;
        return outdoor_[(static_cast<std::size_t>(y) * nz_ + z) * nx_ + x] != 0;
    }

    void Interior::classify()
    {
        outdoor_.assign(cells_.size(), 0);
        std::queue<int> open;
        auto index = [&](int x, int y, int z) { return (y * nz_ + z) * nx_ + x; };
        auto push = [&](int x, int y, int z) {
            if (x < 0 || y < 0 || z < 0 || x >= nx_ || y >= ny_ || z >= nz_ || solid(x, y, z) || outdoor_[static_cast<std::size_t>(index(x, y, z))])
                return;
            outdoor_[static_cast<std::size_t>(index(x, y, z))] = 1;
            open.push(index(x, y, z));
        };
        for (int y = 0; y < ny_; ++y)
            for (int z = 0; z < nz_; ++z)
                for (int x = 0; x < nx_; ++x)
                    if (x == 0 || z == 0 || y == 0 || x == nx_ - 1 || z == nz_ - 1 || y == ny_ - 1)
                        push(x, y, z);
        while (!open.empty())
        {
            const int i = open.front();
            open.pop();
            const int x = i % nx_, z = (i / nx_) % nz_, y = i / (nx_ * nz_);
            push(x + 1, y, z);
            push(x - 1, y, z);
            push(x, y + 1, z);
            push(x, y - 1, z);
            push(x, y, z + 1);
            push(x, y, z - 1);
        }
    }

    int Interior::panel_in_view(const sl::dvec3 &eye, const sl::dvec3 &forward, double reach) const
    {
        int best = -1;
        double best_dot = 0.8;
        for (std::size_t i = 0; i < panels_.size(); ++i)
        {
            const sl::dvec3 to = panels_[i].pos - eye;
            const double d = glm::length(to);
            if (d > reach || d < 1e-6 || glm::dot(forward, panels_[i].normal) > -0.2)
                continue;
            const double facing = glm::dot(forward, to / d);
            if (facing > best_dot)
                best_dot = facing, best = static_cast<int>(i);
        }
        return best;
    }

    int Interior::room_at(const sl::dvec3 &p) const
    {
        const int x = static_cast<int>(std::floor(p.x / cell_)), z = static_cast<int>(std::floor(p.z / cell_)), y = static_cast<int>(std::floor(p.y / cell_)) + 1;
        if (x < 0 || z < 0 || y < 0 || x >= nx_ || z >= nz_ || y >= ny_ || room_.empty())
            return -1;
        return room_[(static_cast<std::size_t>(y) * nz_ + z) * nx_ + x];
    }

    // Rooms are the connected pockets of enclosed air; walls, closed doors and the hatch separate them. Each is named by a "rooms" seed.
    void Interior::find_rooms(const nlohmann::json &json, int)
    {
        room_.assign(cells_.size(), -1);
        const std::vector<std::uint8_t> &door_cell = door_col_;
        std::vector<std::pair<int, int>> seeds;
        std::vector<std::string> names;
        if (json.contains("rooms") && json["rooms"].is_array())
            for (const nlohmann::json &r : json["rooms"])
                if (r.contains("at") && r["at"].is_array() && r["at"].size() >= 2)
                {
                    seeds.push_back({r["at"][0].get<int>(), r["at"][1].get<int>()});
                    names.push_back(r.value("name", ""));
                }
        auto idx = [&](int x, int y, int z) { return (static_cast<std::size_t>(y) * nz_ + z) * nx_ + x; };
        int next = 0;
        for (int y = 1; y < ny_; ++y)
            for (int z = 0; z < nz_; ++z)
                for (int x = 0; x < nx_; ++x)
                {
                    if (solid(x, y, z) || outdoor_[idx(x, y, z)] || room_[idx(x, y, z)] >= 0 || door_cell[static_cast<std::size_t>(z) * nx_ + x])
                        continue;
                    const int id = next++;
                    std::queue<std::array<int, 3>> open;
                    room_[idx(x, y, z)] = id;
                    open.push({x, y, z});
                    while (!open.empty())
                    {
                        const auto c = open.front();
                        open.pop();
                        const int d[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                        for (const auto &o : d)
                        {
                            const int a = c[0] + o[0], b = c[1] + o[1], e = c[2] + o[2];
                            if (a < 0 || b < 1 || e < 0 || a >= nx_ || b >= ny_ || e >= nz_ || solid(a, b, e) || room_[idx(a, b, e)] >= 0 || door_cell[static_cast<std::size_t>(e) * nx_ + a])
                                continue;
                            room_[idx(a, b, e)] = id;
                            open.push({a, b, e});
                        }
                    }
                }
        room_names_.assign(static_cast<std::size_t>(next), "");
        for (std::size_t i = 0; i < seeds.size(); ++i)
        {
            const int id = room_at(sl::dvec3((seeds[i].first + 0.5) * cell_, 0.1, (seeds[i].second + 0.5) * cell_));
            if (id >= 0)
                room_names_[static_cast<std::size_t>(id)] = names[i];
        }
    }

    bool Interior::blocked(const sl::dvec3 &feet, double radius, double height, const InteriorState *state) const
    {
        const double eps = 1e-6;
        const int x0 = static_cast<int>(std::floor((feet.x - radius) / cell_)), x1 = static_cast<int>(std::floor((feet.x + radius) / cell_));
        const int z0 = static_cast<int>(std::floor((feet.z - radius) / cell_)), z1 = static_cast<int>(std::floor((feet.z + radius) / cell_));
        const int y0 = static_cast<int>(std::floor((feet.y + eps) / cell_)) + 1, y1 = static_cast<int>(std::floor((feet.y + height) / cell_)) + 1;
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x)
                {
                    // Beyond the plan is outdoors: nobody walks off the edge of the grid.
                    if (x < 0 || z < 0 || x >= nx_ || z >= nz_ || y < 0 || y >= ny_ || solid(x, y, z))
                        return true;
                    if (state && y >= 1 && y <= door_cells && !door_at_.empty())
                    {
                        const int di = door_at_[static_cast<std::size_t>(z) * nx_ + x];
                        if (di >= 0 && static_cast<std::size_t>(di) < state->door_open.size() && state->door_open[static_cast<std::size_t>(di)] < 0.75)
                            return true;
                    }
                }
        return false;
    }

    void Interior::build_meshes(sl::MeshData &exterior, sl::MeshData &inside, sl::MeshData &glass) const
    {
        const double c = cell_;
        // Per face direction (axis, sign): the two tangent axes follow the cycle so that u x v points along the normal.
        for (int y = 0; y < ny_; ++y)
            for (int z = 0; z < nz_; ++z)
                for (int x = 0; x < nx_; ++x)
                {
                    const Cell type = at(x, y, z);
                    if (type == Cell::air)
                        continue;
                    const int pos[3] = {x, y, z};
                    for (int axis = 0; axis < 3; ++axis)
                        for (int sign = -1; sign <= 1; sign += 2)
                        {
                            int n[3] = {x, y, z};
                            n[axis] += sign;
                            // Glass shows its faces to open air only; walls show theirs to anything but another opaque cell.
                            if (type == Cell::glass ? solid(n[0], n[1], n[2]) : solid(n[0], n[1], n[2]) && at(n[0], n[1], n[2]) != Cell::glass)
                                continue;
                            const bool out = outdoors(n[0], n[1], n[2]);
                            sl::MeshData &mesh = type == Cell::glass ? glass : out ? exterior : inside;
                            const int ua = (axis + 1) % 3, va = (axis + 2) % 3;
                            sl::dvec3 corner(pos[0] * c, (pos[1] - 1) * c, pos[2] * c);
                            if (sign > 0)
                                corner[axis] += c;
                            sl::dvec3 u(0.0), v(0.0), normal(0.0);
                            u[ua] = c;
                            v[va] = c;
                            normal[axis] = sign;
                            if (sign < 0)
                                std::swap(u, v);

                            float shade = 0.72f;
                            if (!out)
                                shade = axis == 1 ? (sign > 0 ? 0.8f : 0.5f) : 0.68f;
                            shade *= 0.92f + 0.08f * static_cast<float>(hash3(x, y, z) & 255u) / 255.0f;
                            float colour[3] = {0.78f, 0.8f, 0.84f};
                            float alpha = 1.0f;
                            if (type == Cell::glass)
                            {
                                colour[0] = 0.55f;
                                colour[1] = 0.75f;
                                colour[2] = 0.9f;
                                alpha = 0.22f;
                                shade = 1.0f;
                            }
                            else if (type == Cell::hatch)
                            {
                                colour[0] = 0.95f;
                                colour[1] = 0.7f;
                                colour[2] = 0.2f;
                            }
                            else if (type == Cell::slab && axis == 1 && sign > 0 && !out)
                            {
                                colour[0] = 0.55f;
                                colour[1] = 0.58f;
                                colour[2] = 0.62f;
                            }
                            const sl::dvec3 n_ship = dir_to_ship(normal);
                            const sl::dvec3 corners[4] = {corner, corner + u, corner + u + v, corner + v};
                            const std::uint32_t first = static_cast<std::uint32_t>(mesh.vertices.size());
                            for (const sl::dvec3 &p : corners)
                            {
                                sl::Vertex3D vert;
                                const sl::dvec3 s = to_ship(p);
                                for (int k = 0; k < 3; ++k)
                                {
                                    vert.position[k] = static_cast<float>(s[k]);
                                    vert.normal[k] = static_cast<float>(n_ship[k]);
                                    vert.colour[k] = colour[k] * shade;
                                }
                                vert.colour[3] = alpha;
                                mesh.vertices.push_back(vert);
                            }
                            for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                                mesh.indices.push_back(first + i);
                        }
                }
    }

    sl::dvec3 InteriorWalker::forward() const
    {
        const double cp = std::cos(pitch);
        return sl::dvec3(-std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp);
    }

    void InteriorWalker::place(const Interior &interior)
    {
        position = interior.entry();
        velocity = sl::dvec3(0.0);
        yaw = interior.entry_yaw();
        pitch = 0.0;
        crouch = 0.0;
        grounded = true;
    }

    void InteriorWalker::step(const Interior &interior, const EvaInput &input, double gravity, double dt, const InteriorState *state)
    {
        yaw += input.yaw;
        pitch = std::clamp(pitch + input.pitch, -1.5, 1.5);
        crouch += (((input.crouch ? 1.0 : 0.0) - crouch) * std::min(1.0, dt * 10.0));
        const double height = crouch > 0.5 ? 1.2 : 1.8;

        const double top = input.run ? 9.0 : input.crouch ? 1.5 : 3.5;
        const sl::dvec3 fwd(-std::sin(yaw), 0.0, -std::cos(yaw)), right(std::cos(yaw), 0.0, -std::sin(yaw));
        sl::dvec3 wish = fwd * input.forward + right * input.right;
        const double len = glm::length(wish);
        if (len > 1.0)
            wish /= len;
        const sl::dvec3 target = wish * top;
        sl::dvec3 flat(velocity.x, 0.0, velocity.z);
        const sl::dvec3 diff = target - flat;
        const double dl = glm::length(diff), accel = (grounded ? 40.0 : 6.0) * dt;
        flat += dl > accel ? diff * (accel / dl) : diff;
        velocity.x = flat.x;
        velocity.z = flat.z;
        velocity.y -= gravity * dt;
        if (input.jump && grounded)
            velocity.y = 4.4;

        grounded = false;
        for (int axis : {0, 2, 1})
        {
            sl::dvec3 next = position;
            next[axis] += velocity[axis] * dt;
            if (!interior.blocked(next, radius, height, state))
                position = next;
            else
            {
                if (axis == 1 && velocity.y < 0.0)
                    grounded = true;
                velocity[axis] = 0.0;
            }
        }
        speed = std::hypot(velocity.x, velocity.z);
    }
} // namespace kosmos
