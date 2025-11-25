#pragma once

#include "Mesh.h"
#include "Texture.h"
#include <memory>
#include <vector>
#include <string>
#include <glm/glm.hpp>

namespace Kosmos {

// Material definition from .vert file
struct VertMaterial {
    glm::vec4 ambient;
    glm::vec4 diffuse;
    glm::vec4 specular;
    glm::vec4 emissive;
};

// Represents a mesh group with its own material and geometry
struct VertMeshGroup {
    std::unique_ptr<Mesh> mesh;
    VertMaterial material;
    std::shared_ptr<Texture> diffuseTexture;  // Main texture
    std::shared_ptr<Texture> ambientTexture;  // Optional ambient texture
};

// Complete model with multiple groups
struct VertModel {
    std::vector<VertMeshGroup> groups;
    
    void Clear() {
        groups.clear();
    }
};

class VertLoader {
public:
    // Load a .vert format mesh file
    // Returns nullptr on failure
    static std::unique_ptr<VertModel> Load(const std::string& filepath, const std::string& textureDir);
};

} // namespace Kosmos
