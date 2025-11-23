#include "Vessel.h"

namespace Kosmos {

Vessel::Vessel()
    : m_name("Unnamed Vessel")
    , m_position(0.0, 0.0, 0.0)
    , m_velocity(0.0, 0.0, 0.0)
    , m_acceleration(0.0, 0.0, 0.0)
    , m_emptyMass(1000.0)
    , m_emptyCenterOfMass(0.0, 0.0, 0.0)
    , m_totalForce(0.0, 0.0, 0.0)
    , m_totalTorque(0.0, 0.0, 0.0)
    , m_atmosphericPressure(0.0)
    , m_atmosphericDensity(0.0)
    , m_altitude(0.0)
{
}

void Vessel::Update(double deltaTime) {
    PreStep(deltaTime);
    UpdateThrusters(deltaTime);
    CalculateThrusterForces();
    PostStep(deltaTime);
}

void Vessel::PreStep(double deltaTime) {
    (void)deltaTime;  // Unused in base class
    // Override in derived classes
}

void Vessel::PostStep(double deltaTime) {
    (void)deltaTime;  // Unused in base class
    // Override in derived classes
}

double Vessel::GetMass() const {
    return m_emptyMass + m_fuelTankManager.GetTotalFuelMass();
}

double Vessel::GetFuelMass() const {
    return m_fuelTankManager.GetTotalFuelMass();
}

glm::dvec3 Vessel::GetCenterOfMass() const {
    double totalMass = m_emptyMass;
    glm::dvec3 weightedPosition = m_emptyCenterOfMass * m_emptyMass;
    
    for (const auto& tank : m_fuelTankManager.GetTanks()) {
        double tankMass = tank->GetMass();
        totalMass += tankMass;
        weightedPosition += tank->GetCenterOfMass() * tankMass;
    }
    
    if (totalMass > 0.0) {
        return weightedPosition / totalMass;
    }
    
    return m_emptyCenterOfMass;
}

void Vessel::SetThrusterGroupLevel(ThrusterGroup::GroupType group, double level) {
    if (auto* g = m_thrusterGroupManager.GetGroup(group)) {
        g->SetLevel(level);
    }
}

double Vessel::GetThrusterGroupLevel(ThrusterGroup::GroupType group) const {
    if (auto* g = const_cast<ThrusterGroupManager&>(m_thrusterGroupManager).GetGroup(group)) {
        return g->GetLevel();
    }
    return 0.0;
}

void Vessel::SetAttitudeControl(const glm::dvec3& angularControl) {
    m_thrusterGroupManager.SetRotationControl(angularControl);
}

void Vessel::SetTranslationControl(const glm::dvec3& linearControl) {
    m_thrusterGroupManager.SetTranslationControl(linearControl);
}

void Vessel::AddForce(const glm::dvec3& force) {
    m_totalForce += force;
}

void Vessel::AddForceAt(const glm::dvec3& force, const glm::dvec3& point) {
    AddForce(force);
    
    // Calculate torque: τ = r × F
    glm::dvec3 centerOfMass = GetCenterOfMass();
    glm::dvec3 r = point - centerOfMass;
    glm::dvec3 torque = glm::cross(r, force);
    
    AddTorque(torque);
}

void Vessel::AddTorque(const glm::dvec3& torque) {
    m_totalTorque += torque;
}

void Vessel::ClearForces() {
    m_totalForce = glm::dvec3(0.0);
    m_totalTorque = glm::dvec3(0.0);
}

void Vessel::SetAtmosphericConditions(double pressure, double density, double altitude) {
    m_atmosphericPressure = pressure;
    m_atmosphericDensity = density;
    m_altitude = altitude;
}

void Vessel::UpdateThrusters(double deltaTime) {
    for (auto& thruster : m_thrusters) {
        thruster->Update(deltaTime, m_fuelTankManager, m_atmosphericPressure);
    }
}

void Vessel::CalculateThrusterForces() {
    ClearForces();
    
    for (auto& thruster : m_thrusters) {
        if (thruster->GetLevel() > 0.0) {
            glm::dvec3 thrustVector = thruster->GetThrustVector();
            glm::dvec3 thrustPosition = thruster->GetPosition();
            AddForceAt(thrustVector, thrustPosition);
        }
    }
}

void Vessel::UpdateMassProperties() {
    // This would recalculate inertia tensor
    // For now, just stub
}

} // namespace Kosmos
