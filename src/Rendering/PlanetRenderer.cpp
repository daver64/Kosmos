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
    
    // Temporary debug for first frame
    static int frameCount = 0;
    if (frameCount < 2 && (body->GetName() == "Earth" || body->GetName() == "Sol")) {
        LOG_INFO(body->GetName() + ": pos=(" + std::to_string(bodyPosition.x/1e6) + "," + 
                 std::to_string(bodyPosition.y/1e6) + "," + std::to_string(bodyPosition.z/1e6) + ")Mm cam=(" +
                 std::to_string(cameraPosition.x/1e6) + "," + std::to_string(cameraPosition.y/1e6) + "," + 
                 std::to_string(cameraPosition.z/1e6) + ")Mm dist=" + std::to_string(distance/1e3) + "km");
        if (body->GetName() == "Earth") frameCount++;
    }
    
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
    
    // For camera-relative rendering, we translate objects relative to camera position
    // and use an identity view matrix (camera at origin looking down -Z)
    glm::dvec3 relativePos = bodyPosition - cameraPosition;
    
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(relativePos));
    model = glm::scale(model, glm::vec3(body->GetRadius()));
    
    // Create a view matrix that only includes rotation, not translation
    // (since we've already translated to camera space)
    glm::dmat4 rotationOnly = camera->GetViewMatrix();
    rotationOnly[3] = glm::dvec4(0, 0, 0, 1);  // Remove translation component
    glm::mat4 view = glm::mat4(rotationOnly);
    
    glm::mat4 projection = camera->GetProjectionMatrix();
    
    m_shader->SetMat4("model", model);
    m_shader->SetMat4("view", view);
    m_shader->SetMat4("projection", projection);
    m_shader->SetVec3("objectColor", body->GetColor());
    
    // For stars (Sun), use full brightness. For planets, use lighting
    if (body->GetName() == "Sol") {
        // Sun is self-luminous - render at full brightness
        m_shader->SetFloat("emissive", 1.0f);
        m_shader->SetVec3("lightPos", glm::vec3(relativePos));  // Light position in camera space
        m_shader->SetVec3("viewPos", glm::vec3(0.0f, 0.0f, 0.0f));  // Camera at origin
    } else {
        // Planets get lit by the Sun
        m_shader->SetFloat("emissive", 0.0f);
        glm::dvec3 sunPos(0.0, 0.0, 0.0);  // Sun at origin in world space
        glm::dvec3 relativeSunPos = sunPos - cameraPosition;  // Sun in camera space
        m_shader->SetVec3("lightPos", glm::vec3(relativeSunPos));
        m_shader->SetVec3("viewPos", glm::vec3(0.0f, 0.0f, 0.0f));  // Camera at origin
    }
    
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