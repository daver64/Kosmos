#pragma once

#include <vector>
#include <memory>
#include <glm/glm.hpp>

namespace Kosmos {

class FuelTank {
public:
    enum class FuelType {
        LIQUID_HYDROGEN,
        LIQUID_OXYGEN,
        RP1,
        HYDRAZINE,
        XENON,
        NUCLEAR_THERMAL,
        ATMOSPHERE
    };
    
    FuelTank(FuelType type, double capacity, const glm::dvec3& position);
    ~FuelTank() = default;
    
    // Fuel management
    double GetCapacity() const { return m_capacity; }
    double GetMass() const { return m_currentMass; }
    double GetFuelMass() const { return m_currentMass - m_tankMass; }
    double GetLevel() const;
    
    bool ConsumeFuel(double amount);
    void Refuel(double amount);
    void SetLevel(double fraction);
    
    FuelType GetFuelType() const { return m_fuelType; }
    glm::dvec3 GetPosition() const { return m_position; }
    
    // Physical properties
    double GetDensity() const;
    double GetVolume() const;
    glm::dvec3 GetCenterOfMass() const { return m_position; }
    
private:
    FuelType m_fuelType;
    double m_capacity;        // kg
    double m_currentMass;     // kg (includes tank mass)
    double m_tankMass;        // kg (empty tank)
    glm::dvec3 m_position;    // Position in vessel coordinates
    
    static double GetFuelDensity(FuelType type);
};

class FuelTankManager {
public:
    FuelTankManager() = default;
    ~FuelTankManager() = default;
    
    void AddTank(std::shared_ptr<FuelTank> tank);
    void RemoveTank(size_t index);
    
    std::vector<FuelTank*> GetTanksByType(FuelTank::FuelType type);
    bool ConsumeFuel(FuelTank::FuelType type, double amount);
    
    double GetTotalFuel(FuelTank::FuelType type) const;
    double GetTotalFuelMass() const;
    
    const std::vector<std::shared_ptr<FuelTank>>& GetTanks() const {
        return m_tanks;
    }
    
private:
    std::vector<std::shared_ptr<FuelTank>> m_tanks;
};

} // namespace Kosmos
