// Payload: what a mission carries (ships and loose mass) and whether it fits in the launcher's payload volumes.
#pragma once

#include "budget.h"

#include <string>
#include <vector>

namespace km
{
    struct PayloadItem
    {
        std::string label;     // ship name, or a name for loose cargo
        std::string ship_file; // the payload ship's ship.json, if it is a ship
        int count = 1;
        double mass = 0;       // kg each
        PayloadSize size;      // unknown = mass only, not checked for fit
        std::string bay;       // a bay name, or empty to let the planner choose
    };

    struct BayFit
    {
        std::string bay;
        double length = 0, used_length = 0, mass = 0, max_mass = 0;
        std::vector<std::string> contents; // "12 x Luna E3"
    };

    struct FitReport
    {
        bool checked = false; // false when nothing sized was carried or the ship has no payload volume
        bool ok = true;
        std::vector<BayFit> bays;
        std::vector<std::string> problems;
        std::string text() const;
    };

    double items_mass(const std::vector<PayloadItem> &items);

    /**
     * Places the items in the bays: items go into layers along the bay axis, and each layer is packed in the cross-section by trying
     * positions on a fine grid. Each item may be turned to whichever way round fits. Conservative: a real packing could be a little
     * tighter. Items with a named bay go there; the others go to the first bay that takes them.
     */
    FitReport check_fit(const std::vector<PayloadBay> &bays, const std::vector<PayloadItem> &items);
} // namespace km
