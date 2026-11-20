#pragma once

#include "../sdk/shipedit/src/rocket_assembly.h"
#include "ship.h"

#include <memory>

namespace kosmos::app
{
    struct RocketBuilderState
    {
        struct Source
        {
            std::string ship;
            shipedit::Document document;
            std::vector<shipedit::RocketPart> parts;
        };
        struct Payload
        {
            std::string ship;
            int carrier = -1, carrier_port = 0, own_port = 0;
        };
        bool open = false, active = false, preview_valid = false, saved = false;
        int base = 0, source = 0, part = 0, host = 0, selection = 0;
        int payload_ship = 0, payload_carrier = 0, payload_carrier_port = 0, payload_own_port = 0;
        float move_distance = 1.0f;
        char name[128] = "New Rocket", id[128] = "new-rocket";
        std::string base_id, body, status, fit_status, saved_directory;
        std::unique_ptr<shipedit::RocketAssembly> assembly;
        std::vector<Source> sources;
        std::vector<Payload> payloads;
        ShipSpec preview;
        sl::dvec3 pan{0.0};
    };
}
