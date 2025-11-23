#include "DDSLoader.h"
#include "../Core/Logger.h"
#include <fstream>
#include <vector>

namespace Kosmos {

uint32_t DDSLoader::MakeFourCC(char ch0, char ch1, char ch2, char ch3) {
    return ((uint32_t)(uint8_t)(ch0)) | ((uint32_t)(uint8_t)(ch1) << 8) |
           ((uint32_t)(uint8_t)(ch2) << 16) | ((uint32_t)(uint8_t)(ch3) << 24);
}

GLenum DDSLoader::GetCompressedFormat(uint32_t fourCC) {
    switch (fourCC) {
        case FOURCC_DXT1:
            return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
        case FOURCC_DXT3:
            return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
        case FOURCC_DXT5:
            return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        default:
            return 0;
    }
}

bool DDSLoader::Load(const std::string& path, DDSTextureData& outData) {
    // Open file
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open DDS file: " + path);
        return false;
    }
    
    // Read magic number
    uint32_t magic;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != DDS_MAGIC) {
        LOG_ERROR("Invalid DDS file (bad magic number): " + path);
        return false;
    }
    
    // Read header
    DDS_HEADER header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (header.dwSize != 124) {
        LOG_ERROR("Invalid DDS header size: " + path);
        return false;
    }
    
    outData.width = header.dwWidth;
    outData.height = header.dwHeight;
    uint32_t mipMapCount = (header.dwMipMapCount > 0) ? header.dwMipMapCount : 1;
    
    // Check for DX10 extension
    bool isDX10 = false;
    DDS_HEADER_DXT10 header10;
    if (header.ddspf.dwFlags & DDPF_FOURCC && header.ddspf.dwFourCC == FOURCC_DX10) {
        isDX10 = true;
        file.read(reinterpret_cast<char*>(&header10), sizeof(header10));
    }
    
    // Determine format
    bool compressed = false;
    GLenum format = 0;
    GLenum internalFormat = 0;
    uint32_t blockSize = 0;
    
    if (header.ddspf.dwFlags & DDPF_FOURCC) {
        compressed = true;
        format = GetCompressedFormat(header.ddspf.dwFourCC);
        
        if (format == 0) {
            if (isDX10) {
                // Handle DX10 formats
                // For now, we'll use a basic RGB/RGBA fallback
                LOG_WARNING("DX10 format not fully supported, attempting basic decoding: " + path);
                compressed = false;
                format = GL_RGBA;
                internalFormat = GL_RGBA;
                outData.channels = 4;
            } else {
                LOG_ERROR("Unsupported DDS format: " + path);
                return false;
            }
        } else {
            internalFormat = format;
            // Compressed formats
            if (header.ddspf.dwFourCC == FOURCC_DXT1) {
                blockSize = 8;
                outData.channels = 3;
            } else {
                blockSize = 16;
                outData.channels = 4;
            }
        }
    } else if (header.ddspf.dwFlags & DDPF_RGB) {
        // Uncompressed RGB/RGBA
        compressed = false;
        if (header.ddspf.dwFlags & DDPF_RGBA || header.ddspf.dwRGBBitCount == 32) {
            format = GL_BGRA;
            internalFormat = GL_RGBA;
            outData.channels = 4;
        } else {
            format = GL_BGR;
            internalFormat = GL_RGB;
            outData.channels = 3;
        }
    } else {
        LOG_ERROR("Unsupported DDS pixel format: " + path);
        return false;
    }
    
    outData.compressed = compressed;
    outData.format = format;
    
    // Generate texture
    glGenTextures(1, &outData.textureID);
    glBindTexture(GL_TEXTURE_2D, outData.textureID);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipMapCount > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Load texture data
    uint32_t width = outData.width;
    uint32_t height = outData.height;
    
    for (uint32_t mip = 0; mip < mipMapCount; ++mip) {
        uint32_t size;
        
        if (compressed) {
            size = ((width + 3) / 4) * ((height + 3) / 4) * blockSize;
        } else {
            size = width * height * outData.channels;
        }
        
        // Read mipmap data
        std::vector<char> buffer(size);
        file.read(buffer.data(), size);
        
        if (file.gcount() != static_cast<std::streamsize>(size)) {
            LOG_ERROR("Failed to read DDS mipmap data: " + path);
            glDeleteTextures(1, &outData.textureID);
            return false;
        }
        
        // Upload to GPU
        if (compressed) {
            glCompressedTexImage2D(GL_TEXTURE_2D, mip, internalFormat, width, height, 0, size, buffer.data());
        } else {
            glTexImage2D(GL_TEXTURE_2D, mip, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, buffer.data());
        }
        
        // Calculate next mipmap dimensions
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    
    // If no mipmaps were provided, generate them
    if (mipMapCount == 1 && !compressed) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    
    LOG_INFO("Loaded DDS texture: " + path + " (" + std::to_string(outData.width) + "x" + 
             std::to_string(outData.height) + ", " + (compressed ? "compressed" : "uncompressed") + 
             ", " + std::to_string(mipMapCount) + " mipmaps)");
    
    return true;
}

} // namespace Kosmos
