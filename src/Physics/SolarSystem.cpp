#include "SolarSystem.h"
#include "../Core/Logger.h"
#include "../Math/OrbitalElements.h"

namespace Kosmos {

SolarSystem::SolarSystem(const std::string& name)
    : m_name(name)
    , m_centralBody(nullptr)
    , m_integrator(Integrator::Method::RK4)
    , m_simulationTime(0.0)
    , m_timeAcceleration(1.0)
{
}

void SolarSystem::AddBody(std::unique_ptr<CelestialBody> body) {
    if (m_bodies.empty() && body->GetType() == CelestialBody::Type::Star) {
        m_centralBody = body.get();
    }
    m_bodies.push_back(std::move(body));
}

CelestialBody* SolarSystem::GetBody(const std::string& name) {
    for (auto& body : m_bodies) {
        if (body->GetName() == name) {
            return body.get();
        }
    }
    return nullptr;
}

void SolarSystem::Update(double deltaTime) {
    double dt = deltaTime * m_timeAcceleration;
    m_simulationTime += dt;
    
    // Collect raw pointers for gravity calculations
    std::vector<CelestialBody*> bodyPtrs;
    bodyPtrs.reserve(m_bodies.size());
    for (auto& body : m_bodies) {
        bodyPtrs.push_back(body.get());
    }
    
    // Update each body's state using numerical integration
    for (auto& body : m_bodies) {
        // Skip the central star (assumed stationary for now)
        if (body.get() == m_centralBody) continue;
        
        // Create derivative function for this body
        auto derivative = [this, &bodyPtrs, bodyPtr = body.get()](double t, const StateVector& state) -> StateVector {
            glm::dvec3 acceleration = m_gravityEngine.CalculateAcceleration(bodyPtr, bodyPtrs);
            return StateVector(state.GetVelocity(), acceleration);
        };
        
        // Integrate one step
        StateVector newState = m_integrator.Step(
            m_simulationTime,
            body->GetState(),
            dt,
            derivative
        );
        
        body->SetState(newState);
    }
}

glm::dvec3 SolarSystem::GetGravityAt(const glm::dvec3& position) const {
    std::vector<CelestialBody*> bodyPtrs;
    bodyPtrs.reserve(m_bodies.size());
    for (const auto& body : m_bodies) {
        bodyPtrs.push_back(body.get());
    }
    
    return m_gravityEngine.CalculateAccelerationAt(position, bodyPtrs);
}

std::unique_ptr<SolarSystem> SolarSystem::CreateTestSystem() {
    auto system = std::make_unique<SolarSystem>("Sol");
    
    // Create the Sun
    auto sun = std::make_unique<CelestialBody>("Sun", CelestialBody::Type::Star);
    sun->SetMass(1.989e30);  // kg
    sun->SetRadius(6.96e8);  // meters
    sun->SetColor(glm::vec3(1.0f, 0.9f, 0.6f));
    sun->SetPosition(glm::dvec3(0.0, 0.0, 0.0));
    sun->SetVelocity(glm::dvec3(0.0, 0.0, 0.0));
    
    // Create Earth
    auto earth = std::make_unique<CelestialBody>("Earth", CelestialBody::Type::Planet);
    earth->SetMass(5.972e24);  // kg
    earth->SetRadius(6.371e6);  // meters
    earth->SetColor(glm::vec3(0.2f, 0.4f, 0.8f));
    earth->SetRotationPeriod(86400.0);  // 24 hours
    earth->SetAxialTilt(0.4091);  // 23.44 degrees
    earth->SetParent(sun.get());
    
    // Set Earth's orbital state (circular orbit at 1 AU)
    const double AU = 1.496e11;  // meters
    const double mu = sun->GetGravitationalParameter();
    
    OrbitalElements earthOrbit = OrbitalElements::CircularOrbit(AU, 0.0);
    StateVector earthState = earthOrbit.ToStateVector(mu);
    earth->SetState(earthState);
    
    LOG_INFO("Created test solar system: Sun and Earth");
    LOG_INFO("Earth orbital velocity: " + std::to_string(earthState.GetVelocity().x / 1000.0) + " km/s");
    
    system->AddBody(std::move(sun));
    system->AddBody(std::move(earth));
    
    return system;
}

} // namespace Kosmos