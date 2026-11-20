#pragma once

#include <istream>
#include <string>
#include <vector>

namespace kmap
{
    struct BodyInfo
    {
        std::string name, parent;
        double radius = 0.0;
    };

    bool read_body_catalog(std::istream &input, std::vector<BodyInfo> &bodies, std::string &error);
}
