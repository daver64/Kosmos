#include "PlanetRenderer.h"
#include "Camera.h"
#include "../Core/Logger.h"
#include <GL/glew.h>
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
    
    // Calculate apparent angular size in pixels
    double apparentSize = radius / distance;
    double fov = glm::radians(60.0); // 60 degree FOV
    int screenHeight = 720; // TODO: get from window
    double pixelSize = (apparentSize * screenHeight) / fov;
    
    // Stars (Sun) always render as a visible sphere
    if (body->GetName() == "Sol") {
        // Render Sun as a scaled sphere for visibility across solar system
        int segments = 32, rings = 16;
        Mesh* sphereMesh = GetSphereMesh(segments, rings);
        if (!sphereMesh) return;
        
        m_shader->Use();
        
        // Disable depth test for the Sun to ensure it's never occluded by skybox
        glDisable(GL_DEPTH_TEST);
        
        glm::dvec3 relativePos = bodyPosition - cameraPosition;
        
        // Debug logging - log every frame for a few seconds to see direction
        static int sunLogCount = 0;
        if (sunLogCount < 60) {  // Log for first 60 frames (~1 second)
            Logger::GetInstance().Log(LogLevel::INFO, 
                "Sun render: distance=" + std::to_string(distance/1e9) + "Gm, " +
                "relPos=(" + std::to_string(relativePos.x/1e9) + "," +
                std::to_string(relativePos.y/1e9) + "," +
                std::to_string(relativePos.z/1e9) + ")Gm, " +
                "camPos=(" + std::to_string(cameraPosition.x/1e9) + "," +
                std::to_string(cameraPosition.y/1e9) + "," +
                std::to_string(cameraPosition.z/1e9) + ")Gm");
            sunLogCount++;
        }
        
        // Use the actual Sun radius - no artificial scaling needed
        double visualRadius = radius;  // 696,000 km actual radius
        
        // Use double precision matrices for accuracy, convert to float only for OpenGL
        glm::dmat4 model = glm::dmat4(1.0);
        model = glm::translate(model, relativePos);
        model = glm::scale(model, glm::dvec3(visualRadius));
        
        glm::dmat4 rotationOnly = camera->GetViewMatrix();
        rotationOnly[3] = glm::dvec4(0, 0, 0, 1);
        glm::dmat4 view = rotationOnly;
        glm::dmat4 projection = glm::dmat4(camera->GetProjectionMatrix());
        
        m_shader->SetMat4("model", glm::mat4(model));
        m_shader->SetMat4("view", glm::mat4(view));
        m_shader->SetMat4("projection", glm::mat4(projection));
        m_shader->SetVec3("objectColor", body->GetColor() * 1.5f);  // Bright
        m_shader->SetFloat("emissive", 1.0f);  // Self-luminous
        m_shader->SetVec3("lightPos", glm::vec3(0.0f));
        m_shader->SetVec3("viewPos", glm::vec3(0.0f));
        
        sphereMesh->Render();
        
        // Re-enable depth test
        glEnable(GL_DEPTH_TEST);
        
        m_shader->Unuse();
        return;
    }
    
    // If object is too small (< 5 pixels), render as a bright point
    if (pixelSize < 5.0) {
        // Disable depth test to avoid skybox occlusion
        glDisable(GL_DEPTH_TEST);
        RenderAsPoint(body, camera, cameraPosition);
        glEnable(GL_DEPTH_TEST);
        return;
    }
    
    // Select LOD based on apparent size
    int segments = 32, rings = 16;
    
    if (apparentSize > 0.1) {
        segments = 64;
        rings = 32;
    } else if (apparentSize < 0.01) {
        segments = 16;
        rings = 8;
    }
    
    Mesh* sphereMesh = GetSphereMesh(segments, rings);
    if (!sphereMesh) return;
    
    // Disable depth test for distant objects to avoid skybox occlusion
    // Only use depth test for nearby objects (within 1 million km)
    bool useDepthTest = (distance < 1e9);  // 1 million km
    if (!useDepthTest) {
        glDisable(GL_DEPTH_TEST);
    }
    
    // Use shader
    m_shader->Use();
    
    // For camera-relative rendering, we translate objects relative to camera position
    // and use an identity view matrix (camera at origin looking down -Z)
    glm::dvec3 relativePos = bodyPosition - cameraPosition;
    
    // Use double precision matrices for accuracy, convert to float only for OpenGL
    glm::dmat4 model = glm::dmat4(1.0);
    model = glm::translate(model, relativePos);
    model = glm::scale(model, glm::dvec3(body->GetRadius()));
    
    // Create a view matrix that only includes rotation, not translation
    // (since we've already translated to camera space)
    glm::dmat4 rotationOnly = camera->GetViewMatrix();
    rotationOnly[3] = glm::dvec4(0, 0, 0, 1);  // Remove translation component
    glm::dmat4 view = rotationOnly;
    
    glm::dmat4 projection = glm::dmat4(camera->GetProjectionMatrix());
    
    m_shader->SetMat4("model", glm::mat4(model));
    m_shader->SetMat4("view", glm::mat4(view));
    m_shader->SetMat4("projection", glm::mat4(projection));
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
    
    // Re-enable depth test if it was disabled
    if (!useDepthTest) {
        glEnable(GL_DEPTH_TEST);
    }
    
    m_shader->Unuse();
}

void PlanetRenderer::RenderAsPoint(const CelestialBody* body, Camera* camera, const glm::dvec3& cameraPosition) {
    // Render distant objects as bright points (like stars/planets in night sky)
    glm::dvec3 bodyPosition = body->GetPosition();
    glm::dvec3 relativePos = bodyPosition - cameraPosition;
    
    // Calculate brightness based on size and distance
    double distance = glm::length(relativePos);
    double radius = body->GetRadius();
    double brightness = (radius * radius) / (distance * distance);
    
    // Stars (like the Sun) are much brighter
    if (body->GetName() == "Sol") {
        brightness *= 100.0;
    }
    
    // Clamp brightness
    brightness = glm::clamp(brightness * 1e12, 0.3, 3.0);
    
    // Render as a small sphere instead of point
    m_shader->Use();
    
    // Set up matrices for a tiny sphere at the location - use double precision
    glm::dmat4 model = glm::dmat4(1.0);
    model = glm::translate(model, relativePos);
    model = glm::scale(model, glm::dvec3(radius * 0.0001));  // Very small visual representation
    
    glm::dmat4 rotationOnly = camera->GetViewMatrix();
    rotationOnly[3] = glm::dvec4(0, 0, 0, 1);
    glm::dmat4 view = rotationOnly;
    glm::dmat4 projection = glm::dmat4(camera->GetProjectionMatrix());
    
    m_shader->SetMat4("model", glm::mat4(model));
    m_shader->SetMat4("view", glm::mat4(view));
    m_shader->SetMat4("projection", glm::mat4(projection));
    m_shader->SetVec3("objectColor", body->GetColor() * glm::vec3(brightness));
    m_shader->SetFloat("emissive", 1.0f);  // Self-luminous
    m_shader->SetVec3("lightPos", glm::vec3(0.0f));
    m_shader->SetVec3("viewPos", glm::vec3(0.0f));
    
    // Use a low-poly sphere for points
    Mesh* pointMesh = GetSphereMesh(8, 4);
    if (pointMesh) {
        pointMesh->Render();
    }
    
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