#pragma once

#include "sim.h"

#include <functional>

namespace kosmos
{
    /** What `--selftest` was asked to do. */
    struct SelftestOptions
    {
        /** Simulated seconds to run. */
        double seconds = 0.0;
        /** Warp index to use during a coast phase of the ascent autopilot. */
        double warp = 0.0;
        bool gear_up = false;
        /** Release a bomb this many seconds in (negative for none); `drop_atom` releases the last hardpoint instead. */
        double drop = -1.0;
        bool drop_atom = false;
        /** Run the arsenal checks (kit, weapons, charges, blast damage, bomb rack) instead of a flight. */
        bool arsenal = false;
        /** Run the ship weapon checks (beam, shields, flak, light cap) instead of a flight. */
        bool weapons = false;
        bool timed_burn = false;
        bool orbital_sas = false;
        bool ui_windows = false;
        bool vessel_ascent = false;
        bool save = false;
        bool orbitcomp = false;
        /** Apply the app's active-ship instrument selection in handoff tests. */
        std::function<void()> select_instruments;
        /** Hold this throttle (negative leaves the controls alone). */
        double throttle = -1.0;
    };

    /** Fly `ship` headlessly, printing a report. Returns 0 on success; the arsenal checks return 1 if any fail. */
    int run_selftest(Sim &sim, Ship *ship, const SelftestOptions &options);
} // namespace kosmos
