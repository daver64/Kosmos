#pragma once

#include "Core.h"

extern "C" {
    // Plugin lifecycle functions
    PLUGIN_EXPORT const char* GetPluginName();
    PLUGIN_EXPORT const char* GetPluginVersion();
    PLUGIN_EXPORT int GetAPIVersion();
    
    // Vessel creation/destruction
    PLUGIN_EXPORT void* CreateVessel(void* engineInterface);
    PLUGIN_EXPORT void DestroyVessel(void* vessel);
    
    // Vessel interface functions
    PLUGIN_EXPORT void Vessel_Update(void* vessel, double deltaTime);
    PLUGIN_EXPORT void Vessel_PreStep(void* vessel, double deltaTime);
    PLUGIN_EXPORT void Vessel_PostStep(void* vessel, double deltaTime);
    
    // Vessel control
    PLUGIN_EXPORT void Vessel_SetThrusterGroupLevel(void* vessel, int groupType, double level);
    PLUGIN_EXPORT double Vessel_GetThrusterGroupLevel(void* vessel, int groupType);
    
    // Fuel management
    PLUGIN_EXPORT void* Vessel_CreateFuelTank(void* vessel, int fuelType, double capacity, const double* position);
    PLUGIN_EXPORT void Vessel_SetFuelLevel(void* tank, double fraction);
    PLUGIN_EXPORT double Vessel_GetFuelLevel(void* tank);
    
    // Thruster management
    struct ThrusterSpecC {
        int type;
        double maxThrust;
        double isp0;
        double ispAtm;
        int fuelType;
        int oxidizerType;
        double fuelMassRatio;
        double throttleMin;
        double throttleMax;
        bool canGimbal;
        double gimbalRange;
    };
    
    PLUGIN_EXPORT void* Vessel_CreateThruster(void* vessel, const ThrusterSpecC* spec,
                                              const double* position, const double* direction);
    PLUGIN_EXPORT void Vessel_AttachThrusterToTank(void* thruster, void* fuelTank, void* oxidizerTank);
    PLUGIN_EXPORT void Vessel_SetThrusterLevel(void* thruster, double level);
    
    // Thruster group management
    PLUGIN_EXPORT void* Vessel_CreateThrusterGroup(void* vessel, int groupType, const char* name);
    PLUGIN_EXPORT void Vessel_AddThrusterToGroup(void* group, void* thruster, double weight);
    PLUGIN_EXPORT void Vessel_SetGroupLevel(void* group, double level);
    
    // Mesh loading
    PLUGIN_EXPORT const char* Vessel_GetMeshPath(void* vessel);
    PLUGIN_EXPORT void* Vessel_LoadMesh(void* vessel, const char* filepath);
    PLUGIN_EXPORT void Vessel_SetMesh(void* vessel, void* mesh);
}
