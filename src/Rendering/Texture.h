#pragma once
#include <string>
#include <GL/glew.h>

namespace Kosmos {

class Texture {
public:
    Texture();
    ~Texture();

    // Load texture from file (supports JPG, PNG, DDS)
    bool LoadFromFile(const std::string& path);
    
    // Bind texture for rendering
    void Bind(unsigned int slot = 0) const;
    void Unbind() const;
    
    // Getters
    GLuint GetID() const { return m_textureID; }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    int GetChannels() const { return m_channels; }
    
private:
    bool LoadDDS(const std::string& path);
    
    GLuint m_textureID;
    int m_width;
    int m_height;
    int m_channels;
    std::string m_path;
};

} // namespace Kosmos
