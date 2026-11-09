#include "terrain_mesh.h"

#include "base.h"
#include "draw.h"
#include "model3d.h"
#include "surface3d.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

namespace kosmos
{
    namespace
    {
        constexpr float none = std::numeric_limits<float>::quiet_NaN();
        constexpr double pi = 3.14159265358979323846;

        double smooth(double t)
        {
            t = std::clamp(t, 0.0, 1.0);
            return t * t * (3.0 - 2.0 * t);
        }

        struct Tri
        {
            double p[3][3]; // (across, forward, up) in mesh units
            float rgb[3][3];
            bool has_rgb = false;
        };

        bool read_stl(const std::string &path, std::vector<Tri> &out, std::string &error)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
            {
                error = "cannot open " + path;
                return false;
            }
            std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::uint32_t count = 0;
            if (bytes.size() >= 84)
                std::memcpy(&count, bytes.data() + 80, 4);
            if (bytes.size() >= 84 && bytes.size() == 84 + 50ull * count)
            {
                out.reserve(count);
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    const char *rec = bytes.data() + 84 + 50ull * i + 12;
                    Tri t;
                    for (int k = 0; k < 3; ++k)
                    {
                        float v[3];
                        std::memcpy(v, rec + 12 * k, 12);
                        for (int c = 0; c < 3; ++c)
                            t.p[k][c] = v[c];
                    }
                    out.push_back(t);
                }
                return true;
            }
            std::istringstream text(std::string(bytes.begin(), bytes.end()));
            std::string word;
            Tri t;
            int k = 0;
            while (text >> word)
            {
                if (word != "vertex")
                    continue;
                text >> t.p[k][0] >> t.p[k][1] >> t.p[k][2];
                if (++k == 3)
                {
                    out.push_back(t);
                    k = 0;
                }
            }
            if (out.empty())
                error = "no triangles in " + path;
            return !out.empty();
        }

        // Pixel of a bitmap at texture coordinates (u, v) with the origin at the top left, wrapping.
        void texel(const sl::Bitmap *bitmap, double u, double v, float rgb[3])
        {
            u -= std::floor(u);
            v -= std::floor(v);
            const int x = std::min(bitmap->width - 1, static_cast<int>(u * bitmap->width));
            const int y = std::min(bitmap->height - 1, static_cast<int>(v * bitmap->height));
            const std::uint8_t *p = &bitmap->pixels[(static_cast<std::size_t>(y) * bitmap->width + x) * 4];
            for (int c = 0; c < 3; ++c)
                rgb[c] = p[c] / 255.0f;
        }

        bool has_pixels(const sl::Bitmap *bitmap)
        {
            return bitmap && bitmap->width > 0 && bitmap->height > 0 && bitmap->pixels.size() >= static_cast<std::size_t>(bitmap->width) * bitmap->height * 4;
        }

        // Triangles of a model file, with the colour of the texture (or vertex colour) at each corner.
        bool read_model(const std::string &path, bool up_y, std::vector<Tri> &out, std::string &error)
        {
            const bool retained = sl::mesh_cpu_data_retained();
            sl::set_mesh_cpu_data_retained(true);
            sl::Model *model = sl::load_model(path);
            sl::set_mesh_cpu_data_retained(retained);
            if (!model)
            {
                error = "cannot load " + path;
                return false;
            }
            for (const sl::ModelPart &part : model->parts)
            {
                if (!part.mesh || !part.visible || part.mesh->indices.empty() || part.mesh->vertices.empty())
                    continue;
                const bool textured = has_pixels(part.material.texture);
                const sl::Colour tint = part.material.tint;
                const bool coloured = textured || tint.red != 255 || tint.green != 255 || tint.blue != 255;
                const std::vector<sl::Vertex3D> &vs = part.mesh->vertices;
                const std::vector<std::uint32_t> &ix = part.mesh->indices;
                for (std::size_t i = 0; i + 2 < ix.size(); i += 3)
                {
                    Tri t;
                    t.has_rgb = coloured;
                    for (int k = 0; k < 3; ++k)
                    {
                        const sl::Vertex3D &v = vs[ix[i + static_cast<std::size_t>(k)]];
                        const glm::vec4 p = part.transform * glm::vec4(v.position[0], v.position[1], v.position[2], 1.0f);
                        if (up_y)
                        {
                            t.p[k][0] = p.x;
                            t.p[k][1] = -p.z;
                            t.p[k][2] = p.y;
                        }
                        else
                        {
                            t.p[k][0] = p.x;
                            t.p[k][1] = p.y;
                            t.p[k][2] = p.z;
                        }
                        float rgb[3] = {1.0f, 1.0f, 1.0f};
                        if (textured)
                            texel(part.material.texture, v.uv[0], v.uv[1], rgb);
                        t.rgb[k][0] = rgb[0] * v.colour[0] * tint.red / 255.0f;
                        t.rgb[k][1] = rgb[1] * v.colour[1] * tint.green / 255.0f;
                        t.rgb[k][2] = rgb[2] * v.colour[2] * tint.blue / 255.0f;
                    }
                    out.push_back(t);
                }
            }
            sl::destroy_model(model);
            if (out.empty())
                error = "no triangles in " + path;
            return !out.empty();
        }

        bool sample_image(const sl::Bitmap *bitmap, double fu, double fv, float rgb[3])
        {
            texel(bitmap, std::clamp(fu, 0.0, 0.9999), std::clamp(fv, 0.0, 0.9999), rgb);
            return true;
        }
    }

    static void rasterise_tris(const std::vector<Tri> &tris, MeshGrid &out, double cu, double cv, double cell, std::vector<float> &rgb, bool &any_rgb)
    {
        for (const Tri &t : tris)
        {
            const double x0 = t.p[0][0], y0 = t.p[0][1], x1 = t.p[1][0], y1 = t.p[1][1], x2 = t.p[2][0], y2 = t.p[2][1];
            const double det = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
            if (std::abs(det) < 1e-12)
                continue;
            const int i0 = std::max(0, static_cast<int>(std::ceil((std::min({x0, x1, x2}) - cu) / cell - 1e-9)));
            const int i1 = std::min(out.width - 1, static_cast<int>(std::floor((std::max({x0, x1, x2}) - cu) / cell + 1e-9)));
            const int j0 = std::max(0, static_cast<int>(std::ceil((std::min({y0, y1, y2}) - cv) / cell - 1e-9)));
            const int j1 = std::min(out.height - 1, static_cast<int>(std::floor((std::max({y0, y1, y2}) - cv) / cell + 1e-9)));
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i)
                {
                    const double px = cu + i * cell, py = cv + j * cell;
                    const double a = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / det;
                    const double b = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / det;
                    const double c = 1.0 - a - b;
                    constexpr double eps = -1e-7;
                    if (a < eps || b < eps || c < eps)
                        continue;
                    const float h = static_cast<float>(a * t.p[0][2] + b * t.p[1][2] + c * t.p[2][2]);
                    const std::size_t at = static_cast<std::size_t>(j) * out.width + i;
                    if (!(std::isnan(out.heights[at])) && out.heights[at] >= h)
                        continue;
                    out.heights[at] = h;
                    if (t.has_rgb)
                    {
                        any_rgb = true;
                        for (int k = 0; k < 3; ++k)
                            rgb[at * 3 + k] = static_cast<float>(a * t.rgb[0][k] + b * t.rgb[1][k] + c * t.rgb[2][k]);
                    }
                }
        }
    }

    static void fill_holes(MeshGrid &out, std::vector<float> &rgb)
    {
        std::vector<float> filled = out.heights;
        for (int j = 1; j + 1 < out.height; ++j)
            for (int i = 1; i + 1 < out.width; ++i)
            {
                const std::size_t at = static_cast<std::size_t>(j) * out.width + i;
                if (!std::isnan(out.heights[at]))
                    continue;
                double sum = 0.0, sum_rgb[3] = {0, 0, 0};
                int valid = 0;
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di)
                    {
                        const std::size_t k = static_cast<std::size_t>(j + dj) * out.width + (i + di);
                        if ((di || dj) && !std::isnan(out.heights[k]))
                        {
                            sum += out.heights[k];
                            for (int c = 0; c < 3; ++c)
                                sum_rgb[c] += rgb[k * 3 + c];
                            ++valid;
                        }
                    }
                if (valid >= 6)
                {
                    filled[at] = static_cast<float>(sum / valid);
                    for (int c = 0; c < 3; ++c)
                        rgb[at * 3 + c] = static_cast<float>(sum_rgb[c] / valid);
                }
            }
        out.heights.swap(filled);
    }

    static bool drape_image(const std::string &image_path, MeshGrid &out, std::vector<float> &rgb, std::string &error)
    {
        sl::Bitmap *bitmap = sl::load_bitmap(image_path);
        if (!has_pixels(bitmap))
        {
            if (bitmap)
                sl::destroy_bitmap(bitmap);
            error = "cannot read the image " + image_path;
            return false;
        }
        for (int j = 0; j < out.height; ++j)
            for (int i = 0; i < out.width; ++i)
            {
                float c[3];
                // Seen from above: forward is up the picture.
                sample_image(bitmap, static_cast<double>(i) / (out.width - 1), 1.0 - static_cast<double>(j) / (out.height - 1), c);
                for (int k = 0; k < 3; ++k)
                    rgb[(static_cast<std::size_t>(j) * out.width + i) * 3 + k] = c[k];
            }
        sl::destroy_bitmap(bitmap);
        return true;
    }

    bool load_mesh_grid(const TerrainMeshSpec &spec, const std::string &path, const std::string &image_path, MeshGrid &out, std::string &error)
    {
        std::string ext = std::filesystem::path(path).extension().string();
        for (char &c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const bool stl = ext == ".stl";
        const bool up_y = spec.up.empty() ? !stl : spec.up == "y" || spec.up == "Y";

        std::vector<Tri> tris;
        if (stl ? !read_stl(path, tris, error) : !read_model(path, up_y, tris, error))
            return false;
        if (stl && up_y)
            for (Tri &t : tris)
                for (auto &p : t.p)
                {
                    const double x = p[0], y = p[1], z = p[2];
                    p[0] = x;
                    p[1] = -z;
                    p[2] = y;
                }

        const double side = spec.scale, up = spec.scale * spec.vscale;
        double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
        for (Tri &t : tris)
            for (auto &p : t.p)
            {
                p[0] *= side;
                p[1] *= side;
                p[2] *= up;
                for (int c = 0; c < 3; ++c)
                {
                    lo[c] = std::min(lo[c], p[c]);
                    hi[c] = std::max(hi[c], p[c]);
                }
            }
        const double ew = hi[0] - lo[0], eh = hi[1] - lo[1];
        if (ew <= 0.0 || eh <= 0.0)
        {
            error = "mesh has no area seen from above";
            return false;
        }
        double cell = std::sqrt(2.0 * ew * eh / static_cast<double>(tris.size()));
        const double extent = std::max(ew, eh);
        cell = std::clamp(cell, extent / 2048.0, extent / 256.0);
        out.cell = cell;
        out.width = static_cast<int>(std::ceil(ew / cell)) + 1;
        out.height = static_cast<int>(std::ceil(eh / cell)) + 1;
        const std::size_t n = static_cast<std::size_t>(out.width) * out.height;
        out.heights.assign(n, none);
        std::vector<float> rgb(n * 3, 0.0f);
        bool any_rgb = false;
        // The grid is centred on the middle of the mesh's footprint.
        const double cu = 0.5 * (lo[0] + hi[0]) - 0.5 * (out.width - 1) * cell, cv = 0.5 * (lo[1] + hi[1]) - 0.5 * (out.height - 1) * cell;

        rasterise_tris(tris, out, cu, cv, cell, rgb, any_rgb);


        // Close the single-sample holes that slivers and exact edges leave.
        fill_holes(out, rgb);

        float low = std::numeric_limits<float>::max(), high = std::numeric_limits<float>::lowest();
        for (float h : out.heights)
            if (!std::isnan(h))
            {
                low = std::min(low, h);
                high = std::max(high, h);
            }
        if (low > high)
        {
            error = "mesh has no surface seen from above";
            return false;
        }
        for (float &h : out.heights)
            if (!std::isnan(h))
                h -= low;
        out.max_height = high - low;

        if (!image_path.empty())
        {
            if (!drape_image(image_path, out, rgb, error))
                return false;
            any_rgb = true;
        }
        if (any_rgb)
        {
            out.colours.resize(n * 3);
            for (std::size_t i = 0; i < n * 3; ++i)
                out.colours[i] = static_cast<unsigned char>(std::clamp(rgb[i], 0.0f, 1.0f) * 255.0f + 0.5f);
        }
        return true;
    }

    MeshSource::Placed MeshSource::place(const Base &base, double radius, const TerrainMeshSpec &spec, std::shared_ptr<MeshGrid> grid, double datum)
    {
        Placed p;
        p.origin = base.direction;
        const sl::dvec3 east = base_point(base, radius, 100.0, 0.0), north = base_point(base, radius, 0.0, 100.0);
        p.right = glm::normalize((east - p.origin) - p.origin * glm::dot(east - p.origin, p.origin));
        p.forward = glm::normalize((north - p.origin) - p.origin * glm::dot(north - p.origin, p.origin));
        p.x = spec.x;
        p.z = spec.z;
        p.heading = spec.heading * pi / 180.0;
        p.blend = spec.blend;
        p.datum = datum;
        const std::string &t = spec.texture;
        p.use_colour = t != "terrain" && !grid->colours.empty();
        p.grid = std::move(grid);
        int w = p.grid->width, h = p.grid->height;
        const std::vector<float> *prev = &p.grid->heights;
        while (w > 2 && h > 2)
        {
            const int nw = (w + 1) / 2, nh = (h + 1) / 2;
            std::vector<float> next(static_cast<std::size_t>(nw) * nh, none);
            for (int j = 0; j < nh; ++j)
                for (int i = 0; i < nw; ++i)
                {
                    double sum = 0.0;
                    int valid = 0;
                    for (int dj = 0; dj < 2; ++dj)
                        for (int di = 0; di < 2; ++di)
                        {
                            const int x = std::min(w - 1, 2 * i + di), y = std::min(h - 1, 2 * j + dj);
                            const float v = (*prev)[static_cast<std::size_t>(y) * w + x];
                            if (!std::isnan(v))
                            {
                                sum += v;
                                ++valid;
                            }
                        }
                    if (valid)
                        next[static_cast<std::size_t>(j) * nw + i] = static_cast<float>(sum / valid);
                }
            p.mips.push_back(std::move(next));
            prev = &p.mips.back();
            w = nw;
            h = nh;
        }
        return p;
    }

    MeshSource::MeshSource(std::shared_ptr<sl::TerrainSource> source, double radius, std::vector<Placed> meshes)
        : source_(std::move(source)), radius_(radius), meshes_(std::move(meshes))
    {
        min_ = source_->min_height();
        max_ = source_->max_height();
        for (const Placed &m : meshes_)
        {
            min_ = std::min(min_, m.datum);
            max_ = std::max(max_, m.datum + m.grid->max_height);
        }
    }

    namespace
    {
        struct Local
        {
            double u, v, edge;
        };

        // Position of a direction in a placed mesh's frame, or false if it lies outside the footprint.
        bool locate(const MeshSource::Placed &m, const sl::dvec3 &direction, double radius, Local &at)
        {
            if (glm::dot(direction, m.origin) <= 0.0)
                return false;
            const sl::dvec3 offset = (direction - m.origin) * radius;
            const double dx = glm::dot(offset, m.right) - m.x, dz = glm::dot(offset, m.forward) - m.z;
            const double s = std::sin(m.heading), c = std::cos(m.heading);
            at.u = dx * c - dz * s;
            at.v = dx * s + dz * c;
            const double hw = 0.5 * (m.grid->width - 1) * m.grid->cell, hh = 0.5 * (m.grid->height - 1) * m.grid->cell;
            at.edge = std::min(hw - std::abs(at.u), hh - std::abs(at.v));
            return at.edge >= 0.0;
        }

        double rim(const MeshSource::Placed &m, double edge)
        {
            return m.blend > 0.0 ? smooth(edge / m.blend) : 1.0;
        }

        // Bilinear height at node coordinates (fu, fv) of a level; NaN if any neighbour is empty.
        double bilinear(const std::vector<float> &heights, int w, int h, double fu, double fv)
        {
            fu = std::clamp(fu, 0.0, static_cast<double>(w - 1));
            fv = std::clamp(fv, 0.0, static_cast<double>(h - 1));
            const int i = std::min(static_cast<int>(fu), std::max(w - 2, 0)), j = std::min(static_cast<int>(fv), std::max(h - 2, 0));
            const double a = fu - i, b = fv - j;
            const int i1 = std::min(i + 1, w - 1), j1 = std::min(j + 1, h - 1);
            const double h00 = heights[static_cast<std::size_t>(j) * w + i], h10 = heights[static_cast<std::size_t>(j) * w + i1];
            const double h01 = heights[static_cast<std::size_t>(j1) * w + i], h11 = heights[static_cast<std::size_t>(j1) * w + i1];
            if (std::isnan(h00) || std::isnan(h10) || std::isnan(h01) || std::isnan(h11))
                return std::numeric_limits<double>::quiet_NaN();
            return (h00 * (1 - a) + h10 * a) * (1 - b) + (h01 * (1 - a) + h11 * a) * b;
        }
    }

    double MeshSource::apply(const sl::dvec3 &direction, double natural, double cell_size) const
    {
        for (const Placed &m : meshes_)
        {
            Local at;
            if (!locate(m, direction, radius_, at))
                continue;
            const MeshGrid &g = *m.grid;
            int level = 0;
            if (cell_size > g.cell)
                level = std::min(static_cast<int>(m.mips.size()), static_cast<int>(std::floor(std::log2(cell_size / g.cell))));
            const double step = g.cell * static_cast<double>(1 << level);
            const double fu = (at.u + 0.5 * (g.width - 1) * g.cell) / step, fv = (at.v + 0.5 * (g.height - 1) * g.cell) / step;
            double h;
            if (level == 0)
                h = bilinear(g.heights, g.width, g.height, fu, fv);
            else
            {
                int w = g.width, ht = g.height;
                for (int k = 0; k < level; ++k)
                {
                    w = (w + 1) / 2;
                    ht = (ht + 1) / 2;
                }
                h = bilinear(m.mips[static_cast<std::size_t>(level - 1)], w, ht, fu, fv);
            }
            if (std::isnan(h))
                continue;
            natural += (m.datum + h - natural) * rim(m, at.edge);
        }
        return natural;
    }

    double MeshSource::height_on_sphere(const sl::dvec3 &direction, double radius) const
    {
        return apply(direction, source_->height_on_sphere(direction, radius), 0.0);
    }

    double MeshSource::height_on_sphere_lod(const sl::dvec3 &direction, double radius, double cell_size) const
    {
        return apply(direction, source_->height_on_sphere_lod(direction, radius, cell_size), cell_size);
    }

    bool MeshSource::colour(const sl::dvec3 &direction, float rgb[3]) const
    {
        bool changed = false;
        for (const Placed &m : meshes_)
        {
            Local at;
            if (!m.use_colour || !locate(m, direction, radius_, at))
                continue;
            const MeshGrid &g = *m.grid;
            const int i = std::clamp(static_cast<int>(std::lround((at.u + 0.5 * (g.width - 1) * g.cell) / g.cell)), 0, g.width - 1);
            const int j = std::clamp(static_cast<int>(std::lround((at.v + 0.5 * (g.height - 1) * g.cell) / g.cell)), 0, g.height - 1);
            const std::size_t k = static_cast<std::size_t>(j) * g.width + i;
            if (std::isnan(g.heights[k]))
                continue;
            const double t = rim(m, at.edge);
            for (int c = 0; c < 3; ++c)
                rgb[c] += static_cast<float>((g.colours[k * 3 + static_cast<std::size_t>(c)] / 255.0f - rgb[c]) * t);
            changed = true;
        }
        return changed;
    }

    bool MeshSource::colour_may_overlap(const sl::dvec3 &centre, double angular_radius) const
    {
        for (const Placed &m : meshes_)
        {
            if (!m.use_colour)
                continue;
            const double hw = 0.5 * (m.grid->width - 1) * m.grid->cell;
            const double hh = 0.5 * (m.grid->height - 1) * m.grid->cell;
            const double extent = (std::hypot(m.x, m.z) + std::hypot(hw, hh)) / radius_;
            if (extent >= 1.0)
                return true;
            const double separation = std::acos(std::clamp(glm::dot(centre, m.origin), -1.0, 1.0));
            if (separation <= std::min(angular_radius, pi) + std::asin(extent))
                return true;
        }
        return false;
    }

    std::vector<std::pair<sl::dvec3, double>> MeshSource::footprints() const
    {
        std::vector<std::pair<sl::dvec3, double>> out;
        for (const Placed &m : meshes_)
        {
            const double hw = 0.5 * (m.grid->width - 1) * m.grid->cell, hh = 0.5 * (m.grid->height - 1) * m.grid->cell;
            const sl::dvec3 centre = glm::normalize(m.origin + (m.right * m.x + m.forward * m.z) / radius_);
            out.emplace_back(centre, std::sqrt(hw * hw + hh * hh));
        }
        return out;
    }
}
