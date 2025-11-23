#pragma once

#include "StateVector.h"
#include <glm/glm.hpp>

namespace Kosmos {

/**
 * @brief Keplerian orbital elements
 * Describes an orbit in terms of classical orbital parameters
 */
struct OrbitalElements {
    double semiMajorAxis;        // a (meters)
    double eccentricity;         // e (0-1)
    double inclination;          // i (radians)
    double longitudeOfAscendingNode;  // Ω (radians)
    double argumentOfPeriapsis;  // ω (radians)
    double trueAnomaly;          // ν (radians)
    
    // Derived quantities
    double GetSemiMinorAxis() const;
    double GetPeriapsis() const;
    double GetApoapsis() const;
    double GetPeriod(double gravitationalParameter) const;
    double GetMeanMotion(double gravitationalParameter) const;
    
    // Conversion functions
    StateVector ToStateVector(double gravitationalParameter) const;
    static OrbitalElements FromStateVector(const StateVector& state, double gravitationalParameter);
    
    // Useful orbit types
    static OrbitalElements CircularOrbit(double radius, double inclination = 0.0);
    static OrbitalElements EllipticalOrbit(double periapsis, double apoapsis, double inclination = 0.0);
};

} // namespace Kosmos
