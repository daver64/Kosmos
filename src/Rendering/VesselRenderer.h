#pragma once

#include "Shader.h"
#include "VertLoader.h"
#include <memory>
#include <glm/glm.hpp>

namespace Kosmos {

class Camera;

/**
 * @brief Renders vessels (spacecraft) with their mesh models
 */
class VesselRenderer {
public:
    VesselRenderer();
    ~VesselRenderer();
    
    bool Initialize();
    void Shutdown();
    
    // Render a vessel model at a given position and orientation
    void Render(const VertModel* model, 
                const glm::dvec3& position,
                const glm::dmat3& orientation,
                Camera* camera, 
                const glm::dvec3& cameraPosition);
    
private:
    std::unique_ptr<Shader> m_shader;
};

} // namespace Kosmos
