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
    
    // Load atmosphere shader
    m_atmosphereShader = std::make_unique<Shader>();
    if (!m_atmosphereShader->LoadFromFiles("data/shaders/atmosphere.vert", "data/shaders/atmosphere.frag")) {
        LOG_WARNING("PlanetRenderer: Failed to load atmosphere shaders - atmospheres will be disabled");
        m_atmosphereShader.reset();
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
    m_textureCache.clear();
    m_shader.reset();
    m_atmosphereShader.reset();
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
    
    // Debug logging for Moon/Earth visibility
    if (body->GetName() == "Moon" || body->GetName() == "Earth") {
        glm::dvec3 relativePos = bodyPosition - cameraPosition;
        
        // Check camera orientation
        static bool camDebug = false;
        if (!camDebug && body->GetName() == "Moon") {
            glm::dvec3 camPos = camera->GetPosition();
            glm::dvec3 camTarget = camera->GetTarget();
            glm::dvec3 camForward = glm::normalize(camTarget - camPos);
            std::cerr << ">>> Camera: pos=(" << (camPos.x/1e6) << "," << (camPos.y/1e6) << "," << (camPos.z/1e6) << ")Mm"
                      << " target=(" << (camTarget.x/1e6) << "," << (camTarget.y/1e6) << "," << (camTarget.z/1e6) << ")Mm"
                      << " forward=(" << camForward.x << "," << camForward.y << "," << camForward.z << ")" << std::endl;
            camDebug = true;
        }
        
        // Check if object is in front of camera (negative Z in view space)
        glm::dmat4 viewMatrix = camera->GetViewMatrix();
        glm::dvec4 viewSpacePos = viewMatrix * glm::dvec4(relativePos, 1.0);
    }
    
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
        
        // Get rotation-only view matrix for camera-relative rendering
        glm::dmat4 view = glm::dmat4(camera->GetViewMatrix());
        glm::dmat4 projection = glm::dmat4(camera->GetProjectionMatrix());
        
        m_shader->SetMat4("model", glm::mat4(model));
        m_shader->SetMat4("view", glm::mat4(view));
        m_shader->SetMat4("projection", glm::mat4(projection));
        m_shader->SetVec3("objectColor", body->GetColor() * 1.5f);  // Bright
        m_shader->SetBool("useTexture", false);
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
    
    // Select LOD based on apparent size and distance
    int segments = 32, rings = 16;
    
    // Use higher detail for closest body (camera focus)
    if (distance < 1e8) {  // Within 100,000 km - very close (e.g., orbiting Earth)
        segments = 128;
        rings = 64;
    } else if (apparentSize > 0.1) {
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
    if (useDepthTest) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    
    // Temporarily disable face culling to debug rendering issues
    glDisable(GL_CULL_FACE);
    
    // Use shader
    m_shader->Use();
    
    // For camera-relative rendering, we translate objects relative to camera position
    // and use a view matrix with only rotation (no translation)
    glm::dvec3 relativePos = bodyPosition - cameraPosition;
    
    // Use double precision matrices for accuracy, convert to float only for OpenGL
    glm::dmat4 model = glm::dmat4(1.0);
    model = glm::translate(model, relativePos);
    model = glm::scale(model, glm::dvec3(body->GetRadius()));
    
    // Get rotation-only view matrix for camera-relative rendering
    glm::dmat4 view = glm::dmat4(camera->GetViewMatrix());
    
    glm::dmat4 projection = glm::dmat4(camera->GetProjectionMatrix());
    
    m_shader->SetMat4("model", glm::mat4(model));
    m_shader->SetMat4("view", glm::mat4(view));
    m_shader->SetMat4("projection", glm::mat4(projection));
    
    // Try to load and use texture if available
    bool useTexture = false;
    if (!body->GetTexture().empty()) {
        Texture* texture = GetOrLoadTexture(body->GetTexture());
        if (texture) {
            texture->Bind(0);
            m_shader->SetInt("textureSampler", 0);
            m_shader->SetBool("useTexture", true);
            useTexture = true;
        }
    }
    
    if (!useTexture) {
        m_shader->SetVec3("objectColor", body->GetColor());
        m_shader->SetBool("useTexture", false);
    }
    
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
    
    // Re-enable face culling
    glEnable(GL_CULL_FACE);
    
    // Re-enable depth test if it was disabled
    if (!useDepthTest) {
        glEnable(GL_DEPTH_TEST);
    }
    
    m_shader->Unuse();
    
    // Render atmosphere if this body has one and we're close enough to see it
    if (HasAtmosphere(body->GetName()) && distance < radius * 100.0) {
        RenderAtmosphere(body, camera, cameraPosition, radius);
    }
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
    m_shader->SetBool("useTexture", false);
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

Texture* PlanetRenderer::GetOrLoadTexture(const std::string& texturePath) {
    // Check if already loaded
    auto it = m_textureCache.find(texturePath);
    if (it != m_textureCache.end()) {
        return it->second.get();
    }
    
    // Load new texture
    std::unique_ptr<Texture> texture = std::make_unique<Texture>();
    std::string fullPath = "data/textures/planets/" + texturePath;
    
    if (texture->LoadFromFile(fullPath)) {
        Texture* texturePtr = texture.get();
        m_textureCache[texturePath] = std::move(texture);
        LOG_INFO("PlanetRenderer: Loaded texture " + fullPath);
        return texturePtr;
    } else {
        LOG_WARNING("PlanetRenderer: Failed to load texture " + fullPath);
        return nullptr;
    }
}

bool PlanetRenderer::HasAtmosphere(const std::string& bodyName) const {
    // List of bodies with significant atmospheres
    return bodyName == "Earth" || 
           bodyName == "Mars" || 
           bodyName == "Venus" || 
           bodyName == "Titan" ||
           bodyName == "Jupiter" ||
           bodyName == "Saturn" ||
           bodyName == "Uranus" ||
           bodyName == "Neptune";
}

void PlanetRenderer::RenderAtmosphere(const CelestialBody* body, Camera* camera, 
                                      const glm::dvec3& cameraPosition, double planetRadius) {
    if (!m_atmosphereShader) return;
    
    // Calculate atmosphere parameters based on body type
    double atmosphereHeight = planetRadius * 0.05;  // 5% of planet radius
    glm::vec3 atmosphereColor(0.5f, 0.7f, 1.0f);   // Default blue
    
    // Customize atmosphere for different bodies
    if (body->GetName() == "Earth") {
        atmosphereHeight = planetRadius * 0.08;  // Thicker for more visible haze
        atmosphereColor = glm::vec3(0.5f, 0.7f, 1.0f);  // Bright sky blue
    } else if (body->GetName() == "Mars") {
        atmosphereHeight = planetRadius * 0.05;  // Thinner atmosphere
        atmosphereColor = glm::vec3(0.9f, 0.6f, 0.4f);  // Peachy red-orange
    } else if (body->GetName() == "Venus") {
        atmosphereHeight = planetRadius * 0.10;  // Very thick atmosphere
        atmosphereColor = glm::vec3(1.0f, 0.95f, 0.7f);  // Pale yellow
    } else if (body->GetName() == "Titan") {
        atmosphereHeight = planetRadius * 0.08;
        atmosphereColor = glm::vec3(1.0f, 0.7f, 0.4f);  // Orange haze
    } else if (body->GetName() == "Jupiter" || body->GetName() == "Saturn" || 
               body->GetName() == "Uranus" || body->GetName() == "Neptune") {
        atmosphereHeight = planetRadius * 0.06;  // Gas giants
        atmosphereColor = body->GetColor() * 1.2f;  // Slightly brighter
    }
    
    double atmosphereRadius = planetRadius + atmosphereHeight;
    
    // Set up rendering state for transparent atmosphere
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);  // Don't write to depth buffer
    glDisable(GL_CULL_FACE);  // Render both sides
    
    m_atmosphereShader->Use();
    
    // Get appropriate LOD mesh
    glm::dvec3 bodyPosition = body->GetPosition();
    double distance = glm::length(bodyPosition - cameraPosition);
    int segments = 32, rings = 16;
    if (distance < planetRadius * 10.0) {
        segments = 64;
        rings = 32;
    }
    Mesh* atmosphereMesh = GetSphereMesh(segments, rings);
    if (!atmosphereMesh) {
        m_atmosphereShader->Unuse();
        glEnable(GL_CULL_FACE);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        return;
    }
    
    // Set up matrices
    glm::dvec3 relativePos = bodyPosition - cameraPosition;
    glm::dmat4 model = glm::dmat4(1.0);
    model = glm::translate(model, relativePos);
    model = glm::scale(model, glm::dvec3(atmosphereRadius));
    
    glm::dmat4 view = glm::dmat4(camera->GetViewMatrix());
    glm::dmat4 projection = glm::dmat4(camera->GetProjectionMatrix());
    
    m_atmosphereShader->SetMat4("model", glm::mat4(model));
    m_atmosphereShader->SetMat4("view", glm::mat4(view));
    m_atmosphereShader->SetMat4("projection", glm::mat4(projection));
    
    // Set atmosphere parameters
    m_atmosphereShader->SetVec3("planetCenter", glm::vec3(relativePos));
    m_atmosphereShader->SetFloat("planetRadius", static_cast<float>(planetRadius));
    m_atmosphereShader->SetFloat("atmosphereRadius", static_cast<float>(atmosphereRadius));
    m_atmosphereShader->SetVec3("atmosphereColor", atmosphereColor);
    
    // Light direction (from planet to sun)
    glm::dvec3 sunPos(0.0, 0.0, 0.0);  // Sun at origin
    glm::dvec3 lightDir = glm::normalize(sunPos - bodyPosition);
    m_atmosphereShader->SetVec3("lightDir", glm::vec3(lightDir));
    
    // Render atmosphere
    atmosphereMesh->Render();
    
    // Restore rendering state
    m_atmosphereShader->Unuse();
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

} // namespace Kosmos