#pragma once

#include "flight.h"

#include <string>
#include <vector>

namespace kosmos
{
    /** A VSOP87 version A series file (heliocentric rectangular ecliptic J2000) for one planet. */
    class Vsop87
    {
    public:
        /** Loads data/vsop87/VSOP87A.<code> style files; returns false if the file is missing or malformed. */
        bool load(const std::string &file);

        /**
         * Heliocentric state at a Julian date (TDB) in the simlib world frame (ecliptic X, Z, -Y; the pole is +Y),
         * in metres and metres per second.
         */
        void state(double julian_date, sl::dvec3 &position, sl::dvec3 &velocity) const;

        /** The three-letter VSOP87 file suffix for a planet name, or an empty string. */
        static std::string code_for(const std::string &planet);

    private:
        struct Term
        {
            double amplitude, phase, frequency;
        };
        // series_[variable][power]
        std::vector<Term> series_[3][6];
    };
} // namespace kosmos
