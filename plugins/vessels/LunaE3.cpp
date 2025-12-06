#include "../../include/Kosmos/PluginAPI.h"
#include <cstring>
#include <string>

// LunaE3 probe vessel plugin
// A simple space probe with basic propulsion system

struct LunaE3Data {
    void* mesh;
    double mainThrustLevel;
    double rcsThrustLevel;
    double fuel;
    double fuelCapacity;
    char name[64];
    
    // Fuel tanks
    void* mainFuelTank;
    
    // Thrusters
    void* mainThruster;
    
    // Thruster groups
    void* mainGroup;
};

extern "C" {

PLUGIN_EXPORT const char* GetPluginName() {
    return "LunaE3";
}

PLUGIN_EXPORT const char* GetPluginVersion() {
    return "0.1.0";
}

PLUGIN_EXPORT int GetAPIVersion() {
    return KOSMOS_API_VERSION;
}

PLUGIN_EXPORT void* CreateVessel(void* engineInterface) {
    (void)engineInterface;  // Will use this later for creating components
    
    LunaE3Data* vessel = new LunaE3Data();
    vessel->mesh = nullptr;
    vessel->mainThrustLevel = 0.0;
    vessel->rcsThrustLevel = 0.0;
    vessel->fuel = 1.0;  // 100% fuel
    vessel->fuelCapacity = 500.0;  // 500 kg
    std::strcpy(vessel->name, "LunaE3");
    
    vessel->mainFuelTank = nullptr;
    vessel->mainThruster = nullptr;
    vessel->mainGroup = nullptr;
    
    // TODO: Use engineInterface to create actual fuel tanks and thrusters
    // For now this is a placeholder structure
    
    return vessel;
}

PLUGIN_EXPORT void DestroyVessel(void* vessel) {
    if (vessel) {
        delete static_cast<LunaE3Data*>(vessel);
    }
}

PLUGIN_EXPORT void Vessel_Update(void* vessel, double deltaTime) {
    if (!vessel) return;
    
    LunaE3Data* probe = static_cast<LunaE3Data*>(vessel);
    
    // Update fuel consumption based on thrust
    if (probe->mainThrustLevel > 0.0 && probe->fuel > 0.0) {
        // Simple fuel consumption model
        double fuelFlow = probe->mainThrustLevel * 0.1;  // kg/s at full thrust
        double consumed = fuelFlow * deltaTime;
        probe->fuel -= consumed / probe->fuelCapacity;
        if (probe->fuel < 0.0) probe->fuel = 0.0;
    }
}

PLUGIN_EXPORT void Vessel_PreStep(void* vessel, double deltaTime) {
    (void)vessel; 
    (void)deltaTime;
    // Pre-physics step processing
}

PLUGIN_EXPORT void Vessel_PostStep(void* vessel, double deltaTime) {
    (void)vessel; 
    (void)deltaTime;
    // Post-physics step processing
}

PLUGIN_EXPORT void Vessel_SetThrusterGroupLevel(void* vessel, int groupType, double level) {
    if (!vessel) return;
    
    LunaE3Data* probe = static_cast<LunaE3Data*>(vessel);
    
    // Group types: 0=MAIN, 1=RETRO, 2=HOVER, 3=RCS
    switch (groupType) {
        case 0:  // MAIN
            probe->mainThrustLevel = level;
            break;
        case 3:  // RCS
            probe->rcsThrustLevel = level;
            break;
        default:
            break;
    }
}

PLUGIN_EXPORT double Vessel_GetThrusterGroupLevel(void* vessel, int groupType) {
    if (!vessel) return 0.0;
    
    LunaE3Data* probe = static_cast<LunaE3Data*>(vessel);
    
    switch (groupType) {
        case 0:  // MAIN
            return probe->mainThrustLevel;
        case 3:  // RCS
            return probe->rcsThrustLevel;
        default:
            return 0.0;
    }
}

PLUGIN_EXPORT void* Vessel_CreateFuelTank(void* vessel, int fuelType, double capacity, const double* position) {
    (void)vessel;
    (void)fuelType;
    (void)capacity;
    (void)position;
    // TODO: Implement when engine interface is available
    return nullptr;
}

PLUGIN_EXPORT void Vessel_SetFuelLevel(void* tank, double fraction) {
    (void)tank;
    (void)fraction;
    // TODO: Implement
}

PLUGIN_EXPORT double Vessel_GetFuelLevel(void* tank) {
    (void)tank;
    // TODO: Implement
    return 1.0;
}

PLUGIN_EXPORT void* Vessel_CreateThruster(void* vessel, const ThrusterSpecC* spec,
                                          const double* position, const double* direction) {
    (void)vessel;
    (void)spec;
    (void)position;
    (void)direction;
    // TODO: Implement when engine interface is available
    return nullptr;
}

PLUGIN_EXPORT void Vessel_AttachThrusterToTank(void* thruster, void* fuelTank, void* oxidizerTank) {
    (void)thruster;
    (void)fuelTank;
    (void)oxidizerTank;
    // TODO: Implement
}

PLUGIN_EXPORT void Vessel_SetThrusterLevel(void* thruster, double level) {
    (void)thruster;
    (void)level;
    // TODO: Implement
}

PLUGIN_EXPORT void* Vessel_CreateThrusterGroup(void* vessel, int groupType, const char* name) {
    (void)vessel;
    (void)groupType;
    (void)name;
    // TODO: Implement when engine interface is available
    return nullptr;
}

PLUGIN_EXPORT void Vessel_AddThrusterToGroup(void* group, void* thruster, double weight) {
    (void)group;
    (void)thruster;
    (void)weight;
    // TODO: Implement
}

PLUGIN_EXPORT void Vessel_SetGroupLevel(void* group, double level) {
    (void)group;
    (void)level;
    // TODO: Implement
}

PLUGIN_EXPORT void* Vessel_LoadMesh(void* vessel, const char* filepath) {
    (void)vessel;
    (void)filepath;
    // TODO: Implement when mesh loading interface is available
    return nullptr;
}

PLUGIN_EXPORT void Vessel_SetMesh(void* vessel, void* mesh) {
    if (!vessel) return;
    
    LunaE3Data* probe = static_cast<LunaE3Data*>(vessel);
    probe->mesh = mesh;
}

} // extern "C"
