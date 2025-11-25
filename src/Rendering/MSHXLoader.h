#pragma once

#include "Mesh.h"
#include "Texture.h"
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>

namespace Kosmos {

// Material definition from MSHX1 file
struct Material {
    std::string name;
    glm::vec4 diffuse;
    glm::vec4 ambient;
    glm::vec4 specular;
    float specularPower;
    glm::vec4 emissive;
};

// Represents a mesh group with its own material and geometry
struct MeshGroup {
    std::unique_ptr<Mesh> mesh;
    int materialIndex;
    int textureIndex;
    bool hasUVCoords;  // True if vertices include UV coordinates
    std::string name;
};

// Complete model with multiple groups
struct MSHXModel {
    std::vector<MeshGroup> groups;
    std::vector<std::shared_ptr<Texture>> textures;  // Texture pool
    std::vector<Material> materials;  // Material definitions
    std::vector<std::string> textureNames;  // Texture filenames from TEXTURES section
    
    void Clear() {
        groups.clear();
        textures.clear();
        materials.clear();
        textureNames.clear();
    }
};

class MSHXLoader {
public:
    // Load an MSHX1 format mesh file
    // Returns nullptr on failure
    static std::unique_ptr<MSHXModel> Load(const std::string& filepath);
    
    // Load textures for a model from a base directory
    // Looks for texture files like probe02.dds, probe03.dds, etc.
    static bool LoadTextures(MSHXModel* model, const std::string& textureDir);
    
private:
    // Parse functions
    static bool ParseHeader(std::ifstream& file);
    static int ParseGroupCount(std::ifstream& file);
    static bool ParseMaterialIndex(std::ifstream& file, int& outIndex);
    static bool ParseTextureIndex(std::ifstream& file, int& outIndex);
    static bool ParseGeometry(std::ifstream& file, Mesh* mesh, std::string& outName, bool readUVCoords, bool& outHasUVCoords);
    
    // Helper to skip whitespace and comments
    static void SkipWhitespaceAndComments(std::ifstream& file);
};

} // namespace Kosmos
