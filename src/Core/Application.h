#pragma once

#include <string>
#include <memory>

namespace Kosmos {

class Window;
class Camera;
class Shader;
class Mesh;

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
    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<Mesh> m_testCube;
    
    float m_rotation;
    
    void ProcessEvents();
    void Update(double deltaTime);
    void Render();
};

} // namespace Kosmos
