#pragma once

#include "../Math/StateVector.h"
#include <glm/glm.hpp>
#include <string>

namespace Kosmos {

/**
 * @brief Represents a celestial body (star, planet, moon, etc.)
 */
class CelestialBody {
public:
    enum class Type {
        Star,
        Planet,
        Moon,
        Asteroid,
        Comet
    };
    
    CelestialBody(const std::string& name, Type type);
    ~CelestialBody() = default;
    
    // Basic properties
    const std::string& GetName() const { return m_name; }
    Type GetType() const { return m_type; }
    
    void SetMass(double mass) { m_mass = mass; }
    double GetMass() const { return m_mass; }
    
    void SetRadius(double radius) { m_radius = radius; }
    double GetRadius() const { return m_radius; }
    
    void SetColor(const glm::vec3& color) { m_color = color; }
    const glm::vec3& GetColor() const { return m_color; }
    
    // State
    void SetState(const StateVector& state) { m_state = state; }
    const StateVector& GetState() const { return m_state; }
    StateVector& GetState() { return m_state; }
    
    const glm::dvec3& GetPosition() const { return m_state.GetPosition(); }
    const glm::dvec3& GetVelocity() const { return m_state.GetVelocity(); }
    
    void SetPosition(const glm::dvec3& position);
    void SetVelocity(const glm::dvec3& velocity);
    
    // Rotation
    void SetRotationPeriod(double period) { m_rotationPeriod = period; }
    double GetRotationPeriod() const { return m_rotationPeriod; }
    
    void SetAxialTilt(double tilt) { m_axialTilt = tilt; }
    double GetAxialTilt() const { return m_axialTilt; }
    
    // Physics
    double GetGravitationalParameter() const { return m_mass * 6.67430e-11; } // G*M
    double GetSurfaceGravity() const;
    double GetEscapeVelocity() const;
    
    // Hierarchy
    void SetParent(CelestialBody* parent) { m_parent = parent; }
    CelestialBody* GetParent() const { return m_parent; }
    
    // Rendering
    void SetTexture(const std::string& texturePath) { m_texturePath = texturePath; }
    const std::string& GetTexture() const { return m_texturePath; }
    
private:
    std::string m_name;
    Type m_type;
    
    double m_mass;              // kg
    double m_radius;            // meters
    glm::vec3 m_color;         // RGB color for rendering
    
    StateVector m_state;        // Position and velocity
    
    double m_rotationPeriod;    // seconds
    double m_axialTilt;         // radians
    
    CelestialBody* m_parent;    // Parent body (for moons, etc.)
    
    std::string m_texturePath;  // Path to Celestia-format texture
};

} // namespace Kosmos
