#pragma once

#include "FuelTank.h"
#include <memory>

namespace Kosmos {

class Thruster {
public:
    enum class Type {
        ROCKET,
        MONOPROP,
        ION,
        NUCLEAR_THERMAL,
        AIRBREATHING_JET,
        SCRAMJET,
        RCS
    };
    
    struct ThrusterSpec {
        Type type;
        double maxThrust;           // Newtons
        double isp0;                // Vacuum Isp (seconds)
        double ispAtm;              // Atmospheric Isp (seconds)
        FuelTank::FuelType fuelType;
        FuelTank::FuelType oxidizerType;
        double fuelMassRatio;       // Fuel/(Fuel+Oxidizer)
        double throttleMin;         // 0.0-1.0
        double throttleMax;         // 0.0-1.0
        bool canGimbal;
        double gimbalRange;         // Radians
    };
    
    Thruster(const ThrusterSpec& spec, const glm::dvec3& position, const glm::dvec3& direction);
    ~Thruster() = default;
    
    // Control
    void SetLevel(double level);
    double GetLevel() const { return m_currentLevel; }
    
    void SetGimbal(double pitch, double yaw);
    void GetGimbal(double& pitch, double& yaw) const;
    
    // Physics
    double GetCurrentThrust() const;
    double GetCurrentIsp() const;
    double GetFuelFlowRate() const;
    double GetOxidizerFlowRate() const;
    
    double CalculateThrust(double atmosphericPressure, double altitude) const;
    
    glm::dvec3 GetThrustVector() const;
    glm::dvec3 GetPosition() const { return m_position; }
    glm::dvec3 GetDirection() const { return m_direction; }
    
    bool Update(double deltaTime, FuelTankManager& tankManager, double atmosphericPressure = 0.0);
    
    Type GetType() const { return m_spec.type; }
    double GetMaxThrust() const { return m_spec.maxThrust; }
    
    void AttachFuelTank(FuelTank* tank) { m_fuelTank = tank; }
    void AttachOxidizerTank(FuelTank* tank) { m_oxidizerTank = tank; }
    
private:
    ThrusterSpec m_spec;
    glm::dvec3 m_position;
    glm::dvec3 m_direction;
    
    double m_currentLevel;
    double m_gimbalPitch;
    double m_gimbalYaw;
    
    FuelTank* m_fuelTank;
    FuelTank* m_oxidizerTank;
    
    double CalculateIsp(double atmosphericPressure) const;
};

} // namespace Kosmos
