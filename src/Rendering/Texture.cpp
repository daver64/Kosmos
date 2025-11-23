#include "Texture.h"
#include "DDSLoader.h"
#include "../Core/Logger.h"
#include <GL/glew.h>

// stb_image for loading JPG/PNG
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Kosmos {

Texture::Texture()
    : m_textureID(0)
    , m_width(0)
    , m_height(0)
    , m_channels(0)
{
}

Texture::~Texture() {
    if (m_textureID) {
        glDeleteTextures(1, &m_textureID);
    }
}

bool Texture::LoadFromFile(const std::string& path) {
    m_path = path;
    
    // Check if this is a DDS file
    bool isDDS = (path.length() >= 4 && path.substr(path.length() - 4) == ".dds");
    
    if (isDDS) {
        return LoadDDS(path);
    }
    
    // Load image data using stb_image for standard formats (JPG, PNG, etc.)
    stbi_set_flip_vertically_on_load(1);  // OpenGL expects texture origin at bottom-left
    unsigned char* data = stbi_load(path.c_str(), &m_width, &m_height, &m_channels, 0);
    
    if (!data) {
        LOG_ERROR("Failed to load texture: " + path);
        return false;
    }
    
    // Determine format
    GLenum format = GL_RGB;
    if (m_channels == 1)
        format = GL_RED;
    else if (m_channels == 3)
        format = GL_RGB;
    else if (m_channels == 4)
        format = GL_RGBA;
    
    // Generate and bind texture
    glGenTextures(1, &m_textureID);
    glBindTexture(GL_TEXTURE_2D, m_textureID);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Upload texture data
    glTexImage2D(GL_TEXTURE_2D, 0, format, m_width, m_height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    
    // Free image data
    stbi_image_free(data);
    
    LOG_INFO("Loaded texture: " + path + " (" + std::to_string(m_width) + "x" + std::to_string(m_height) + ", " + std::to_string(m_channels) + " channels)");
    
    return true;
}

bool Texture::LoadDDS(const std::string& path) {
    DDSTextureData ddsData;
    if (!DDSLoader::Load(path, ddsData)) {
        return false;
    }
    
    m_textureID = ddsData.textureID;
    m_width = ddsData.width;
    m_height = ddsData.height;
    m_channels = ddsData.channels;
    
    return true;
}

void Texture::Bind(unsigned int slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_textureID);
}

void Texture::Unbind() const {
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace Kosmos