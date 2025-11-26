#pragma once

#include "Mesh.h"
#include "Shader.h"
#include "Texture.h"
#include "../Physics/CelestialBody.h"
#include <memory>
#include <unordered_map>

namespace Kosmos {

class Camera;

/**
 * @brief Renders celestial bodies (planets, stars, moons) as spheres
 * Supports textured spheres with Celestia-format textures and atmospheric scattering
 */
class PlanetRenderer {
public:
    PlanetRenderer();
    ~PlanetRenderer();
    
    bool Initialize();
    void Shutdown();
    
    // Render a single celestial body
    void Render(const CelestialBody* body, Camera* camera, const glm::dvec3& cameraPosition);
    
    // Get or create sphere mesh with given detail level
    Mesh* GetSphereMesh(int segments = 32, int rings = 16);
    
private:
    void RenderAsPoint(const CelestialBody* body, Camera* camera, const glm::dvec3& cameraPosition);
    void RenderAtmosphere(const CelestialBody* body, Camera* camera, const glm::dvec3& cameraPosition, double planetRadius);
    Texture* GetOrLoadTexture(const std::string& texturePath);
    bool HasAtmosphere(const std::string& bodyName) const;
    
    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<Shader> m_atmosphereShader;
    std::unordered_map<int, std::unique_ptr<Mesh>> m_sphereMeshes;  // LOD system
    std::unordered_map<std::string, std::unique_ptr<Texture>> m_textureCache;  // Texture cache
    
    int MakeLODKey(int segments, int rings) const { return segments * 1000 + rings; }
};

} // namespace Kosmos
