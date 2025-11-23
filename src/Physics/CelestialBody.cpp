#include "CelestialBody.h"
#include <cmath>

namespace Kosmos {

CelestialBody::CelestialBody(const std::string& name, Type type)
    : m_name(name)
    , m_type(type)
    , m_mass(0.0)
    , m_radius(0.0)
    , m_color(1.0f, 1.0f, 1.0f)
    , m_rotationPeriod(86400.0)  // 24 hours default
    , m_axialTilt(0.0)
    , m_parent(nullptr)
{
}

void CelestialBody::SetPosition(const glm::dvec3& position) {
    m_state.SetPosition(position);
}

void CelestialBody::SetVelocity(const glm::dvec3& velocity) {
    m_state.SetVelocity(velocity);
}

double CelestialBody::GetSurfaceGravity() const {
    if (m_radius == 0.0) return 0.0;
    const double G = 6.67430e-11;
    return G * m_mass / (m_radius * m_radius);
}

double CelestialBody::GetEscapeVelocity() const {
    if (m_radius == 0.0) return 0.0;
    const double G = 6.67430e-11;
    return std::sqrt(2.0 * G * m_mass / m_radius);
}

} // namespace Kosmos