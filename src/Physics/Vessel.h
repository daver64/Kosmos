#pragma once

#include "FuelTank.h"
#include "Thruster.h"
#include "ThrusterGroup.h"
#include <memory>
#include <vector>

namespace Kosmos {

class Vessel {
public:
    Vessel();
    virtual ~Vessel() = default;
    
    // Physics update
    virtual void Update(double deltaTime);
    virtual void PreStep(double deltaTime);
    virtual void PostStep(double deltaTime);
    
    // Position and orientation
    glm::dvec3 GetPosition() const { return m_position; }
    glm::dvec3 GetVelocity() const { return m_velocity; }
    
    // Mass properties
    double GetMass() const;
    double GetEmptyMass() const { return m_emptyMass; }
    double GetFuelMass() const;
    glm::dvec3 GetCenterOfMass() const;
    
    // Propulsion system access
    FuelTankManager& GetFuelTankManager() { return m_fuelTankManager; }
    ThrusterGroupManager& GetThrusterGroupManager() { return m_thrusterGroupManager; }
    std::vector<std::unique_ptr<Thruster>>& GetThrusters() { return m_thrusters; }
    
    // High-level thruster control
    void SetThrusterGroupLevel(ThrusterGroup::GroupType group, double level);
    double GetThrusterGroupLevel(ThrusterGroup::GroupType group) const;
    
    // Attitude control
    void SetAttitudeControl(const glm::dvec3& angularControl);
    void SetTranslationControl(const glm::dvec3& linearControl);
    
    // Force application
    void AddForce(const glm::dvec3& force);
    void AddForceAt(const glm::dvec3& force, const glm::dvec3& point);
    void AddTorque(const glm::dvec3& torque);
    void ClearForces();
    
    // Atmospheric conditions
    void SetAtmosphericConditions(double pressure, double density, double altitude);
    
    // Configuration
    void SetName(const std::string& name) { m_name = name; }
    const std::string& GetName() const { return m_name; }
    
protected:
    std::string m_name;
    
    // Physics state
    glm::dvec3 m_position;
    glm::dvec3 m_velocity;
    glm::dvec3 m_acceleration;
    
    // Mass properties
    double m_emptyMass;
    glm::dvec3 m_emptyCenterOfMass;
    
    // Propulsion systems
    FuelTankManager m_fuelTankManager;
    ThrusterGroupManager m_thrusterGroupManager;
    std::vector<std::unique_ptr<Thruster>> m_thrusters;
    
    // Forces
    glm::dvec3 m_totalForce;
    glm::dvec3 m_totalTorque;
    
    // Atmospheric conditions
    double m_atmosphericPressure;
    double m_atmosphericDensity;
    double m_altitude;
    
    // Update methods
    void UpdateThrusters(double deltaTime);
    void CalculateThrusterForces();
    void UpdateMassProperties();
};

} // namespace Kosmos
