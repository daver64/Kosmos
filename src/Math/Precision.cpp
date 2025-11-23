// High-precision math utilities
// Using GLM's double-precision types (dvec3, dmat4, etc.)

#include <glm/glm.hpp>

namespace Kosmos {

// GLM provides double-precision math through dvec3, dvec4, dmat4, etc.
// This is sufficient for orbital mechanics at solar system scales

// For interstellar scales, we may need:
// - Arbitrary precision arithmetic
// - Compensated summation (Kahan)
// - Relative coordinates

} // namespace Kosmos
