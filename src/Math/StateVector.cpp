#include "StateVector.h"
#include <glm/gtx/norm.hpp>

namespace Kosmos {

StateVector::StateVector()
    : m_position(0.0)
    , m_velocity(0.0)
{
}

StateVector::StateVector(const glm::dvec3& position, const glm::dvec3& velocity)
    : m_position(position)
    , m_velocity(velocity)
{
}

double StateVector::GetSpeed() const {
    return glm::length(m_velocity);
}

double StateVector::GetKineticEnergy(double mass) const {
    double speedSq = glm::length2(m_velocity);
    return 0.5 * mass * speedSq;
}

glm::dvec3 StateVector::GetMomentum(double mass) const {
    return m_velocity * mass;
}

glm::dvec3 StateVector::GetAngularMomentum(double mass) const {
    return glm::cross(m_position, m_velocity * mass);
}

StateVector StateVector::operator+(const StateVector& other) const {
    return StateVector(
        m_position + other.m_position,
        m_velocity + other.m_velocity
    );
}

StateVector StateVector::operator-(const StateVector& other) const {
    return StateVector(
        m_position - other.m_position,
        m_velocity - other.m_velocity
    );
}

StateVector StateVector::operator*(double scalar) const {
    return StateVector(
        m_position * scalar,
        m_velocity * scalar
    );
}

StateVector& StateVector::operator+=(const StateVector& other) {
    m_position += other.m_position;
    m_velocity += other.m_velocity;
    return *this;
}

StateVector& StateVector::operator-=(const StateVector& other) {
    m_position -= other.m_position;
    m_velocity -= other.m_velocity;
    return *this;
}

StateVector& StateVector::operator*=(double scalar) {
    m_position *= scalar;
    m_velocity *= scalar;
    return *this;
}

} // namespace Kosmos