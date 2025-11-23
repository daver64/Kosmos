#include "OrbitalElements.h"
#include <cmath>
#include <glm/gtx/norm.hpp>

namespace Kosmos {

double OrbitalElements::GetSemiMinorAxis() const {
    return semiMajorAxis * std::sqrt(1.0 - eccentricity * eccentricity);
}

double OrbitalElements::GetPeriapsis() const {
    return semiMajorAxis * (1.0 - eccentricity);
}

double OrbitalElements::GetApoapsis() const {
    return semiMajorAxis * (1.0 + eccentricity);
}

double OrbitalElements::GetPeriod(double gravitationalParameter) const {
    return 2.0 * M_PI * std::sqrt((semiMajorAxis * semiMajorAxis * semiMajorAxis) / gravitationalParameter);
}

double OrbitalElements::GetMeanMotion(double gravitationalParameter) const {
    return std::sqrt(gravitationalParameter / (semiMajorAxis * semiMajorAxis * semiMajorAxis));
}

StateVector OrbitalElements::ToStateVector(double gravitationalParameter) const {
    // Convert orbital elements to state vector (position and velocity)
    // This uses the standard transformation from Keplerian elements to Cartesian coordinates
    
    double a = semiMajorAxis;
    double e = eccentricity;
    double i = inclination;
    double Omega = longitudeOfAscendingNode;
    double omega = argumentOfPeriapsis;
    double nu = trueAnomaly;
    
    // Distance from focus
    double r = a * (1.0 - e * e) / (1.0 + e * std::cos(nu));
    
    // Position and velocity in orbital plane
    double cosNu = std::cos(nu);
    double sinNu = std::sin(nu);
    
    glm::dvec3 rOrb(r * cosNu, r * sinNu, 0.0);
    
    double h = std::sqrt(gravitationalParameter * a * (1.0 - e * e));
    double vr = gravitationalParameter / h * e * sinNu;
    double vt = gravitationalParameter / h * (1.0 + e * cosNu);
    
    glm::dvec3 vOrb(vr * cosNu - vt * sinNu, vr * sinNu + vt * cosNu, 0.0);
    
    // Rotation matrices for transformation to inertial frame
    double cosOmega = std::cos(Omega);
    double sinOmega = std::sin(Omega);
    double cosI = std::cos(i);
    double sinI = std::sin(i);
    double cosOmegaLower = std::cos(omega);
    double sinOmegaLower = std::sin(omega);
    
    // Combined rotation matrix
    glm::dmat3 R;
    R[0][0] = cosOmega * cosOmegaLower - sinOmega * sinOmegaLower * cosI;
    R[0][1] = -cosOmega * sinOmegaLower - sinOmega * cosOmegaLower * cosI;
    R[0][2] = sinOmega * sinI;
    
    R[1][0] = sinOmega * cosOmegaLower + cosOmega * sinOmegaLower * cosI;
    R[1][1] = -sinOmega * sinOmegaLower + cosOmega * cosOmegaLower * cosI;
    R[1][2] = -cosOmega * sinI;
    
    R[2][0] = sinOmegaLower * sinI;
    R[2][1] = cosOmegaLower * sinI;
    R[2][2] = cosI;
    
    // Transform to inertial frame
    glm::dvec3 position = R * rOrb;
    glm::dvec3 velocity = R * vOrb;
    
    return StateVector(position, velocity);
}

OrbitalElements OrbitalElements::FromStateVector(const StateVector& state, double gravitationalParameter) {
    // Convert state vector to orbital elements
    // This is the inverse of ToStateVector
    
    glm::dvec3 r = state.GetPosition();
    glm::dvec3 v = state.GetVelocity();
    
    double mu = gravitationalParameter;
    
    // Specific angular momentum
    glm::dvec3 h = glm::cross(r, v);
    double hMag = glm::length(h);
    
    // Node vector
    glm::dvec3 n(-h.y, h.x, 0.0);
    double nMag = glm::length(n);
    
    // Eccentricity vector
    double rMag = glm::length(r);
    double vMag = glm::length(v);
    glm::dvec3 e_vec = ((vMag * vMag - mu / rMag) * r - glm::dot(r, v) * v) / mu;
    double e = glm::length(e_vec);
    
    // Specific orbital energy
    double epsilon = vMag * vMag / 2.0 - mu / rMag;
    
    OrbitalElements elements;
    
    // Semi-major axis
    elements.semiMajorAxis = -mu / (2.0 * epsilon);
    elements.eccentricity = e;
    
    // Inclination
    elements.inclination = std::acos(h.z / hMag);
    
    // Longitude of ascending node
    if (nMag > 1e-10) {
        elements.longitudeOfAscendingNode = std::acos(n.x / nMag);
        if (n.y < 0.0) {
            elements.longitudeOfAscendingNode = 2.0 * M_PI - elements.longitudeOfAscendingNode;
        }
    } else {
        elements.longitudeOfAscendingNode = 0.0;
    }
    
    // Argument of periapsis
    if (nMag > 1e-10 && e > 1e-10) {
        elements.argumentOfPeriapsis = std::acos(glm::dot(n, e_vec) / (nMag * e));
        if (e_vec.z < 0.0) {
            elements.argumentOfPeriapsis = 2.0 * M_PI - elements.argumentOfPeriapsis;
        }
    } else {
        elements.argumentOfPeriapsis = 0.0;
    }
    
    // True anomaly
    if (e > 1e-10) {
        elements.trueAnomaly = std::acos(glm::dot(e_vec, r) / (e * rMag));
        if (glm::dot(r, v) < 0.0) {
            elements.trueAnomaly = 2.0 * M_PI - elements.trueAnomaly;
        }
    } else {
        // Circular orbit
        if (nMag > 1e-10) {
            elements.trueAnomaly = std::acos(glm::dot(n, r) / (nMag * rMag));
            if (r.z < 0.0) {
                elements.trueAnomaly = 2.0 * M_PI - elements.trueAnomaly;
            }
        } else {
            elements.trueAnomaly = std::acos(r.x / rMag);
            if (r.y < 0.0) {
                elements.trueAnomaly = 2.0 * M_PI - elements.trueAnomaly;
            }
        }
    }
    
    return elements;
}

OrbitalElements OrbitalElements::CircularOrbit(double radius, double inclination) {
    OrbitalElements elements;
    elements.semiMajorAxis = radius;
    elements.eccentricity = 0.0;
    elements.inclination = inclination;
    elements.longitudeOfAscendingNode = 0.0;
    elements.argumentOfPeriapsis = 0.0;
    elements.trueAnomaly = 0.0;
    return elements;
}

OrbitalElements OrbitalElements::EllipticalOrbit(double periapsis, double apoapsis, double inclination) {
    OrbitalElements elements;
    elements.semiMajorAxis = (periapsis + apoapsis) / 2.0;
    elements.eccentricity = (apoapsis - periapsis) / (apoapsis + periapsis);
    elements.inclination = inclination;
    elements.longitudeOfAscendingNode = 0.0;
    elements.argumentOfPeriapsis = 0.0;
    elements.trueAnomaly = 0.0;
    return elements;
}

} // namespace Kosmos