#pragma once

#include <string>
#include <memory>
#include <glm/glm.hpp>

namespace Kosmos {

class Window;
class Camera;
class CameraController;
class Shader;
class Mesh;
class PlanetRenderer;
class VesselRenderer;
class StarField;
class SolarSystem;
class CelestialBody;
class Vessel;
struct VertModel;

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
    CameraController* GetCameraController() { return m_cameraController.get(); }
    
private:
    static Application* s_instance;
    
    bool m_running;
    double m_lastFrameTime;
    
    std::unique_ptr<Window> m_window;
    std::unique_ptr<Camera> m_camera;
    std::unique_ptr<CameraController> m_cameraController;
    std::unique_ptr<PlanetRenderer> m_planetRenderer;
    std::unique_ptr<VesselRenderer> m_vesselRenderer;
    std::unique_ptr<StarField> m_starField;
    std::unique_ptr<SolarSystem> m_solarSystem;
    std::unique_ptr<Vessel> m_testVessel;
    std::unique_ptr<VertModel> m_lunaE3Model;
    
    CelestialBody* m_focusBody;
    glm::dvec3 m_cameraPosition;
    glm::dvec3 m_cameraOffset;  // Offset from focus body
    glm::dvec3 m_previousFocusPos;  // For smooth interpolation
    double m_followDistance;    // Distance to maintain from focus body
    double m_cameraDistance;
    double m_cameraYaw;
    double m_cameraPitch;
    double m_probeRotation;     // Rotation angle for LunaE3 probe
    
    void ProcessEvents();
    void ProcessInput(double deltaTime);
    void Update(double deltaTime);
    void UpdateCamera();
    void Render();
    void CycleFocusBody(int direction);
};

} // namespace Kosmos
