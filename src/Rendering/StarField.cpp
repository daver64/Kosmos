#include "StarField.h"
#include "Camera.h"
#include "Shader.h"
#include "Texture.h"
#include "../Core/Logger.h"
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Kosmos {

StarField::StarField()
    : m_VAO(0)
    , m_VBO(0)
    , m_initialized(false)
{
}

StarField::~StarField() {
    Shutdown();
}

bool StarField::Initialize() {
    LOG_INFO("Initializing StarField renderer...");
    
    // Load skybox shader
    m_shader = std::make_unique<Shader>();
    if (!m_shader->LoadFromFiles("data/shaders/skybox.vert", "data/shaders/skybox.frag")) {
        LOG_ERROR("Failed to load skybox shader");
        return false;
    }
    
    // Load skybox texture (try procedural first, then downloaded)
    m_skyboxTexture = std::make_unique<Texture>();
    bool textureLoaded = false;
    
    if (m_skyboxTexture->LoadFromFile("data/textures/skybox/milkyway_procedural_4k.jpg")) {
        LOG_INFO("Loaded procedural Milky Way skybox");
        textureLoaded = true;
    } else if (m_skyboxTexture->LoadFromFile("data/textures/skybox/milky_way_gaia_8k.png")) {
        LOG_INFO("Loaded Gaia Milky Way skybox");
        textureLoaded = true;
    } else if (m_skyboxTexture->LoadFromFile("data/textures/skybox/milkyway.jpg")) {
        LOG_INFO("Loaded Milky Way skybox");
        textureLoaded = true;
    }
    
    if (!textureLoaded) {
        LOG_ERROR("Failed to load any skybox texture");
        return false;
    }
    
    // Create skybox cube mesh
    CreateSkyboxCube();
    
    m_initialized = true;
    LOG_INFO("StarField initialized successfully");
    return true;
}

void StarField::CreateSkyboxCube() {
    // Skybox cube vertices (positions only, centered at origin)
    float skyboxVertices[] = {
        // Positions
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

        -1.0f,  1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f
    };
    
    // Create VAO and VBO
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    
    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
    
    // Position attribute
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    
    glBindVertexArray(0);
}

void StarField::Render(Camera* camera) {
    if (!m_initialized || !camera) return;
    
    // Disable depth writing (skybox should always be behind everything)
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    
    // Use skybox shader
    m_shader->Use();
    
    // Set uniforms
    m_shader->SetMat4("projection", camera->GetProjectionMatrix());
    m_shader->SetMat4("view", camera->GetViewMatrix());
    
    // Bind skybox texture
    m_skyboxTexture->Bind(0);
    m_shader->SetInt("skyboxTexture", 0);
    
    // Render cube
    glBindVertexArray(m_VAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    
    // Restore depth settings
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
}

void StarField::Shutdown() {
    if (m_VAO) {
        glDeleteVertexArrays(1, &m_VAO);
        m_VAO = 0;
    }
    if (m_VBO) {
        glDeleteBuffers(1, &m_VBO);
        m_VBO = 0;
    }
    
    m_initialized = false;
}

} // namespace Kosmos