#include "PlanetRenderer.h"
#include "Camera.h"
#include "../Core/Logger.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Kosmos {

PlanetRenderer::PlanetRenderer() {
}

PlanetRenderer::~PlanetRenderer() {
    Shutdown();
}

bool PlanetRenderer::Initialize() {
    m_shader = std::make_unique<Shader>();
    
    if (!m_shader->LoadFromFiles("data/shaders/basic.vert", "data/shaders/basic.frag")) {
        LOG_ERROR("PlanetRenderer: Failed to load shaders");
        return false;
    }
    
    // Pre-create common LOD levels
    GetSphereMesh(64, 32);  // High detail
    GetSphereMesh(32, 16);  // Medium detail
    GetSphereMesh(16, 8);   // Low detail
    
    LOG_INFO("PlanetRenderer initialized");
    return true;
}

void PlanetRenderer::Shutdown() {
    m_sphereMeshes.clear();
    m_shader.reset();
}

void PlanetRenderer::Render(const CelestialBody* body, Camera* camera, const glm::dvec3& cameraPosition) {
    if (!body || !camera || !m_shader) return;
    
    // Calculate distance for LOD selection
    glm::dvec3 bodyPosition = body->GetPosition();
    double distance = glm::length(bodyPosition - cameraPosition);
    double radius = body->GetRadius();
    
    // Select LOD based on apparent size
    int segments = 32, rings = 16;
    double apparentSize = radius / distance;
    
    if (apparentSize > 0.1) {
        segments = 64;
        rings = 32;
    } else if (apparentSize < 0.01) {
        segments = 16;
        rings = 8;
    }
    
    Mesh* sphereMesh = GetSphereMesh(segments, rings);
    if (!sphereMesh) return;
    
    // Use shader
    m_shader->Use();
    
    // Set up matrices relative to camera
    glm::dvec3 relativePos = bodyPosition - cameraPosition;
    
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(relativePos));
    model = glm::scale(model, glm::vec3(body->GetRadius()));
    
    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 projection = camera->GetProjectionMatrix();
    
    m_shader->SetMat4("model", model);
    m_shader->SetMat4("view", view);
    m_shader->SetMat4("projection", projection);
    m_shader->SetVec3("objectColor", body->GetColor());
    
    // Simple lighting from origin (star)
    glm::dvec3 lightPos(0.0, 0.0, 0.0);
    glm::dvec3 relativeLightPos = lightPos - cameraPosition;
    m_shader->SetVec3("lightPos", glm::vec3(relativeLightPos));
    m_shader->SetVec3("viewPos", glm::vec3(0.0f, 0.0f, 0.0f));  // Camera is at origin in view space
    
    // Render the sphere
    sphereMesh->Render();
    
    m_shader->Unuse();
}

Mesh* PlanetRenderer::GetSphereMesh(int segments, int rings) {
    int key = MakeLODKey(segments, rings);
    
    auto it = m_sphereMeshes.find(key);
    if (it != m_sphereMeshes.end()) {
        return it->second.get();
    }
    
    // Create new sphere mesh
    std::unique_ptr<Mesh> mesh(Mesh::CreateSphere(1.0f, segments, rings));
    Mesh* meshPtr = mesh.get();
    m_sphereMeshes[key] = std::move(mesh);
    
    return meshPtr;
}

} // namespace Kosmos