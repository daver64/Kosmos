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
    , m_epoch(VSOP87::J2000_JD)
    , m_useVSOP87(false)
    , m_moonOrbitalPhase(0.0)
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

void SolarSystem::EnableVSOP87(const std::string& dataPath, char variant) {
    m_vsop87 = std::make_unique<VSOP87>();
    if (m_vsop87->LoadData(dataPath, variant)) {
        m_useVSOP87 = true;
        LOG_INFO("VSOP87 enabled for solar system: " + m_name);
    } else {
        m_vsop87.reset();
        m_useVSOP87 = false;
        LOG_ERROR("Failed to enable VSOP87 for solar system: " + m_name);
    }
}

double SolarSystem::GetJulianDate() const {
    // Convert simulation time (seconds since epoch) to Julian Date
    return m_epoch + (m_simulationTime / 86400.0);
}

void SolarSystem::SetJulianDate(double jd) {
    m_epoch = jd;
    m_simulationTime = 0.0;
    if (m_useVSOP87) {
        UpdateVSOP87Positions();
    }
}

void SolarSystem::UpdateVSOP87Positions() {
    if (!m_useVSOP87 || !m_vsop87) return;
    
    double jd = GetJulianDate();
    
    // Debug log
    static bool logged = false;
    if (!logged) {
        LOG_INFO("UpdateVSOP87Positions called, JD=" + std::to_string(jd));
        logged = true;
    }
    
    // Map planet names to VSOP87 planets
    const std::map<std::string, VSOP87::Planet> planetMap = {
        {"Mercury", VSOP87::Planet::Mercury},
        {"Venus", VSOP87::Planet::Venus},
        {"Earth", VSOP87::Planet::Earth},
        {"Mars", VSOP87::Planet::Mars},
        {"Jupiter", VSOP87::Planet::Jupiter},
        {"Saturn", VSOP87::Planet::Saturn},
        {"Uranus", VSOP87::Planet::Uranus},
        {"Neptune", VSOP87::Planet::Neptune}
    };
    
    for (auto& body : m_bodies) {
        // Skip the central star
        if (body.get() == m_centralBody) continue;
        
        auto it = planetMap.find(body->GetName());
        if (it != planetMap.end() && m_vsop87->HasData(it->second)) {
            glm::dvec3 pos, vel;
            m_vsop87->GetPositionVelocity(it->second, jd, pos, vel);
            
            // Debug log first update
            if (!logged) {
                LOG_INFO(body->GetName() + " VSOP87 pos (AU): " + 
                         std::to_string(pos.x/1.496e11) + ", " +
                         std::to_string(pos.y/1.496e11) + ", " +
                         std::to_string(pos.z/1.496e11));
            }
            
            // VSOP87 gives equatorial coordinates (for variant D)
            body->SetPosition(pos);
            body->SetVelocity(vel);
        }
    }
}

void SolarSystem::Update(double deltaTime) {
    double dt = deltaTime * m_timeAcceleration;
    m_simulationTime += dt;
    
    if (m_useVSOP87) {
        // Use VSOP87 for planetary positions
        UpdateVSOP87Positions();
        
        // Update Moon's position relative to Earth with smooth orbital motion
        CelestialBody* earth = GetBody("Earth");
        CelestialBody* moon = GetBody("Moon");
        if (earth && moon) {
            // Smooth circular orbit using incremental phase
            double moonPeriod = 27.32 * 86400.0;  // 27.32 days
            double moonRadius = 384400e3;  // 384,400 km
            
            // Increment phase smoothly
            double angularVelocity = 2.0 * M_PI / moonPeriod;
            m_moonOrbitalPhase += angularVelocity * dt;
            
            // Keep phase in [0, 2π] range
            while (m_moonOrbitalPhase > 2.0 * M_PI) {
                m_moonOrbitalPhase -= 2.0 * M_PI;
            }
            
            glm::dvec3 moonOffset;
            moonOffset.x = moonRadius * cos(m_moonOrbitalPhase);
            moonOffset.y = 0.0;
            moonOffset.z = moonRadius * sin(m_moonOrbitalPhase);
            
            moon->SetPosition(earth->GetPosition() + moonOffset);
            
            // Calculate orbital velocity (perpendicular to radius)
            double speed = 2.0 * M_PI * moonRadius / moonPeriod;
            glm::dvec3 moonVel;
            moonVel.x = -speed * sin(m_moonOrbitalPhase);
            moonVel.y = 0.0;
            moonVel.z = speed * cos(m_moonOrbitalPhase);
            moon->SetVelocity(earth->GetVelocity() + moonVel);
        }
    } else {
        // Use numerical integration
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
            auto derivative = [this, &bodyPtrs, bodyPtr = body.get()](double /*t*/, const StateVector& state) -> StateVector {
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

std::unique_ptr<SolarSystem> SolarSystem::CreateSolSystem(bool /*useVSOP87*/) {
    auto system = std::make_unique<SolarSystem>("Sol System");
    
    // Sun (central body)
    auto sun = std::make_unique<CelestialBody>("Sol", CelestialBody::Type::Star);
    sun->SetMass(1.98892e30);  // kg
    sun->SetRadius(696000e3);  // meters
    sun->SetColor(glm::vec3(1.0f, 1.0f, 0.8f));
    sun->SetPosition(glm::dvec3(0.0));
    sun->SetVelocity(glm::dvec3(0.0));
    system->m_centralBody = sun.get();
    system->AddBody(std::move(sun));
    
    // Mercury
    auto mercury = std::make_unique<CelestialBody>("Mercury", CelestialBody::Type::Planet);
    mercury->SetMass(3.3011e23);
    mercury->SetRadius(2439.7e3);
    mercury->SetColor(glm::vec3(0.7f, 0.7f, 0.7f));
    mercury->SetParent(system->m_centralBody);
    system->AddBody(std::move(mercury));
    
    // Venus
    auto venus = std::make_unique<CelestialBody>("Venus", CelestialBody::Type::Planet);
    venus->SetMass(4.8675e24);
    venus->SetRadius(6051.8e3);
    venus->SetColor(glm::vec3(0.9f, 0.8f, 0.6f));
    venus->SetParent(system->m_centralBody);
    system->AddBody(std::move(venus));
    
    // Earth
    auto earth = std::make_unique<CelestialBody>("Earth", CelestialBody::Type::Planet);
    earth->SetMass(5.97237e24);
    earth->SetRadius(6371.0e3);
    earth->SetColor(glm::vec3(0.3f, 0.5f, 0.9f));
    earth->SetRotationPeriod(86400.0);
    earth->SetAxialTilt(0.4091);  // 23.44 degrees in radians
    earth->SetParent(system->m_centralBody);
    CelestialBody* earthPtr = earth.get();  // Save pointer for Moon's parent
    system->AddBody(std::move(earth));
    
    // Moon (Earth's satellite)
    auto moon = std::make_unique<CelestialBody>("Moon", CelestialBody::Type::Moon);
    moon->SetMass(7.342e22);
    moon->SetRadius(1737.4e3);
    moon->SetColor(glm::vec3(0.7f, 0.7f, 0.7f));
    moon->SetParent(earthPtr);  // Moon orbits Earth
    // Set Moon's orbital parameters (circular orbit approximation)
    double moonSemiMajorAxis = 384400e3;  // 384,400 km
    double moonOrbitalPeriod = 27.32 * 86400.0;  // 27.32 days
    double moonOrbitalSpeed = 2.0 * M_PI * moonSemiMajorAxis / moonOrbitalPeriod;
    // Position Moon in orbit around Earth (will be relative to Earth)
    moon->SetPosition(glm::dvec3(moonSemiMajorAxis, 0.0, 0.0));
    moon->SetVelocity(glm::dvec3(0.0, moonOrbitalSpeed, 0.0));
    system->AddBody(std::move(moon));
    
    // Mars
    auto mars = std::make_unique<CelestialBody>("Mars", CelestialBody::Type::Planet);
    mars->SetMass(6.4171e23);
    mars->SetRadius(3389.5e3);
    mars->SetColor(glm::vec3(0.9f, 0.5f, 0.3f));
    mars->SetParent(system->m_centralBody);
    system->AddBody(std::move(mars));
    
    // Jupiter
    auto jupiter = std::make_unique<CelestialBody>("Jupiter", CelestialBody::Type::Planet);
    jupiter->SetMass(1.8982e27);
    jupiter->SetRadius(69911e3);
    jupiter->SetColor(glm::vec3(0.8f, 0.7f, 0.6f));
    jupiter->SetParent(system->m_centralBody);
    system->AddBody(std::move(jupiter));
    
    // Saturn
    auto saturn = std::make_unique<CelestialBody>("Saturn", CelestialBody::Type::Planet);
    saturn->SetMass(5.6834e26);
    saturn->SetRadius(58232e3);
    saturn->SetColor(glm::vec3(0.9f, 0.8f, 0.6f));
    saturn->SetParent(system->m_centralBody);
    system->AddBody(std::move(saturn));
    
    // Uranus
    auto uranus = std::make_unique<CelestialBody>("Uranus", CelestialBody::Type::Planet);
    uranus->SetMass(8.6810e25);
    uranus->SetRadius(25362e3);
    uranus->SetColor(glm::vec3(0.5f, 0.8f, 0.9f));
    uranus->SetParent(system->m_centralBody);
    system->AddBody(std::move(uranus));
    
    // Neptune
    auto neptune = std::make_unique<CelestialBody>("Neptune", CelestialBody::Type::Planet);
    neptune->SetMass(1.02413e26);
    neptune->SetRadius(24622e3);
    neptune->SetColor(glm::vec3(0.3f, 0.4f, 0.9f));
    neptune->SetParent(system->m_centralBody);
    system->AddBody(std::move(neptune));
    
    LOG_INFO("Created Sol system with 8 planets (use EnableVSOP87 for positions)");
    return system;
}

} // namespace Kosmos