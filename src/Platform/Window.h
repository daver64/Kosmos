#pragma once

#include <string>

// Forward declare SDL types
struct SDL_Window;
typedef void* SDL_GLContext;

namespace Kosmos {

struct WindowConfig {
    std::string title = "Kosmos";
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool vsync = true;
    int glMajorVersion = 4;
    int glMinorVersion = 1;
};

class Window {
public:
    Window();
    ~Window();
    
    bool Initialize(const WindowConfig& config);
    void Shutdown();
    
    void SwapBuffers();
    void ProcessEvents();
    
    bool ShouldClose() const { return m_shouldClose; }
    void SetShouldClose(bool close) { m_shouldClose = close; }
    
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    float GetAspectRatio() const { return static_cast<float>(m_width) / static_cast<float>(m_height); }
    
    SDL_Window* GetSDLWindow() { return m_window; }
    SDL_GLContext GetGLContext() { return m_glContext; }
    
private:
    SDL_Window* m_window;
    SDL_GLContext m_glContext;
    int m_width;
    int m_height;
    bool m_shouldClose;
};

} // namespace Kosmos
