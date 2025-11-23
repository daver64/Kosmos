#include "Integrator.h"

namespace Kosmos {

Integrator::Integrator(Method method)
    : m_method(method)
{
}

StateVector Integrator::Step(double t, const StateVector& state, double dt, DerivativeFunction derivative) {
    switch (m_method) {
        case Method::Euler:
            return StepEuler(t, state, dt, derivative);
        case Method::RK4:
            return StepRK4(t, state, dt, derivative);
        case Method::RK8:
            return StepRK8(t, state, dt, derivative);
        default:
            return StepRK4(t, state, dt, derivative);
    }
}

StateVector Integrator::StepEuler(double t, const StateVector& state, double dt, DerivativeFunction derivative) {
    StateVector k1 = derivative(t, state);
    return state + k1 * dt;
}

StateVector Integrator::StepRK4(double t, const StateVector& state, double dt, DerivativeFunction derivative) {
    // Classic 4th-order Runge-Kutta
    StateVector k1 = derivative(t, state);
    StateVector k2 = derivative(t + dt * 0.5, state + k1 * (dt * 0.5));
    StateVector k3 = derivative(t + dt * 0.5, state + k2 * (dt * 0.5));
    StateVector k4 = derivative(t + dt, state + k3 * dt);
    
    return state + (k1 + k2 * 2.0 + k3 * 2.0 + k4) * (dt / 6.0);
}

StateVector Integrator::StepRK8(double t, const StateVector& state, double dt, DerivativeFunction derivative) {
    // Dormand-Prince 8th order method (simplified)
    // Using coefficients from the Dormand-Prince 8(7) method
    
    StateVector k1 = derivative(t, state);
    StateVector k2 = derivative(t + dt / 18.0, state + k1 * (dt / 18.0));
    StateVector k3 = derivative(t + dt / 12.0, state + (k1 * 1.0 + k2 * 3.0) * (dt / 48.0));
    StateVector k4 = derivative(t + dt / 8.0, state + (k1 * 1.0 + k3 * 3.0) * (dt / 32.0));
    StateVector k5 = derivative(t + dt * 5.0 / 16.0, state + (k1 * 5.0 - k3 * 75.0 + k4 * 75.0) * (dt / 16.0));
    StateVector k6 = derivative(t + dt * 3.0 / 8.0, state + (k1 * 3.0 + k4 * 9.0 + k5 * 4.0) * (dt / 80.0));
    
    // Simplified combination for demonstration
    return state + (k1 * 23.0 + k3 * 125.0 + k4 * 125.0 - k5 * 81.0 + k6 * 125.0) * (dt / 192.0);
}

} // namespace Kosmos