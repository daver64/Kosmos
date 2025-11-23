#pragma once

#include "CelestialBody.h"
#include "GravityEngine.h"
#include "Integrator.h"
#include <vector>
#include <memory>
#include <string>

namespace Kosmos {

/**
 * @brief Manages a single star system with planets, moons, etc.
 */
class SolarSystem {
public:
    SolarSystem(const std::string& name);
    ~SolarSystem() = default;
    
    const std::string& GetName() const { return m_name; }
    
    // Body management
    void AddBody(std::unique_ptr<CelestialBody> body);
    CelestialBody* GetBody(const std::string& name);
    const std::vector<std::unique_ptr<CelestialBody>>& GetBodies() const { return m_bodies; }
    
    CelestialBody* GetCentralBody() { return m_centralBody; }
    void SetCentralBody(CelestialBody* body) { m_centralBody = body; }
    
    // Simulation
    void Update(double deltaTime);
    void SetTimeAcceleration(double factor) { m_timeAcceleration = factor; }
    double GetTimeAcceleration() const { return m_timeAcceleration; }
    
    double GetSimulationTime() const { return m_simulationTime; }
    
    // Queries
    glm::dvec3 GetGravityAt(const glm::dvec3& position) const;
    
    // Factory methods for common systems
    static std::unique_ptr<SolarSystem> CreateTestSystem();  // Sun + Earth
    
private:
    std::string m_name;
    std::vector<std::unique_ptr<CelestialBody>> m_bodies;
    CelestialBody* m_centralBody;  // Usually the star
    
    GravityEngine m_gravityEngine;
    Integrator m_integrator;
    
    double m_simulationTime;
    double m_timeAcceleration;
};

} // namespace Kosmos
