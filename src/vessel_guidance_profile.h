#pragma once

#include <string_view>

namespace kosmos
{
    enum class VesselGuidanceProfile { none, ares, deltaglider, shuttle_a, soyuz };
    enum class VesselAscentPhase { inactive, liftoff, climb, orbit_burn, coast, circularise };

    inline const char *vessel_ascent_phase_name(VesselAscentPhase phase)
    {
        switch (phase)
        {
        case VesselAscentPhase::inactive: return "";
        case VesselAscentPhase::liftoff: return "Liftoff";
        case VesselAscentPhase::climb: return "Climb";
        case VesselAscentPhase::orbit_burn: return "Orbit burn";
        case VesselAscentPhase::coast: return "Coast";
        case VesselAscentPhase::circularise: return "Circularise";
        }
        return "Invalid ascent phase";
    }

    inline VesselGuidanceProfile vessel_guidance_profile(std::string_view name)
    {
        if (name == "Ares Spider")
            return VesselGuidanceProfile::ares;
        if (name == "CVEL Deltaglider" || name == "CVEL Deltaglider (Nuclear)")
            return VesselGuidanceProfile::deltaglider;
        if (name == "CVEL Shuttle-A" || name == "CVEL Shuttle-A (Nuclear)" || name == "CVEL Shuttle-AQ")
            return VesselGuidanceProfile::shuttle_a;
        if (name == "CVEL Soyuz TMA")
            return VesselGuidanceProfile::soyuz;
        return VesselGuidanceProfile::none;
    }

    inline bool vessel_ascent_request_matches(VesselGuidanceProfile profile, std::string_view request)
    {
        return (profile == VesselGuidanceProfile::ares && request == "Ares") ||
               (profile == VesselGuidanceProfile::deltaglider && request == "Deltaglider") ||
               (profile == VesselGuidanceProfile::shuttle_a && request == "Shuttle-A");
    }
}
