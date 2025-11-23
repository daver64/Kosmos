#pragma once

#include <string>
#include <memory>
#include <glm/glm.hpp>

namespace Kosmos {

class Window;
class Camera;
class Shader;
class Mesh;
class PlanetRenderer;
class SolarSystem;
class CelestialBody;

class Application {
public:
    Application();
    ~Application();
    
    bool Initialize(int argc, char** argv);
    void Run();
    void Shutdown();
    
    static Application* GetInstance() { return s_instance; }
    
    Window* GetWindow() { return m_window.get(); }
    Camera* GetCamera() { return m_camera.get(); }
    
private:
    static Application* s_instance;
    
    bool m_running;
    double m_lastFrameTime;
    
    std::unique_ptr<Window> m_window;
    std::unique_ptr<Camera> m_camera;
    std::unique_ptr<PlanetRenderer> m_planetRenderer;
    std::unique_ptr<SolarSystem> m_solarSystem;
    
    CelestialBody* m_focusBody;
    glm::dvec3 m_cameraPosition;
    double m_cameraDistance;
    double m_cameraYaw;
    double m_cameraPitch;
    
    void ProcessEvents();
    void Update(double deltaTime);
    void UpdateCamera();
    void Render();
};

} // namespace Kosmos
