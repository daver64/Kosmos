#include "FuelTank.h"

namespace Kosmos {

FuelTank::FuelTank(FuelType type, double capacity, const glm::dvec3& position)
    : m_fuelType(type)
    , m_capacity(capacity)
    , m_currentMass(capacity * 1.1)  // Assume tank is 10% of fuel mass
    , m_tankMass(capacity * 0.1)
    , m_position(position)
{
}

double FuelTank::GetLevel() const {
    double fuelMass = GetFuelMass();
    return (fuelMass > 0.0) ? (fuelMass / m_capacity) : 0.0;
}

bool FuelTank::ConsumeFuel(double amount) {
    double fuelMass = GetFuelMass();
    if (fuelMass < amount) {
        return false;
    }
    
    m_currentMass -= amount;
    return true;
}

void FuelTank::Refuel(double amount) {
    double fuelMass = GetFuelMass();
    double newFuelMass = fuelMass + amount;
    if (newFuelMass > m_capacity) {
        newFuelMass = m_capacity;
    }
    m_currentMass = newFuelMass + m_tankMass;
}

void FuelTank::SetLevel(double fraction) {
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    m_currentMass = m_capacity * fraction + m_tankMass;
}

double FuelTank::GetDensity() const {
    return GetFuelDensity(m_fuelType);
}

double FuelTank::GetVolume() const {
    return m_capacity / GetDensity();
}

double FuelTank::GetFuelDensity(FuelType type) {
    switch (type) {
        case FuelType::LIQUID_HYDROGEN:  return 70.8;      // kg/m³
        case FuelType::LIQUID_OXYGEN:    return 1141.0;    // kg/m³
        case FuelType::RP1:              return 820.0;     // kg/m³
        case FuelType::HYDRAZINE:        return 1004.0;    // kg/m³
        case FuelType::XENON:            return 2942.0;    // kg/m³ (liquid)
        case FuelType::NUCLEAR_THERMAL:  return 70.8;      // Use LH2 density
        case FuelType::ATMOSPHERE:       return 1.225;     // kg/m³ at sea level
        default:                         return 1000.0;
    }
}

// FuelTankManager implementation

void FuelTankManager::AddTank(std::shared_ptr<FuelTank> tank) {
    m_tanks.push_back(tank);
}

void FuelTankManager::RemoveTank(size_t index) {
    if (index < m_tanks.size()) {
        m_tanks.erase(m_tanks.begin() + index);
    }
}

std::vector<FuelTank*> FuelTankManager::GetTanksByType(FuelTank::FuelType type) {
    std::vector<FuelTank*> result;
    for (auto& tank : m_tanks) {
        if (tank->GetFuelType() == type) {
            result.push_back(tank.get());
        }
    }
    return result;
}

bool FuelTankManager::ConsumeFuel(FuelTank::FuelType type, double amount) {
    auto tanks = GetTanksByType(type);
    
    // Try to consume from tanks in order
    double remaining = amount;
    for (auto* tank : tanks) {
        double available = tank->GetFuelMass();
        if (available >= remaining) {
            return tank->ConsumeFuel(remaining);
        } else if (available > 0.0) {
            tank->ConsumeFuel(available);
            remaining -= available;
        }
    }
    
    return remaining <= 0.0;
}

double FuelTankManager::GetTotalFuel(FuelTank::FuelType type) const {
    double total = 0.0;
    for (const auto& tank : m_tanks) {
        if (tank->GetFuelType() == type) {
            total += tank->GetFuelMass();
        }
    }
    return total;
}

double FuelTankManager::GetTotalFuelMass() const {
    double total = 0.0;
    for (const auto& tank : m_tanks) {
        total += tank->GetFuelMass();
    }
    return total;
}

} // namespace Kosmos
