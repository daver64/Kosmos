#pragma once

#include "model.h"

namespace shipedit
{
    enum class RocketPartRole
    {
        first,
        second,
        upper,
        booster,
        fairing
    };

    struct RocketPart
    {
        std::string id;
        std::string name;
        RocketPartRole role;
        std::vector<int> stages;
        int anchor = -1;
        std::vector<std::string> ports;
    };

    // rocket_builder.parts entries have an id, name, role and stages array. Each stage reference
    // has an expanded-stage index and expected name, so source edits cannot silently change a part.
    // Boosters and fairings also specify an anchor reference for their source-relative geometry.
    // Missing metadata is an empty catalog. Invalid metadata leaves the output unchanged.
    bool rocket_parts(const Document &source, std::vector<RocketPart> &parts, std::string &error);

    // Axial roles are ordered bottom to top; boosters and fairings attach independently.
    bool valid_rocket_roles(const std::vector<RocketPartRole> &roles, std::string &error);
}
