#include "VesselRenderer.h"
#include "Camera.h"
#include "../Core/Logger.h"
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Kosmos {

// Simple material color lookup based on material index
static glm::vec3 GetMaterialColor(int materialIndex) {
    switch (materialIndex) {
        case 1: return glm::vec3(0.7f, 0.7f, 0.7f);   // Light grey
        case 2: return glm::vec3(0.3f, 0.3f, 0.4f);   // Dark blue-grey
        case 3: return glm::vec3(0.8f, 0.6f, 0.2f);   // Gold/copper
        case 4: return glm::vec3(0.2f, 0.2f, 0.2f);   // Dark grey/black
        default: return glm::vec3(0.5f, 0.5f, 0.5f);  // Medium grey
    }
}

VesselRenderer::VesselRenderer() {
}

VesselRenderer::~VesselRenderer() {
    Shutdown();
}

bool VesselRenderer::Initialize() {
    m_shader = std::make_unique<Shader>();
    
    if (!m_shader->LoadFromFiles("data/shaders/basic.vert", "data/shaders/basic.frag")) {
        LOG_ERROR("VesselRenderer: Failed to load shaders");
        return false;
    }
    
    LOG_INFO("VesselRenderer initialized");
    return true;
}

void VesselRenderer::Shutdown() {
    m_shader.reset();
}

void VesselRenderer::Render(const VertModel* model,
                            const glm::dvec3& position,
                            const glm::dmat3& orientation,
                            Camera* camera,
                            const glm::dvec3& cameraPosition) {
    if (!model || !camera || !m_shader) return;
    
    m_shader->Use();
    
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    
    // Calculate camera-relative position for precision
    glm::dvec3 relativePos = position - cameraPosition;
    
    // Build model matrix with orientation
    glm::dmat4 modelMatrix = glm::dmat4(1.0);
    modelMatrix = glm::translate(modelMatrix, relativePos);
    
    // Apply orientation (rotation matrix)
    glm::dmat4 rotationMatrix = glm::dmat4(orientation);
    modelMatrix = modelMatrix * rotationMatrix;
    
    // Get camera view (rotation only, no translation since we're using relative coords)
    glm::dmat4 rotationOnly = camera->GetViewMatrix();
    rotationOnly[3] = glm::dvec4(0, 0, 0, 1);
    glm::dmat4 view = rotationOnly;
    glm::dmat4 projection = glm::dmat4(camera->GetProjectionMatrix());
    
    // Set common uniforms
    m_shader->SetMat4("view", glm::mat4(view));
    m_shader->SetMat4("projection", glm::mat4(projection));
    m_shader->SetFloat("emissive", 0.0f);  // Vessels are not self-luminous
    
    // Simple lighting from camera position (headlight effect)
    m_shader->SetVec3("lightPos", glm::vec3(0.0f, 0.0f, 0.0f));  // Light at camera
    m_shader->SetVec3("viewPos", glm::vec3(0.0f, 0.0f, 0.0f));   // Camera at origin (relative coords)
    
    // Render each mesh group
    static bool loggedOnce = false;
    for (size_t i = 0; i < model->groups.size(); ++i) {
        const auto& group = model->groups[i];
        if (!group.mesh) continue;
        
        // Set model matrix for this group
        m_shader->SetMat4("model", glm::mat4(modelMatrix));
        
        // Get material color from group
        glm::vec3 matColor = glm::vec3(group.material.diffuse.r, group.material.diffuse.g, group.material.diffuse.b);
        m_shader->SetVec3("objectColor", matColor);
        
        // Check if this group has a texture
        bool hasTexture = false;
        if (group.diffuseTexture && group.diffuseTexture->GetID() != 0) {
            group.diffuseTexture->Bind(0);
            m_shader->SetInt("textureSampler", 0);
            m_shader->SetBool("useTexture", true);
            hasTexture = true;
            
            if (!loggedOnce) {
                LOG_INFO("VesselRenderer: Group " + std::to_string(i) + " using texture (ID=" + 
                         std::to_string(group.diffuseTexture->GetID()) + ")");
            }
        }
        
        if (!hasTexture) {
            // No texture, ensure it's disabled
            m_shader->SetBool("useTexture", false);
            glBindTexture(GL_TEXTURE_2D, 0);  // Unbind any previous texture
            
            if (!loggedOnce) {
                LOG_INFO("VesselRenderer: Group " + std::to_string(i) + " using material color (" + 
                         std::to_string(matColor.r) + "," + std::to_string(matColor.g) + "," + 
                         std::to_string(matColor.b) + ")");
            }
        }
        
        group.mesh->Render();
        
        if (hasTexture) {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
    loggedOnce = true;
    
    glDisable(GL_CULL_FACE);
    m_shader->Unuse();
}

} // namespace Kosmos
