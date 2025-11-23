#include "GravityEngine.h"
#include <glm/gtx/norm.hpp>

namespace Kosmos {

GravityEngine::GravityEngine() {
}

glm::dvec3 GravityEngine::CalculateAcceleration(const CelestialBody* body, const std::vector<CelestialBody*>& allBodies) const {
    glm::dvec3 totalAcceleration(0.0);
    glm::dvec3 bodyPosition = body->GetPosition();
    
    for (const CelestialBody* other : allBodies) {
        if (other == body) continue;  // Skip self
        
        glm::dvec3 r = other->GetPosition() - bodyPosition;
        double distanceSq = glm::length2(r);
        
        if (distanceSq < 1e-10) continue;  // Avoid division by zero
        
        double distance = std::sqrt(distanceSq);
        double force = G * other->GetMass() / distanceSq;
        
        totalAcceleration += (r / distance) * force;
    }
    
    return totalAcceleration;
}

glm::dvec3 GravityEngine::CalculateAccelerationAt(const glm::dvec3& position, const std::vector<CelestialBody*>& bodies) const {
    glm::dvec3 totalAcceleration(0.0);
    
    for (const CelestialBody* body : bodies) {
        glm::dvec3 r = body->GetPosition() - position;
        double distanceSq = glm::length2(r);
        
        if (distanceSq < 1e-10) continue;  // Avoid division by zero
        
        double distance = std::sqrt(distanceSq);
        double force = G * body->GetMass() / distanceSq;
        
        totalAcceleration += (r / distance) * force;
    }
    
    return totalAcceleration;
}

double GravityEngine::GetGravitationalParameter(double mass) {
    return G * mass;
}

glm::dvec3 GravityEngine::CalculateTwoBodyAcceleration(const glm::dvec3& position, const glm::dvec3& bodyPosition, double bodyMass) {
    glm::dvec3 r = bodyPosition - position;
    double distanceSq = glm::length2(r);
    
    if (distanceSq < 1e-10) return glm::dvec3(0.0);  // Avoid division by zero
    
    double distance = std::sqrt(distanceSq);
    double force = G * bodyMass / distanceSq;
    
    return (r / distance) * force;
}

} // namespace Kosmos