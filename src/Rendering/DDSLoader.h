#pragma once
#include <GL/glew.h>
#include <string>
#include <cstdint>

namespace Kosmos {

struct DDSTextureData {
    GLuint textureID;
    int width;
    int height;
    int channels;
    bool compressed;
    GLenum format;
};

class DDSLoader {
public:
    // Load a DDS texture from file
    // Returns true on success, false on failure
    static bool Load(const std::string& path, DDSTextureData& outData);
    
private:
    // DDS file structures (based on Microsoft DDS format specification)
    #pragma pack(push, 1)
    struct DDS_PIXELFORMAT {
        uint32_t dwSize;
        uint32_t dwFlags;
        uint32_t dwFourCC;
        uint32_t dwRGBBitCount;
        uint32_t dwRBitMask;
        uint32_t dwGBitMask;
        uint32_t dwBBitMask;
        uint32_t dwABitMask;
    };
    
    struct DDS_HEADER {
        uint32_t dwSize;
        uint32_t dwFlags;
        uint32_t dwHeight;
        uint32_t dwWidth;
        uint32_t dwPitchOrLinearSize;
        uint32_t dwDepth;
        uint32_t dwMipMapCount;
        uint32_t dwReserved1[11];
        DDS_PIXELFORMAT ddspf;
        uint32_t dwCaps;
        uint32_t dwCaps2;
        uint32_t dwCaps3;
        uint32_t dwCaps4;
        uint32_t dwReserved2;
    };
    
    struct DDS_HEADER_DXT10 {
        uint32_t dxgiFormat;
        uint32_t resourceDimension;
        uint32_t miscFlag;
        uint32_t arraySize;
        uint32_t miscFlags2;
    };
    #pragma pack(pop)
    
    // DDS format constants
    static constexpr uint32_t DDS_MAGIC = 0x20534444; // "DDS "
    static constexpr uint32_t DDPF_FOURCC = 0x00000004;
    static constexpr uint32_t DDPF_RGB = 0x00000040;
    static constexpr uint32_t DDPF_RGBA = 0x00000041;
    
    // FourCC codes
    static constexpr uint32_t FOURCC_DXT1 = 0x31545844; // "DXT1"
    static constexpr uint32_t FOURCC_DXT3 = 0x33545844; // "DXT3"
    static constexpr uint32_t FOURCC_DXT5 = 0x35545844; // "DXT5"
    static constexpr uint32_t FOURCC_DX10 = 0x30315844; // "DX10"
    
    // Helper functions
    static uint32_t MakeFourCC(char ch0, char ch1, char ch2, char ch3);
    static GLenum GetCompressedFormat(uint32_t fourCC);
};

} // namespace Kosmos
