#pragma once

#include "CelestialBody.h"
#include <glm/glm.hpp>
#include <vector>

namespace Kosmos {

/**
 * @brief Calculates gravitational forces for N-body simulation
 */
class GravityEngine {
public:
    GravityEngine();
    ~GravityEngine() = default;
    
    // Calculate acceleration on a body due to all other bodies
    glm::dvec3 CalculateAcceleration(const CelestialBody* body, const std::vector<CelestialBody*>& allBodies) const;
    
    // Calculate acceleration at a specific point in space
    glm::dvec3 CalculateAccelerationAt(const glm::dvec3& position, const std::vector<CelestialBody*>& bodies) const;
    
    // Calculate gravitational parameter (G * M)
    static double GetGravitationalParameter(double mass);
    
    // Calculate two-body acceleration
    static glm::dvec3 CalculateTwoBodyAcceleration(const glm::dvec3& position, const glm::dvec3& bodyPosition, double bodyMass);
    
private:
    static constexpr double G = 6.67430e-11;  // Gravitational constant (m³/kg/s²)
};

} // namespace Kosmos
