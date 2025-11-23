#include "Application.h"
#include "../Platform/Window.h"
#include "../Rendering/Camera.h"
#include "../Rendering/Shader.h"
#include "../Rendering/Mesh.h"
#include "Logger.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Kosmos {

Application* Application::s_instance = nullptr;

Application::Application()
    : m_running(false)
    , m_lastFrameTime(0.0)
    , m_rotation(0.0f)
{
    s_instance = this;
}

Application::~Application() {
    s_instance = nullptr;
}

bool Application::Initialize(int argc, char** argv) {
    std::cout << "Kosmos Space Flight Simulator v0.1.0" << std::endl;
    std::cout << "Initializing..." << std::endl;
    
    // Initialize window
    m_window = std::make_unique<Window>();
    WindowConfig config;
    config.title = "Kosmos Space Flight Simulator";
    config.width = 1280;
    config.height = 720;
    
    if (!m_window->Initialize(config)) {
        LOG_ERROR("Failed to initialize window");
        return false;
    }
    
    // Initialize camera
    m_camera = std::make_unique<Camera>();
    m_camera->SetPosition(glm::dvec3(5.0, 3.0, 5.0));
    m_camera->SetTarget(glm::dvec3(0.0, 0.0, 0.0));
    m_camera->SetPerspective(45.0f, m_window->GetAspectRatio(), 0.1f, 1000.0f);
    
    // Load shader
    m_shader = std::make_unique<Shader>();
    if (!m_shader->LoadFromFiles("../../data/shaders/basic.vert", "../../data/shaders/basic.frag")) {
        LOG_ERROR("Failed to load shaders");
        return false;
    }
    
    // Create test cube
    m_testCube.reset(Mesh::CreateCube(2.0f));
    
    LOG_INFO("All systems initialized successfully");
    std::cout << "Initialization complete!" << std::endl;
    std::cout << "Press ESC to exit." << std::endl;
    
    m_running = true;
    return true;
}

void Application::Run() {
    auto lastTime = std::chrono::high_resolution_clock::now();
    
    std::cout << "Running simulation..." << std::endl;
    
    while (m_running && !m_window->ShouldClose()) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = currentTime - lastTime;
        double deltaTime = elapsed.count();
        lastTime = currentTime;
        
        ProcessEvents();
        Update(deltaTime);
        Render();
        
        m_window->SwapBuffers();
    }
}

void Application::ProcessEvents() {
    m_window->ProcessEvents();
}

void Application::Update(double deltaTime) {
    // Rotate the cube
    m_rotation += static_cast<float>(deltaTime) * 45.0f; // 45 degrees per second
    if (m_rotation > 360.0f) {
        m_rotation -= 360.0f;
    }
}

void Application::Render() {
    // Clear screen
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Use shader
    m_shader->Use();
    
    // Set up matrices
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::rotate(model, glm::radians(m_rotation), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(m_rotation * 0.5f), glm::vec3(1.0f, 0.0f, 0.0f));
    
    glm::mat4 view = m_camera->GetViewMatrix();
    glm::mat4 projection = m_camera->GetProjectionMatrix();
    
    m_shader->SetMat4("model", model);
    m_shader->SetMat4("view", view);
    m_shader->SetMat4("projection", projection);
    m_shader->SetVec3("objectColor", glm::vec3(0.3f, 0.6f, 0.9f));
    m_shader->SetVec3("lightPos", glm::vec3(5.0f, 5.0f, 5.0f));
    m_shader->SetVec3("viewPos", glm::vec3(m_camera->GetPosition()));
    
    // Render cube
    if (m_testCube) {
        m_testCube->Render();
    }
    
    m_shader->Unuse();
}

void Application::Shutdown() {
    std::cout << "Shutting down..." << std::endl;
    m_running = false;
    
    // TODO: Cleanup subsystems
}

} // namespace Kosmos
