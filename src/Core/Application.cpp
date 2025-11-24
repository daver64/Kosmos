#include "Application.h"
#include "../Platform/Window.h"
#include "../Rendering/Camera.h"
#include "../Rendering/CameraController.h"
#include "../Rendering/Shader.h"
#include "../Rendering/Mesh.h"
#include "../Rendering/PlanetRenderer.h"
#include "../Rendering/StarField.h"
#include "../Physics/SolarSystem.h"
#include "../Physics/CelestialBody.h"
#include "../Physics/Vessel.h"
#include "Logger.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <GL/glew.h>
#include <SDL2/SDL_scancode.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Kosmos {

Application* Application::s_instance = nullptr;

Application::Application()
    : m_running(false)
    , m_lastFrameTime(0.0)
    , m_cameraDistance(3.0e7)  // 30 million meters from Earth
    , m_cameraYaw(0.0)
    , m_cameraPitch(0.3)
    , m_focusBody(nullptr)
    , m_previousFocusPos(0.0)
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
    
    // Initialize camera controller
    m_cameraController = std::make_unique<CameraController>(m_camera.get());
    
    // Create test vessel
    m_testVessel = std::make_unique<Vessel>();
    m_testVessel->SetName("Test Vessel");
    
    // Note: Camera will be positioned after solar system loads
    
    // Initialize planet renderer
    m_planetRenderer = std::make_unique<PlanetRenderer>();
    if (!m_planetRenderer->Initialize()) {
        LOG_ERROR("Failed to initialize planet renderer");
        return false;
    }
    
    // Initialize starfield background
    m_starField = std::make_unique<StarField>();
    if (!m_starField->Initialize()) {
        LOG_ERROR("Failed to initialize starfield");
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
        
        // Initialize free camera 100km above Earth's equator
        glm::dvec3 focusPos = earth->GetPosition();
        LOG_INFO("Earth position at startup: " + std::to_string(focusPos.x/1e9) + " Gm, " + 
                 std::to_string(focusPos.y/1e9) + " Gm, " + std::to_string(focusPos.z/1e9) + " Gm");
        
        double altitude = 100000.0;  // 100 km in meters
        double earthRadius = earth->GetRadius();
        
        // Use FREE camera mode positioned above Earth
        m_cameraController->SetMode(CameraMode::FREE);
        
        glm::dvec3 camOffset;
        camOffset.x = (earthRadius + altitude) * cos(0.0) * cos(0.0);  // On equator
        camOffset.y = (earthRadius + altitude) * sin(0.0);
        camOffset.z = (earthRadius + altitude) * cos(0.0) * sin(0.0);
        
        glm::dvec3 cameraPos = focusPos + camOffset;
        
        // Store Earth reference for camera tracking
        m_focusBody = earth;
        m_cameraOffset = camOffset;
        m_followDistance = glm::length(camOffset);
        
        m_cameraController->SetFreePosition(cameraPos);
        m_cameraController->SetFreeTarget(focusPos);  // Look at Earth's center
        
        // Force an immediate update to apply the position
        m_cameraController->Update(0.0);
        
        LOG_INFO("Camera initialized at 100km above Earth");
    }
    
    LOG_INFO("All systems initialized successfully");
    std::cout << "Initialization complete!" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  ESC - Exit" << std::endl;
    std::cout << "  F1 - Cockpit camera" << std::endl;
    std::cout << "  F2 - External camera" << std::endl;
    std::cout << "  F3 - Track camera" << std::endl;
    std::cout << "  F4 - Free camera" << std::endl;
    std::cout << "  V - Attach camera to vessel" << std::endl;
    std::cout << "  C - Cycle cockpit cameras" << std::endl;
    std::cout << "  X - Cycle external cameras" << std::endl;
    std::cout << "  , / . - Cycle focus body (prev/next planet)" << std::endl;
    std::cout << "  Q / E - Roll camera" << std::endl;
    std::cout << "  Mouse Wheel - Zoom (external) / Move (free)" << std::endl;
    std::cout << "  Arrow Keys - Rotate camera" << std::endl;
    std::cout << "  WASD - Move camera (free mode)" << std::endl;
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
        ProcessInput(deltaTime);
        Update(deltaTime);
        Render();
        
        m_window->SwapBuffers();
    }
}

void Application::ProcessEvents() {
    m_window->ProcessEvents();
}

void Application::ProcessInput(double deltaTime) {
    // Camera mode switching
    if (m_window->IsKeyPressed(SDL_SCANCODE_F1)) {
        m_cameraController->SetMode(CameraMode::COCKPIT);
    }
    if (m_window->IsKeyPressed(SDL_SCANCODE_F2)) {
        m_cameraController->SetMode(CameraMode::EXTERNAL);
    }
    if (m_window->IsKeyPressed(SDL_SCANCODE_F3)) {
        m_cameraController->SetMode(CameraMode::TRACK);
    }
    if (m_window->IsKeyPressed(SDL_SCANCODE_F4)) {
        m_cameraController->SetMode(CameraMode::FREE);
    }
    
    // Vessel attachment
    if (m_window->IsKeyPressed(SDL_SCANCODE_V)) {
        if (m_cameraController->GetAttachedVessel()) {
            m_cameraController->Detach();
        } else {
            m_cameraController->AttachToVessel(m_testVessel.get());
        }
    }
    
    // Camera cycling
    if (m_window->IsKeyPressed(SDL_SCANCODE_C)) {
        m_cameraController->NextCockpitCamera();
    }
    if (m_window->IsKeyPressed(SDL_SCANCODE_X)) {
        m_cameraController->NextExternalCamera();
    }
    
    // Planet/body cycling
    if (m_window->IsKeyPressed(SDL_SCANCODE_PERIOD)) {  // > key
        LOG_INFO("Period key pressed - cycling forward");
        CycleFocusBody(1);
    }
    if (m_window->IsKeyPressed(SDL_SCANCODE_COMMA)) {  // < key
        LOG_INFO("Comma key pressed - cycling backward");
        CycleFocusBody(-1);
    }
    
    // Camera controls based on mode
    CameraMode mode = m_cameraController->GetMode();
    
    if (mode == CameraMode::FREE) {
        // Free camera movement
        double moveSpeed = 1000.0 * deltaTime; // 1 km/s
        glm::dvec3 movement(0.0);
        
        if (m_window->IsKeyHeld(SDL_SCANCODE_W)) movement.z -= moveSpeed;
        if (m_window->IsKeyHeld(SDL_SCANCODE_S)) movement.z += moveSpeed;
        if (m_window->IsKeyHeld(SDL_SCANCODE_A)) movement.x -= moveSpeed;
        if (m_window->IsKeyHeld(SDL_SCANCODE_D)) movement.x += moveSpeed;
        if (m_window->IsKeyHeld(SDL_SCANCODE_SPACE)) movement.y += moveSpeed;
        if (m_window->IsKeyHeld(SDL_SCANCODE_LCTRL)) movement.y -= moveSpeed;
        
        if (glm::length(movement) > 0.001) {
            m_cameraController->MoveFree(movement);
        }
        
        // Free camera rotation with arrow keys
        double rotSpeed = 0.5 * deltaTime; // 0.5 radian/s
        if (m_window->IsKeyHeld(SDL_SCANCODE_LEFT)) {
            m_cameraController->RotateFree(-rotSpeed, 0.0);
        }
        if (m_window->IsKeyHeld(SDL_SCANCODE_RIGHT)) {
            m_cameraController->RotateFree(rotSpeed, 0.0);
        }
        if (m_window->IsKeyHeld(SDL_SCANCODE_UP)) {
            m_cameraController->RotateFree(0.0, rotSpeed);
        }
        if (m_window->IsKeyHeld(SDL_SCANCODE_DOWN)) {
            m_cameraController->RotateFree(0.0, -rotSpeed);
        }
        if (m_window->IsKeyHeld(SDL_SCANCODE_Q)) {
            m_cameraController->RollFree(-rotSpeed);
        }
        if (m_window->IsKeyHeld(SDL_SCANCODE_E)) {
            m_cameraController->RollFree(rotSpeed);
        }
    }
    else if (mode == CameraMode::EXTERNAL) {
        // External camera rotation with arrow keys
        double rotSpeed = 1.0 * deltaTime;
        if (m_window->IsKeyHeld(SDL_SCANCODE_LEFT)) {
            m_cameraController->RotateExternal(-rotSpeed, 0.0);
        }
        if (m_window->IsKeyHeld(SDL_SCANCODE_RIGHT)) {
            m_cameraController->RotateExternal(rotSpeed, 0.0);
        }
        if (m_window->IsKeyHeld(SDL_SCANCODE_UP)) {
            m_cameraController->RotateExternal(0.0, rotSpeed);
        }
        if (m_window->IsKeyHeld(SDL_SCANCODE_DOWN)) {
            m_cameraController->RotateExternal(0.0, -rotSpeed);
        }
        
        // External camera zoom with Page Up/Down
        if (m_window->IsKeyHeld(SDL_SCANCODE_PAGEUP)) {
            m_cameraController->AdjustExternalDistance(-10.0 * deltaTime);
        }
        if (m_window->IsKeyHeld(SDL_SCANCODE_PAGEDOWN)) {
            m_cameraController->AdjustExternalDistance(10.0 * deltaTime);
        }
    }
}

void Application::Update(double deltaTime) {
    // Update solar system physics
    if (m_solarSystem) {
        m_solarSystem->Update(deltaTime);
    }
    
    // Update test vessel position (orbit around focus body)
    if (m_testVessel && m_focusBody) {
        glm::dvec3 focusPos = m_focusBody->GetPosition();
        // Place vessel in orbit at 400 km altitude
        double orbitRadius = m_focusBody->GetRadius() + 400000.0; // 400 km
        double orbitAngle = m_solarSystem->GetJulianDate() * 0.001; // Slow orbit
        glm::dvec3 vesselPos;
        vesselPos.x = focusPos.x + orbitRadius * cos(orbitAngle);
        vesselPos.y = focusPos.y;
        vesselPos.z = focusPos.z + orbitRadius * sin(orbitAngle);
        m_testVessel->SetPosition(vesselPos);
    }
    
    // Update camera controller
    if (m_cameraController) {
        UpdateCamera();  // Update camera tracking first
        m_cameraController->Update(deltaTime);
    }
}

void Application::UpdateCamera() {
    if (!m_focusBody) return;
    
    glm::dvec3 focusPos = m_focusBody->GetPosition();
    
    // Smooth interpolation of focus body position to reduce jitter
    double smoothing = 0.85;  // Higher = more responsive, lower = smoother
    glm::dvec3 smoothedFocusPos = m_previousFocusPos * (1.0 - smoothing) + focusPos * smoothing;
    m_previousFocusPos = smoothedFocusPos;
    
    // If in FREE mode, update camera position to follow the focus body
    // but preserve the user's rotation (forward direction)
    if (m_cameraController->GetMode() == CameraMode::FREE) {
        glm::dvec3 forward = m_cameraController->GetFreeForward();
        
        // Update position to maintain distance from focus body
        glm::dvec3 newCameraPos = smoothedFocusPos + m_cameraOffset;
        m_cameraController->SetFreePosition(newCameraPos);
        
        // Update target using the preserved forward direction
        // Use SetFreeTargetDirect to avoid recalculating orientation!
        glm::dvec3 newTarget = newCameraPos + forward * m_followDistance;
        m_cameraController->SetFreeTargetDirect(newTarget);
        return;
    }
    
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
    
    // Render starfield background first
    if (m_starField) {
        m_starField->Render(m_camera.get());
    }
    
    // Render all celestial bodies
    if (m_solarSystem && m_planetRenderer) {
        glm::dvec3 camPos = m_camera->GetPosition();
        
        // Debug log once per run
        static bool logged = false;
        if (!logged) {
            LOG_INFO("Rendering " + std::to_string(m_solarSystem->GetBodies().size()) + " celestial bodies");
            for (const auto& body : m_solarSystem->GetBodies()) {
                glm::dvec3 pos = body->GetPosition();
                glm::dvec3 relPos = pos - camPos;
                double distance = glm::length(relPos);
                LOG_INFO(body->GetName() + ": pos=(" + 
                         std::to_string(pos.x/1e9) + "," + 
                         std::to_string(pos.y/1e9) + "," + 
                         std::to_string(pos.z/1e9) + ")Gm dist=" + 
                         std::to_string(distance/1e6) + "Mm radius=" + 
                         std::to_string(body->GetRadius()/1e3) + "km");
            }
            logged = true;
        }
        
        for (const auto& body : m_solarSystem->GetBodies()) {
            m_planetRenderer->Render(body.get(), m_camera.get(), camPos);
        }
    }
}

void Application::Shutdown() {
    std::cout << "Shutting down..." << std::endl;
    m_running = false;
    
    // TODO: Cleanup subsystems
}

void Application::CycleFocusBody(int direction) {
    if (!m_solarSystem || m_solarSystem->GetBodies().empty()) return;
    
    // Find current focus body index
    int currentIndex = -1;
    const auto& bodies = m_solarSystem->GetBodies();
    for (size_t i = 0; i < bodies.size(); ++i) {
        if (bodies[i].get() == m_focusBody) {
            currentIndex = static_cast<int>(i);
            break;
        }
    }
    
    // Cycle to next/previous body
    int newIndex = (currentIndex + direction + bodies.size()) % bodies.size();
    m_focusBody = bodies[newIndex].get();
    
    // Initialize smoothing
    m_previousFocusPos = m_focusBody->GetPosition();
    
    // Reposition camera around new focus
    glm::dvec3 focusPos = m_focusBody->GetPosition();
    double distance = glm::max(m_focusBody->GetRadius() * 5.0, 10000.0);  // 5 radii or 10km min
    
    m_cameraOffset.x = distance;
    m_cameraOffset.y = distance * 0.3;
    m_cameraOffset.z = 0.0;
    m_followDistance = glm::length(m_cameraOffset);
    
    glm::dvec3 cameraPos = focusPos + m_cameraOffset;
    m_cameraController->SetFreePosition(cameraPos);
    m_cameraController->SetFreeTarget(focusPos);
    
    LOG_INFO("Focus changed to: " + m_focusBody->GetName() + " (distance: " + 
             std::to_string(distance/1000.0) + " km)");
}

} // namespace Kosmos
