#pragma once

#include "json.hpp"
#include "reentry.h"

namespace kosmos
{
    /** Read the "thermal", "hypersonic" and "parachutes" entries of a ship or equipment file. Missing keys keep their defaults. */
    void read_thermal(const nlohmann::json &json, ThermalSpec &spec);
    void read_hypersonic(const nlohmann::json &json, HypersonicSpec &spec);
    void read_parachutes(const nlohmann::json &json, std::vector<ParachuteSpec> &list);
} // namespace kosmos
