#include "Application.h"
#include "../Platform/Window.h"
#include "../Rendering/Camera.h"
#include "../Rendering/Shader.h"
#include "../Rendering/Mesh.h"
#include "../Rendering/PlanetRenderer.h"
#include "../Physics/SolarSystem.h"
#include "../Physics/CelestialBody.h"
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
    , m_cameraDistance(3.0e7)  // 30 million meters from Earth
    , m_cameraYaw(0.0)
    , m_cameraPitch(0.3)
{
    s_instance = this;
}

Application::~Application() {
    if (m_planetRenderer) {
        m_planetRenderer->Shutdown();
    }
    s_instance = nullptr;
}

bool Application::Initialize(int /*argc*/, char** /*argv*/) {
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
    
    // Initialize camera for astronomical scales
    m_camera = std::make_unique<Camera>();
    // Near: 1 km, Far: 1 billion km (beyond Neptune's orbit)
    m_camera->SetPerspective(45.0f, m_window->GetAspectRatio(), 1e3f, 1e12f);
    
    // Initialize planet renderer
    m_planetRenderer = std::make_unique<PlanetRenderer>();
    if (!m_planetRenderer->Initialize()) {
        LOG_ERROR("Failed to initialize planet renderer");
        return false;
    }
    
    // Create solar system with VSOP87
    m_solarSystem = SolarSystem::CreateSolSystem();
    m_solarSystem->EnableVSOP87("data/vsop87", 'D');
    m_solarSystem->SetTimeAcceleration(100.0);  // Speed up time 100x
    
    // Set Julian Date to J2000.0 (January 1, 2000, 12:00 TT)
    m_solarSystem->SetJulianDate(2451545.0);
    
    // Focus camera on Earth
    CelestialBody* earth = m_solarSystem->GetBody("Earth");
    if (earth) {
        m_focusBody = earth;
        UpdateCamera();
    }
    
    LOG_INFO("All systems initialized successfully");
    std::cout << "Initialization complete!" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  ESC - Exit" << std::endl;
    std::cout << "  Mouse Wheel - Zoom" << std::endl;
    std::cout << "  Arrow Keys - Rotate camera" << std::endl;
    std::cout << "  1/2 - Decrease/Increase time acceleration" << std::endl;
    
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
    // Update solar system physics
    if (m_solarSystem) {
        m_solarSystem->Update(deltaTime);
    }
    
    // Update camera to follow focus body
    if (m_focusBody) {
        UpdateCamera();
    }
}

void Application::UpdateCamera() {
    if (!m_focusBody) return;
    
    glm::dvec3 focusPos = m_focusBody->GetPosition();
    
    // Debug log position
    static bool logged = false;
    if (!logged) {
        LOG_INFO("Focus body: " + m_focusBody->GetName());
        LOG_INFO("Position (Gm): " + std::to_string(focusPos.x/1e9) + ", " + 
                 std::to_string(focusPos.y/1e9) + ", " + 
                 std::to_string(focusPos.z/1e9));
        LOG_INFO("Camera distance (Mm): " + std::to_string(m_cameraDistance/1e6));
        logged = true;
    }
    
    // Position camera in orbit around focus body
    glm::dvec3 camOffset;
    camOffset.x = m_cameraDistance * cos(m_cameraPitch) * cos(m_cameraYaw);
    camOffset.y = m_cameraDistance * sin(m_cameraPitch);
    camOffset.z = m_cameraDistance * cos(m_cameraPitch) * sin(m_cameraYaw);
    
    m_cameraPosition = focusPos + camOffset;
    m_camera->SetPosition(m_cameraPosition);
    m_camera->SetTarget(focusPos);
}

void Application::Render() {
    // Clear screen with black space background
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Render all celestial bodies
    if (m_solarSystem && m_planetRenderer) {
        for (const auto& body : m_solarSystem->GetBodies()) {
            m_planetRenderer->Render(body.get(), m_camera.get(), m_cameraPosition);
        }
    }
}

void Application::Shutdown() {
    std::cout << "Shutting down..." << std::endl;
    m_running = false;
    
    // TODO: Cleanup subsystems
}

} // namespace Kosmos
