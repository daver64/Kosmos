#include "Thruster.h"
#include <algorithm>
#include <cmath>

namespace Kosmos {

Thruster::Thruster(const ThrusterSpec& spec, const glm::dvec3& position, const glm::dvec3& direction)
    : m_spec(spec)
    , m_position(position)
    , m_direction(glm::normalize(direction))
    , m_currentLevel(0.0)
    , m_gimbalPitch(0.0)
    , m_gimbalYaw(0.0)
    , m_fuelTank(nullptr)
    , m_oxidizerTank(nullptr)
{
}

void Thruster::SetLevel(double level) {
    m_currentLevel = std::clamp(level, 0.0, 1.0);
}

void Thruster::SetGimbal(double pitch, double yaw) {
    if (m_spec.canGimbal) {
        m_gimbalPitch = std::clamp(pitch, -m_spec.gimbalRange, m_spec.gimbalRange);
        m_gimbalYaw = std::clamp(yaw, -m_spec.gimbalRange, m_spec.gimbalRange);
    }
}

void Thruster::GetGimbal(double& pitch, double& yaw) const {
    pitch = m_gimbalPitch;
    yaw = m_gimbalYaw;
}

double Thruster::GetCurrentThrust() const {
    // Simplified - would call CalculateThrust with actual atmospheric conditions
    return m_spec.maxThrust * m_currentLevel;
}

double Thruster::GetCurrentIsp() const {
    // Simplified - would call CalculateIsp with actual atmospheric conditions
    return m_spec.isp0;
}

double Thruster::CalculateThrust(double atmosphericPressure, double altitude) const {
    (void)altitude;  // TODO: Use for altitude-dependent effects
    if (m_currentLevel <= 0.0) return 0.0;
    
    double throttle = std::clamp(m_currentLevel, m_spec.throttleMin, m_spec.throttleMax);
    
    switch (m_spec.type) {
        case Type::ROCKET:
        case Type::MONOPROP:
        case Type::NUCLEAR_THERMAL:
        case Type::RCS: {
            // Calculate ISP for atmospheric conditions
            // double isp = CalculateIsp(atmosphericPressure);  // TODO: Use in efficiency calculations
            double vacuumThrust = m_spec.maxThrust * throttle;
            
            // Atmospheric back-pressure effect
            double pressureRatio = atmosphericPressure / 101325.0;
            double thrustLoss = pressureRatio * (m_spec.isp0 - m_spec.ispAtm) / m_spec.isp0;
            
            return vacuumThrust * (1.0 - thrustLoss * 0.5);
        }
        
        case Type::ION: {
            return m_spec.maxThrust * throttle;
        }
        
        case Type::AIRBREATHING_JET:
        case Type::SCRAMJET: {
            // Simplified air-breathing model
            if (atmosphericPressure < 1000.0) return 0.0;  // Too high
            return m_spec.maxThrust * throttle * (atmosphericPressure / 101325.0);
        }
    }
    
    return 0.0;
}

double Thruster::CalculateIsp(double atmosphericPressure) const {
    if (atmosphericPressure <= 0.0) {
        return m_spec.isp0;
    }
    
    double pressureRatio = std::clamp(atmosphericPressure / 101325.0, 0.0, 1.0);
    return m_spec.isp0 * (1.0 - pressureRatio) + m_spec.ispAtm * pressureRatio;
}

double Thruster::GetFuelFlowRate() const {
    double thrust = GetCurrentThrust();
    double isp = GetCurrentIsp();
    
    if (isp <= 0.0) return 0.0;
    
    constexpr double g0 = 9.80665;  // Standard gravity
    double totalFlowRate = thrust / (isp * g0);
    
    return totalFlowRate * m_spec.fuelMassRatio;
}

double Thruster::GetOxidizerFlowRate() const {
    if (m_spec.oxidizerType == FuelTank::FuelType::ATMOSPHERE) {
        return 0.0;
    }
    
    double thrust = GetCurrentThrust();
    double isp = GetCurrentIsp();
    
    if (isp <= 0.0) return 0.0;
    
    constexpr double g0 = 9.80665;
    double totalFlowRate = thrust / (isp * g0);
    
    return totalFlowRate * (1.0 - m_spec.fuelMassRatio);
}

bool Thruster::Update(double deltaTime, FuelTankManager& tankManager, double atmosphericPressure) {
    (void)atmosphericPressure;  // TODO: Use for thrust calculations
    if (m_currentLevel <= 0.0) return true;
    
    double fuelNeeded = GetFuelFlowRate() * deltaTime;
    double oxidizerNeeded = GetOxidizerFlowRate() * deltaTime;
    
    bool hasFuel = tankManager.ConsumeFuel(m_spec.fuelType, fuelNeeded);
    
    bool hasOxidizer = true;
    if (m_spec.oxidizerType != FuelTank::FuelType::ATMOSPHERE) {
        hasOxidizer = tankManager.ConsumeFuel(m_spec.oxidizerType, oxidizerNeeded);
    }
    
    if (!hasFuel || !hasOxidizer) {
        m_currentLevel = 0.0;
        return false;
    }
    
    return true;
}

glm::dvec3 Thruster::GetThrustVector() const {
    double thrust = GetCurrentThrust();
    
    // TODO: Apply gimbal rotation to direction
    glm::dvec3 direction = m_direction;
    
    return direction * thrust;
}

} // namespace Kosmos
