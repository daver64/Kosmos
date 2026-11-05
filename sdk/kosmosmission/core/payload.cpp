#include "payload.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <set>

namespace km
{
    namespace
    {
        constexpr double EPS = 1e-6;

        // A cross-section: a circle (a = diameter) or a rectangle (a x b), and how far it runs along the bay axis.
        struct Shape
        {
            bool round = true;
            double a = 0, b = 0, len = 0;
        };

        std::vector<Shape> orientations(const PayloadSize &s)
        {
            std::vector<Shape> out;
            if (s.round)
            {
                out.push_back({true, s.diameter, 0, s.length});
                if (std::fabs(s.length - s.diameter) > EPS)
                    out.push_back({false, s.diameter, s.length, s.diameter}); // lying across the bay
            }
            else
            {
                const double d[3] = {s.width, s.height, s.length};
                for (int axis = 0; axis < 3; ++axis)
                {
                    const double p = d[(axis + 1) % 3], q = d[(axis + 2) % 3];
                    out.push_back({false, p, q, d[axis]});
                    if (std::fabs(p - q) > EPS)
                        out.push_back({false, q, p, d[axis]});
                }
            }
            std::stable_sort(out.begin(), out.end(), [](const Shape &x, const Shape &y) { return x.len < y.len; });
            return out;
        }

        struct Placed
        {
            Shape s;
            double x, y;
        };

        struct Layer
        {
            double len = 0;
            std::vector<Placed> placed;
            std::set<std::string> failed; // shapes that did not fit here; a layer only fills up, so they never will
        };

        struct BayState
        {
            PayloadBay bay;
            std::vector<Layer> layers;
            double mass = 0;
            std::map<std::string, int> contents;
            std::vector<std::string> order;
            double used() const
            {
                double u = 0;
                for (const Layer &l : layers)
                    u += l.len;
                return u;
            }
        };

        bool circle_rect_overlap(double cx, double cy, double r, double rx, double ry, double w, double h)
        {
            const double nx = std::clamp(cx, rx - w / 2, rx + w / 2), ny = std::clamp(cy, ry - h / 2, ry + h / 2);
            return std::hypot(cx - nx, cy - ny) < r - EPS;
        }

        bool overlap(const Shape &a, double ax, double ay, const Shape &b, double bx, double by)
        {
            if (a.round && b.round)
                return std::hypot(ax - bx, ay - by) < (a.a + b.a) / 2 - EPS;
            if (!a.round && !b.round)
                return std::fabs(ax - bx) < (a.a + b.a) / 2 - EPS && std::fabs(ay - by) < (a.b + b.b) / 2 - EPS;
            return a.round ? circle_rect_overlap(ax, ay, a.a / 2, bx, by, b.a, b.b) : circle_rect_overlap(bx, by, b.a / 2, ax, ay, a.a, a.b);
        }

        bool inside(const PayloadBay &bay, const Shape &s, double x, double y)
        {
            if (bay.round)
            {
                if (s.round)
                    return std::hypot(x, y) + s.a / 2 <= bay.radius + EPS;
                for (int sx : {-1, 1})
                    for (int sy : {-1, 1})
                        if (std::hypot(x + sx * s.a / 2, y + sy * s.b / 2) > bay.radius + EPS)
                            return false;
                return true;
            }
            const double hw = s.a / 2, hh = s.round ? s.a / 2 : s.b / 2;
            return std::fabs(x) + hw <= bay.width / 2 + EPS && std::fabs(y) + hh <= bay.height / 2 + EPS;
        }

        bool find_position(const PayloadBay &bay, const Layer &layer, const Shape &s, double &px, double &py)
        {
            auto free_at = [&](double x, double y) {
                if (!inside(bay, s, x, y))
                    return false;
                for (const Placed &p : layer.placed)
                    if (overlap(s, x, y, p.s, p.x, p.y))
                        return false;
                return true;
            };
            const double w = bay.round ? 2 * bay.radius : bay.width, h = bay.round ? 2 * bay.radius : bay.height;
            const double step = std::max(0.05, std::max(w, h) / 150);
            for (double y = -h / 2; y <= h / 2 + EPS; y += step)
                for (double x = -w / 2; x <= w / 2 + EPS; x += step)
                    if (free_at(x, y))
                    {
                        px = x;
                        py = y;
                        return true;
                    }
            if (free_at(0, 0)) // an exact fit between the walls can fall between grid points
            {
                px = py = 0;
                return true;
            }
            return false;
        }

        std::string key_of(const Shape &s)
        {
            char b[96];
            std::snprintf(b, sizeof b, "%d/%.4f/%.4f", s.round ? 1 : 0, s.a, s.b);
            return b;
        }

        bool place_in_bay(BayState &st, const PayloadSize &size, double mass, const std::string &label)
        {
            if (st.bay.max_mass > 0 && st.mass + mass > st.bay.max_mass + EPS)
                return false;
            const std::vector<Shape> shapes = orientations(size);
            bool placed = false;
            for (Layer &layer : st.layers)
            {
                for (const Shape &s : shapes)
                {
                    if (s.len > layer.len + EPS || layer.failed.count(key_of(s)))
                        continue;
                    double x, y;
                    if (find_position(st.bay, layer, s, x, y))
                    {
                        layer.placed.push_back({s, x, y});
                        placed = true;
                        break;
                    }
                    layer.failed.insert(key_of(s));
                }
                if (placed)
                    break;
            }
            if (!placed)
            {
                const double room = st.bay.length - st.used();
                for (const Shape &s : shapes)
                {
                    if (s.len > room + EPS)
                        continue;
                    Layer layer;
                    layer.len = s.len;
                    double x, y;
                    if (find_position(st.bay, layer, s, x, y))
                    {
                        layer.placed.push_back({s, x, y});
                        st.layers.push_back(std::move(layer));
                        placed = true;
                        break;
                    }
                }
            }
            if (!placed)
                return false;
            st.mass += mass;
            if (!st.contents.count(label))
                st.order.push_back(label);
            ++st.contents[label];
            return true;
        }

        double volume(const PayloadSize &s)
        {
            return s.round ? 3.14159265 / 4 * s.diameter * s.diameter * s.length : s.width * s.height * s.length;
        }
    } // namespace

    namespace
    {
        std::string lower(std::string s)
        {
            for (char &c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        int port_named(const std::vector<std::string> &ports, std::initializer_list<const char *> names, int skip = -1)
        {
            for (const char *want : names)
                for (size_t i = 0; i < ports.size(); ++i)
                    if (static_cast<int>(i) != skip && lower(ports[i]) == want)
                        return static_cast<int>(i);
            return -1;
        }
    } // namespace

    PayloadItem make_payload(const std::vector<ShipSpec> &stack, int count, bool powered)
    {
        PayloadItem item;
        item.count = std::max(1, count);
        item.powered = powered;
        ShipSpec all;
        bool known = !stack.empty(), round = true;
        double dia = 0, w = 0, h = 0, len = 0;
        for (const ShipSpec &s : stack)
        {
            StackMember m;
            m.ship = s.name;
            m.ship_file = s.file;
            m.mass = s.mass();
            int mount = port_named(s.ports, {"aft", "launcher", "launcher cradle", "cradle", "shuttle end", "docking"});
            m.mount_port = s.ports.empty() ? -1 : (mount >= 0 ? mount : 0); // -1: no port to dock with
            m.carry_port = port_named(s.ports, {"payload", "nose", "top", "fore"}, m.mount_port);
            item.members.push_back(m);
            item.label += (item.label.empty() ? "" : " > ") + s.name;
            item.mass += m.mass;
            all.stages.insert(all.stages.end(), s.stages.begin(), s.stages.end());
            known = known && s.size.known;
            if (!s.size.known)
                continue;
            round = round && s.size.round;
            dia = std::max(dia, s.size.round ? s.size.diameter : std::max(s.size.width, s.size.height));
            w = std::max(w, s.size.round ? s.size.diameter : s.size.width);
            h = std::max(h, s.size.round ? s.size.diameter : s.size.height);
            len += s.size.length;
        }
        if (known)
            item.size = round ? PayloadSize{true, true, dia, 0, 0, len} : PayloadSize{true, false, 0, w, h, len};
        item.dv = budget(all, 0.0).total;
        return item;
    }

    double powered_dv(const std::vector<PayloadItem> &items)
    {
        double best = 0;
        for (const PayloadItem &i : items)
            if (i.powered && i.count == 1)
                best = std::max(best, i.dv);
        return best;
    }

    double items_mass(const std::vector<PayloadItem> &items)
    {
        double m = 0;
        for (const PayloadItem &i : items)
            m += i.mass * std::max(0, i.count);
        return m;
    }

    FitReport check_fit(const std::vector<PayloadBay> &bays, const std::vector<PayloadItem> &items)
    {
        FitReport rep;
        std::vector<BayState> states;
        for (const PayloadBay &b : bays)
        {
            BayState st;
            st.bay = b;
            states.push_back(std::move(st));
        }

        struct Unit
        {
            const PayloadItem *item;
            int index;
        };
        std::vector<Unit> units;
        for (size_t k = 0; k < items.size(); ++k)
            if (items[k].size.known && items[k].size.length > 0)
                for (int n = 0; n < items[k].count; ++n)
                    units.push_back({&items[k], static_cast<int>(k)});
        if (units.empty())
            return rep;
        if (bays.empty())
        {
            rep.problems.push_back("This ship has no payload volume defined (payload_bays in its ship.json), so fit is not checked.");
            return rep;
        }
        rep.checked = true;
        std::stable_sort(units.begin(), units.end(), [](const Unit &a, const Unit &b) { return volume(a.item->size) > volume(b.item->size); });

        std::map<std::string, int> failed;
        for (const Unit &u : units)
        {
            const PayloadItem &item = *u.item;
            bool done = false;
            for (size_t bi = 0; bi < states.size(); ++bi)
            {
                BayState &st = states[bi];
                if (!item.bay.empty() && item.bay != st.bay.name)
                    continue;
                if (place_in_bay(st, item.size, item.mass, item.label))
                {
                    rep.placed.push_back({u.index, static_cast<int>(bi)});
                    done = true;
                    break;
                }
            }
            if (!done)
                ++failed[item.label];
        }

        for (const BayState &st : states)
        {
            BayFit f;
            f.bay = st.bay.name;
            f.length = st.bay.length;
            f.used_length = st.used();
            f.mass = st.mass;
            f.max_mass = st.bay.max_mass;
            for (const std::string &label : st.order)
                f.contents.push_back(std::to_string(st.contents.at(label)) + " x " + label);
            rep.bays.push_back(std::move(f));
        }
        for (const auto &kv : failed)
        {
            rep.ok = false;
            rep.problems.push_back(std::to_string(kv.second) + " x " + kv.first + " do not fit in the payload volume(s).");
        }
        return rep;
    }

    std::string FitReport::text() const
    {
        std::string out;
        char buf[256];
        for (const BayFit &b : bays)
        {
            if (b.contents.empty())
                continue;
            std::snprintf(buf, sizeof buf, "%s: %.1f of %.1f m used", b.bay.c_str(), b.used_length, b.length);
            out += buf;
            if (b.max_mass > 0)
            {
                std::snprintf(buf, sizeof buf, ", %.0f of %.0f kg", b.mass, b.max_mass);
                out += buf;
            }
            out += " (";
            for (size_t i = 0; i < b.contents.size(); ++i)
                out += (i ? ", " : "") + b.contents[i];
            out += ")\n";
        }
        for (const std::string &p : problems)
            out += p + "\n";
        if (!out.empty() && out.back() == '\n')
            out.pop_back();
        return out;
    }
} // namespace km
