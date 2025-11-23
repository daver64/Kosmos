#include "../../include/Kosmos/PluginAPI.h"
#include <cstring>

// Simple example vessel plugin - Delta Glider style spacecraft

struct DeltaGliderData {
    double mainThrust;
    double fuel;
    char name[64];
};

extern "C" {

PLUGIN_EXPORT const char* GetPluginName() {
    return "Delta Glider";
}

PLUGIN_EXPORT const char* GetPluginVersion() {
    return "0.1.0";
}

PLUGIN_EXPORT int GetAPIVersion() {
    return KOSMOS_API_VERSION;
}

PLUGIN_EXPORT void* CreateVessel(void* engineInterface) {
    DeltaGliderData* vessel = new DeltaGliderData();
    vessel->mainThrust = 0.0;
    vessel->fuel = 1.0;
    std::strcpy(vessel->name, "Delta Glider IV");
    
    // TODO: Set up fuel tanks, thrusters, and thruster groups
    // This would use the engine interface to create vessel components
    
    return vessel;
}

PLUGIN_EXPORT void DestroyVessel(void* vessel) {
    if (vessel) {
        delete static_cast<DeltaGliderData*>(vessel);
    }
}

PLUGIN_EXPORT void Vessel_Update(void* vessel, double deltaTime) {
    if (!vessel) return;
    
    DeltaGliderData* dg = static_cast<DeltaGliderData*>(vessel);
    
    // Update vessel state
    if (dg->mainThrust > 0.0 && dg->fuel > 0.0) {
        dg->fuel -= dg->mainThrust * deltaTime * 0.001;
        if (dg->fuel < 0.0) dg->fuel = 0.0;
    }
}

PLUGIN_EXPORT void Vessel_PreStep(void* vessel, double deltaTime) {
    // Pre-physics step processing
}

PLUGIN_EXPORT void Vessel_PostStep(void* vessel, double deltaTime) {
    // Post-physics step processing
}

PLUGIN_EXPORT void Vessel_SetThrusterGroupLevel(void* vessel, int groupType, double level) {
    if (!vessel) return;
    
    DeltaGliderData* dg = static_cast<DeltaGliderData*>(vessel);
    
    // For this simple example, just set main thrust
    if (groupType == 0) {  // MAIN group
        dg->mainThrust = level;
    }
}

PLUGIN_EXPORT double Vessel_GetThrusterGroupLevel(void* vessel, int groupType) {
    if (!vessel) return 0.0;
    
    DeltaGliderData* dg = static_cast<DeltaGliderData*>(vessel);
    
    if (groupType == 0) {  // MAIN group
        return dg->mainThrust;
    }
    
    return 0.0;
}

} // extern "C"
