#pragma once

#include "../Math/StateVector.h"
#include <functional>

namespace Kosmos {

/**
 * @brief Numerical integrator for orbital mechanics
 * Supports multiple integration methods
 */
class Integrator {
public:
    using DerivativeFunction = std::function<StateVector(double, const StateVector&)>;
    
    enum class Method {
        Euler,          // Simple Euler (1st order)
        RK4,            // Runge-Kutta 4th order
        RK8             // Runge-Kutta 8th order (Dormand-Prince)
    };
    
    Integrator(Method method = Method::RK4);
    ~Integrator() = default;
    
    void SetMethod(Method method) { m_method = method; }
    Method GetMethod() const { return m_method; }
    
    // Integrate one step
    StateVector Step(double t, const StateVector& state, double dt, DerivativeFunction derivative);
    
private:
    Method m_method;
    
    StateVector StepEuler(double t, const StateVector& state, double dt, DerivativeFunction derivative);
    StateVector StepRK4(double t, const StateVector& state, double dt, DerivativeFunction derivative);
    StateVector StepRK8(double t, const StateVector& state, double dt, DerivativeFunction derivative);
};

} // namespace Kosmos
