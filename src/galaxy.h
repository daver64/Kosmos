#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kosmos
{
    /** One catalogue entry. Positions are in light years with Sol at the origin and +Y toward the galactic north pole. */
    struct Star
    {
        std::string name;
        double x = 0.0, y = 0.0, z = 0.0;
        char spectral_class = 'G';
        /** Solar units. */
        double mass = 1.0, radius = 1.0, luminosity = 1.0;
        double temperature = 5772.0;
        float colour[3] = {1.0f, 0.95f, 0.8f};
        /** True for the hand-placed neighbours of Sol. */
        bool real = false;
    };

    /**
     * A deterministic procedural galaxy: the same seed always gives the same stars and the same planets around each one.
     *
     * Index 0 is Sol, loaded from data/sol.json. The next few entries are real neighbours with approximate positions, then
     * a local bubble of invented stars close enough to reach by hopping, then a spiral disc of tens of thousands of distant
     * stars for the galaxy map. System contents are generated on demand from the star's index, not stored.
     */
    class Galaxy
    {
    public:
        explicit Galaxy(std::uint64_t seed = 20240601, int local_stars = 400, int distant_stars = 24000);

        int size() const { return static_cast<int>(stars_.size()); }
        const Star &star(int index) const { return stars_[static_cast<std::size_t>(index)]; }
        /** Index by case-insensitive name, or -1. */
        int find(const std::string &name) const;
        double distance_ly(int a, int b) const;
        /** The n stars nearest to `from`, closest first, excluding it. */
        std::vector<int> nearest(int from, int count) const;
        /** Number of planets the generator will give the star (cheap; does not build the system). */
        int planet_count(int index) const;
        /**
         * The star system as JSON text in the format Universe::load_text reads: the star first, then planets, moons, an
         * optional belt and comet. One planet is always the "home" with launch terrain and an atmosphere. Not valid for
         * index 0, which is Sol.
         */
        std::string system_json(int index) const;
        /** Directory of hand-authored systems (<slug>.json); a star with a file there uses it instead of the generator. */
        void set_systems_dir(const std::string &dir) { systems_dir_ = dir; }
        /** Lower-case file stem for the star's name. */
        std::string system_slug(int index) const;

    private:
        std::string authored_json(int index) const;
        std::string systems_dir_;
        std::uint64_t seed_;
        std::vector<Star> stars_;
    };
} // namespace kosmos
