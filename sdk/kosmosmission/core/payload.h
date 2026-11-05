// Payload: what a mission carries (ships and loose mass) and whether it fits in the launcher's payload volumes.
#pragma once

#include "budget.h"

#include <string>
#include <vector>

namespace km
{
    /** One ship in a payload. Ports are indices into that ship's ports, for the scenario's "dock" block. */
    struct StackMember
    {
        std::string ship;      // the ship's name, as a scenario names it
        std::string ship_file; // its ship.json
        double mass = 0;       // kg, wet
        int mount_port = 0;    // the port it attaches to what is below it (its "Aft", cradle or only port)
        int carry_port = -1;   // the port the next member attaches to, or -1
    };

    /**
     * What a launcher carries: one ship, or a stack of them (a Centaur carrying a Fregat carrying a probe), listed from the bottom up.
     * The stack rides in a payload volume as one cylinder or box: the widest member across, the members' lengths added.
     */
    struct PayloadItem
    {
        std::string label;     // "Luna E3", or "Centaur G > Fregat > Luna E3"
        int count = 1;
        double mass = 0;       // kg each, whole stack
        PayloadSize size;      // unknown = mass only, not checked for fit
        std::string bay;       // a bay name, or empty to let the planner choose
        std::vector<StackMember> members;
        bool powered = false;  // the stack's own stages add to the mission's delta-v (only for a single stack, count 1)
        double dv = 0;         // delta-v the stack's stages give, carrying nothing further
    };

    /** A payload from ships listed bottom to top. */
    PayloadItem make_payload(const std::vector<ShipSpec> &stack, int count = 1, bool powered = false);

    /** Delta-v the carried stacks add: the best powered single stack. */
    double powered_dv(const std::vector<PayloadItem> &items);

    struct BayFit
    {
        std::string bay;
        double length = 0, used_length = 0, mass = 0, max_mass = 0;
        std::vector<std::string> contents; // "12 x Luna E3"
    };

    /** Which bay one unit of an item went to (index into the bays), for building the scenario. */
    struct Placement
    {
        int item = 0, bay = 0;
    };

    struct FitReport
    {
        bool checked = false; // false when nothing sized was carried or the ship has no payload volume
        bool ok = true;
        std::vector<BayFit> bays;
        std::vector<std::string> problems;
        std::vector<Placement> placed;
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
