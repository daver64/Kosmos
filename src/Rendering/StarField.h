#pragma once
#include <memory>
#include <GL/glew.h>

namespace Kosmos {

class Camera;
class Shader;
class Texture;

class StarField {
public:
    StarField();
    ~StarField();

    bool Initialize();
    void Render(Camera* camera);
    void Shutdown();

private:
    void CreateSkyboxCube();

    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<Texture> m_skyboxTexture;
    
    GLuint m_VAO;
    GLuint m_VBO;
    
    bool m_initialized;
};

} // namespace Kosmos
