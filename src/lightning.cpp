#include "lightning.h"

#include <algorithm>
#include <cmath>

namespace kosmos
{
    namespace
    {
        constexpr double speed_of_sound = 343.0;
        constexpr double pi = 3.14159265358979323846;
        constexpr double bolt_life = 0.45;

        void put16(std::vector<std::uint8_t> &out, int v)
        {
            out.push_back(static_cast<std::uint8_t>(v & 0xff));
            out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
        }
        void put32(std::vector<std::uint8_t> &out, std::uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
        }

        // Thunder as a mono WAV in memory: a crack that lowers into a long rumble. `kind` 0 is close (sharp), 2 is far (all rumble).
        std::vector<std::uint8_t> make_thunder_wav(int kind)
        {
            const int rate = 22050;
            const double seconds = kind == 0 ? 3.5 : (kind == 1 ? 5.0 : 6.5);
            const int count = static_cast<int>(rate * seconds);
            std::uint32_t seed = 12345u + 777u * static_cast<std::uint32_t>(kind);
            const auto noise = [&seed] {
                seed = seed * 1664525u + 1013904223u;
                return static_cast<double>(static_cast<std::int32_t>(seed)) / 2147483648.0;
            };
            std::vector<std::int16_t> pcm(static_cast<std::size_t>(count));
            double low1 = 0.0, low2 = 0.0, brown = 0.0;
            const double lfo_a = 2.0 + 1.3 * kind, lfo_b = 5.1 + 0.9 * kind;
            for (int i = 0; i < count; ++i)
            {
                const double t = static_cast<double>(i) / rate;
                // The cutoff falls as the sound travels and spreads out.
                const double cutoff = (kind == 0 ? 2200.0 : (kind == 1 ? 900.0 : 380.0)) * std::exp(-t * 0.9) + 70.0;
                const double a = 1.0 - std::exp(-2.0 * pi * cutoff / rate);
                const double white = noise();
                low1 += a * (white - low1);
                low2 += a * (low1 - low2);
                brown = brown * 0.998 + white * 0.03;
                const double attack = kind == 0 ? 0.01 : (kind == 1 ? 0.12 : 0.45);
                const double envelope = std::min(1.0, t / attack) * std::exp(-t / (kind == 0 ? 1.1 : 1.7));
                const double roll = 0.62 + 0.38 * std::sin(2.0 * pi * lfo_a * t + 1.0) * std::sin(2.0 * pi * lfo_b * t);
                const double crack = kind == 0 ? white * std::exp(-t * 28.0) * 0.9 : 0.0;
                const double v = (low2 * 3.2 + brown * 1.4) * roll * envelope + crack;
                pcm[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(std::clamp(v, -1.0, 1.0) * 30000.0);
            }
            std::vector<std::uint8_t> wav = {'R', 'I', 'F', 'F'};
            put32(wav, 36u + static_cast<std::uint32_t>(count) * 2u);
            for (char c : {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '})
                wav.push_back(static_cast<std::uint8_t>(c));
            put32(wav, 16);
            put16(wav, 1);
            put16(wav, 1);
            put32(wav, static_cast<std::uint32_t>(rate));
            put32(wav, static_cast<std::uint32_t>(rate) * 2u);
            put16(wav, 2);
            put16(wav, 16);
            for (char c : {'d', 'a', 't', 'a'})
                wav.push_back(static_cast<std::uint8_t>(c));
            put32(wav, static_cast<std::uint32_t>(count) * 2u);
            for (std::int16_t s : pcm)
                put16(wav, s);
            return wav;
        }

        struct Point
        {
            double x, y, z;
        };

        // A jagged line by midpoint displacement.
        void jag(const Point &a, const Point &b, double spread, int depth, double &state, std::vector<std::pair<Point, Point>> &out)
        {
            if (depth == 0)
            {
                out.emplace_back(a, b);
                return;
            }
            const auto rnd = [&state] {
                state = std::fmod(state * 9301.0 + 49297.0, 233280.0);
                return state / 233280.0 * 2.0 - 1.0;
            };
            const Point m{(a.x + b.x) / 2 + rnd() * spread, (a.y + b.y) / 2 + rnd() * spread, (a.z + b.z) / 2};
            jag(a, m, spread * 0.55, depth - 1, state, out);
            jag(m, b, spread * 0.55, depth - 1, state, out);
        }
    }

    Lightning::~Lightning() { release(); }

    double Lightning::random()
    {
        rng_ = rng_ * 1664525u + 1013904223u;
        return static_cast<double>(rng_ >> 8) / 16777216.0;
    }

    sl::Sample *Lightning::thunder_sample(int kind)
    {
        if (!samples_[kind])
        {
            const std::vector<std::uint8_t> wav = make_thunder_wav(kind);
            samples_[kind] = sl::load_sample_from_memory(wav.data(), wav.size());
        }
        return samples_[kind];
    }

    void Lightning::release()
    {
        for (sl::Sample *&s : samples_)
            if (s)
            {
                sl::destroy_sample(s);
                s = nullptr;
            }
        strikes_.clear();
    }

    void Lightning::update(double dt, double thunder, double sound_volume)
    {
        clock_ += dt;
        volume_ = sound_volume;
        if (thunder > 0.05)
        {
            countdown_ -= dt;
            if (countdown_ <= 0.0)
            {
                Strike s;
                s.bearing = random() * 2.0 * pi;
                s.distance = 500.0 + 6500.0 * random() * random();
                s.seed = 1.0 + std::floor(random() * 1000.0);
                strikes_.push_back(s);
                countdown_ = (3.0 + 10.0 * random()) / std::max(0.3, thunder);
            }
        }
        else
            countdown_ = std::max(countdown_, 2.0);

        double flash = 0.0;
        for (Strike &s : strikes_)
        {
            s.age += dt;
            // Return strokes: two or three quick pulses, brighter the nearer the strike.
            const double near_gain = std::clamp(1.2 - s.distance / 6000.0, 0.15, 1.0);
            for (const double start : {0.0, 0.11, 0.24})
                if (s.age >= start)
                    flash = std::max(flash, near_gain * std::exp(-(s.age - start) * 14.0) * (start == 0.0 ? 1.0 : 0.7));
            if (s.thunder_pending && s.age * 1.0 >= s.distance / speed_of_sound)
            {
                s.thunder_pending = false;
                const int kind = s.distance < 1500.0 ? 0 : (s.distance < 4000.0 ? 1 : 2);
                if (volume_ > 0.0)
                    if (sl::Sample *sample = thunder_sample(kind))
                    {
                        const double loud = std::clamp(1.1 - s.distance / 9000.0, 0.25, 1.0) * volume_;
                        sl::play_sample(sample, static_cast<int>(255.0 * loud), 128 + static_cast<int>(60.0 * std::sin(s.bearing)), 900 + static_cast<int>(200.0 * random()), 0);
                    }
            }
        }
        flash_ = flash;
        strikes_.erase(std::remove_if(strikes_.begin(), strikes_.end(), [](const Strike &s) { return !s.thunder_pending && s.age > bolt_life; }), strikes_.end());
    }

    int Lightning::bolt_vertices(double ground_z, double cloud_z, sl::Vertex3D *out, int max_quads) const
    {
        int quads = 0;
        for (const Strike &s : strikes_)
        {
            if (s.age > bolt_life || cloud_z - ground_z < 80.0)
                continue;
            // Flickers through its strokes.
            const double flicker = (s.age < 0.07 ? 1.0 : (s.age < 0.11 ? 0.2 : (s.age < 0.2 ? 0.9 : (s.age < 0.26 ? 0.3 : std::exp(-(s.age - 0.26) * 12.0)))));
            const double alpha = std::clamp(flicker, 0.0, 1.0);
            if (alpha < 0.03)
                continue;
            const Point top{std::cos(s.bearing) * s.distance, std::sin(s.bearing) * s.distance, cloud_z};
            const Point bottom{top.x + (s.seed - 500.0) * 0.1, top.y, ground_z};
            double state = s.seed;
            std::vector<std::pair<Point, Point>> segments, branches;
            jag(top, bottom, (cloud_z - ground_z) * 0.12, 5, state, segments);
            for (std::size_t i = 4; i < segments.size(); i += 7)
            {
                const Point &from = segments[i].first;
                const double spread = 120.0 + 120.0 * (state / 233280.0);
                const Point end{from.x + (std::fmod(state * 3.1, 2.0) - 1.0) * 450.0, from.y + (std::fmod(state * 1.7, 2.0) - 1.0) * 450.0, from.z - spread * 4.0};
                jag(from, end, 80.0, 3, state, branches);
                state = std::fmod(state * 9301.0 + 49297.0, 233280.0);
            }
            const auto emit = [&](const std::pair<Point, Point> &seg, double width, double gain) {
                if (quads >= max_quads)
                    return;
                const Point mid{(seg.first.x + seg.second.x) / 2, (seg.first.y + seg.second.y) / 2, (seg.first.z + seg.second.z) / 2};
                const double dx = seg.second.x - seg.first.x, dy = seg.second.y - seg.first.y, dz = seg.second.z - seg.first.z;
                // Perpendicular to both the segment and the line of sight, so the strip always faces the camera.
                double sx = dy * mid.z - dz * mid.y, sy = dz * mid.x - dx * mid.z, sz = dx * mid.y - dy * mid.x;
                const double length = std::sqrt(sx * sx + sy * sy + sz * sz);
                if (length < 1.0e-9)
                    return;
                const double w = width / length;
                sx *= w;
                sy *= w;
                sz *= w;
                const Point c[4] = {{seg.first.x - sx, seg.first.y - sy, seg.first.z - sz}, {seg.first.x + sx, seg.first.y + sy, seg.first.z + sz},
                                    {seg.second.x - sx, seg.second.y - sy, seg.second.z - sz}, {seg.second.x + sx, seg.second.y + sy, seg.second.z + sz}};
                for (int k = 0; k < 4; ++k)
                {
                    sl::Vertex3D &v = out[quads * 4 + k];
                    v = sl::Vertex3D();
                    v.position[0] = static_cast<float>(c[k].x);
                    v.position[1] = static_cast<float>(c[k].y);
                    v.position[2] = static_cast<float>(c[k].z);
                    v.colour[0] = 0.8f;
                    v.colour[1] = 0.86f;
                    v.colour[2] = 1.0f;
                    v.colour[3] = static_cast<float>(alpha * gain);
                }
                ++quads;
            };
            const double base_width = std::max(0.9, s.distance * 0.0028);
            for (const auto &seg : segments)
                emit(seg, base_width, 1.0);
            for (const auto &seg : branches)
                emit(seg, base_width * 0.45, 0.6);
        }
        return quads * 4;
    }
}
