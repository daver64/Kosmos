#include "vsop87.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace kosmos
{
    namespace
    {
        constexpr double au = 1.495978707e11;
        constexpr double seconds_per_millennium = 365250.0 * 86400.0;
    } // namespace

    std::string Vsop87::code_for(const std::string &planet)
    {
        static const char *const table[][2] = {{"Mercury", "mer"}, {"Venus", "ven"}, {"Earth", "ear"}, {"Mars", "mar"},
                                               {"Jupiter", "jup"}, {"Saturn", "sat"}, {"Uranus", "ura"}, {"Neptune", "nep"}};
        for (const auto &row : table)
            if (planet == row[0])
                return row[1];
        return "";
    }

    bool Vsop87::load(const std::string &file)
    {
        std::ifstream in(file);
        if (!in)
            return false;
        for (auto &row : series_)
            for (auto &s : row)
                s.clear();
        int variable = -1, power = -1;
        std::string line;
        bool any = false;
        while (std::getline(in, line))
        {
            if (line.find("VSOP87") != std::string::npos)
            {
                const std::size_t v = line.find("VARIABLE");
                const std::size_t p = line.find("*T**");
                if (v == std::string::npos || p == std::string::npos)
                    return false;
                variable = std::atoi(line.c_str() + v + 8) - 1;
                power = std::atoi(line.c_str() + p + 4);
                if (variable < 0 || variable > 2 || power < 0 || power > 5)
                    return false;
                continue;
            }
            if (variable < 0)
                continue;
            std::istringstream ss(line);
            std::vector<std::string> tokens;
            for (std::string t; ss >> t;)
                tokens.push_back(t);
            if (tokens.size() < 4)
                continue;
            const std::size_t n = tokens.size();
            series_[variable][power].push_back({std::atof(tokens[n - 3].c_str()), std::atof(tokens[n - 2].c_str()), std::atof(tokens[n - 1].c_str())});
            any = true;
        }
        return any;
    }

    void Vsop87::state(double julian_date, sl::dvec3 &position, sl::dvec3 &velocity) const
    {
        const double t = (julian_date - 2451545.0) / 365250.0;
        double value[3] = {}, rate[3] = {};
        for (int v = 0; v < 3; ++v)
            for (int k = 0; k < 6; ++k)
            {
                const double tk = std::pow(t, k);
                const double tk1 = k > 0 ? k * std::pow(t, k - 1) : 0.0;
                for (const Term &term : series_[v][k])
                {
                    const double arg = term.phase + term.frequency * t;
                    value[v] += term.amplitude * tk * std::cos(arg);
                    rate[v] += term.amplitude * (tk1 * std::cos(arg) - tk * term.frequency * std::sin(arg));
                }
            }
        position = sl::dvec3(value[0], value[2], -value[1]) * au;
        velocity = sl::dvec3(rate[0], rate[2], -rate[1]) * au / seconds_per_millennium;
    }
} // namespace kosmos
