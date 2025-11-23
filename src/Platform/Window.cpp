#include "Window.h"
#include "../Core/Logger.h"
#include <iostream>
#include <cstring>
#include <GL/glew.h>
#include <SDL2/SDL.h>

namespace Kosmos {

Window::Window()
    : m_window(nullptr)
    , m_glContext(nullptr)
    , m_width(1280)
    , m_height(720)
    , m_shouldClose(false)
    , m_keyboardState(nullptr)
{
    std::memset(m_previousKeyState, 0, sizeof(m_previousKeyState));
}

Window::~Window() {
    Shutdown();
}

bool Window::Initialize(const WindowConfig& config) {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        LOG_ERROR("Failed to initialize SDL: " + std::string(SDL_GetError()));
        return false;
    }
    
    // Set OpenGL attributes
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, config.glMajorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, config.glMinorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    
    // Create window
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    if (config.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    
    m_window = SDL_CreateWindow(
        config.title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        config.width,
        config.height,
        flags
    );
    
    if (!m_window) {
        LOG_ERROR("Failed to create window: " + std::string(SDL_GetError()));
        SDL_Quit();
        return false;
    }
    
    m_width = config.width;
    m_height = config.height;
    
    // Create OpenGL context
    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
        LOG_ERROR("Failed to create OpenGL context: " + std::string(SDL_GetError()));
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        return false;
    }
    
    // Initialize GLEW
    glewExperimental = GL_TRUE;
    GLenum glewError = glewInit();
    if (glewError != GLEW_OK) {
        LOG_ERROR("Failed to initialize GLEW: " + std::string((const char*)glewGetErrorString(glewError)));
        SDL_GL_DeleteContext(m_glContext);
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        return false;
    }
    
    // Set VSync
    SDL_GL_SetSwapInterval(config.vsync ? 1 : 0);
    
    // OpenGL settings
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    
    LOG_INFO("Window initialized: " + std::to_string(m_width) + "x" + std::to_string(m_height));
    LOG_INFO("OpenGL Version: " + std::string((const char*)glGetString(GL_VERSION)));
    LOG_INFO("GLSL Version: " + std::string((const char*)glGetString(GL_SHADING_LANGUAGE_VERSION)));
    
    // Get keyboard state
    m_keyboardState = SDL_GetKeyboardState(nullptr);
    
    return true;
}

void Window::Shutdown() {
    if (m_glContext) {
        SDL_GL_DeleteContext(m_glContext);
        m_glContext = nullptr;
    }
    
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    
    SDL_Quit();
}

void Window::SwapBuffers() {
    SDL_GL_SwapWindow(m_window);
}

void Window::ProcessEvents() {
    UpdateKeyboardState();
    
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                m_shouldClose = true;
                break;
                
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    m_shouldClose = true;
                }
                break;
                
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    m_width = event.window.data1;
                    m_height = event.window.data2;
                    glViewport(0, 0, m_width, m_height);
                }
                break;
        }
    }
}

void Window::UpdateKeyboardState() {
    // Save previous state before SDL updates
    int numKeys;
    const Uint8* currentState = SDL_GetKeyboardState(&numKeys);
    if (currentState) {
        for (int i = 0; i < 512 && i < numKeys; ++i) {
            m_previousKeyState[i] = currentState[i];
        }
    }
}

bool Window::IsKeyPressed(int scancode) const {
    // Key was just pressed this frame (not held from previous frame)
    if (scancode < 0 || scancode >= 512) return false;
    int numKeys;
    const Uint8* currentState = SDL_GetKeyboardState(&numKeys);
    return currentState && scancode < numKeys && currentState[scancode] && !m_previousKeyState[scancode];
}

bool Window::IsKeyHeld(int scancode) const {
    // Key is currently held down
    if (scancode < 0 || scancode >= 512) return false;
    int numKeys;
    const Uint8* currentState = SDL_GetKeyboardState(&numKeys);
    return currentState && scancode < numKeys && currentState[scancode];
}

} // namespace Kosmos
