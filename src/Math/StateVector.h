#pragma once

#include <glm/glm.hpp>

namespace Kosmos {

/**
 * @brief Represents a state vector in 6D phase space (position + velocity)
 * Used for orbital mechanics and physics simulation
 */
class StateVector {
public:
    StateVector();
    StateVector(const glm::dvec3& position, const glm::dvec3& velocity);
    
    // Accessors
    const glm::dvec3& GetPosition() const { return m_position; }
    const glm::dvec3& GetVelocity() const { return m_velocity; }
    
    void SetPosition(const glm::dvec3& position) { m_position = position; }
    void SetVelocity(const glm::dvec3& velocity) { m_velocity = velocity; }
    
    // Derived quantities
    double GetSpeed() const;
    double GetKineticEnergy(double mass) const;
    glm::dvec3 GetMomentum(double mass) const;
    glm::dvec3 GetAngularMomentum(double mass) const;
    
    // Operators
    StateVector operator+(const StateVector& other) const;
    StateVector operator-(const StateVector& other) const;
    StateVector operator*(double scalar) const;
    StateVector& operator+=(const StateVector& other);
    StateVector& operator-=(const StateVector& other);
    StateVector& operator*=(double scalar);
    
private:
    glm::dvec3 m_position;  // meters
    glm::dvec3 m_velocity;  // meters/second
};

} // namespace Kosmos
