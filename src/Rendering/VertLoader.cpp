#include "VertLoader.h"
#include "../Core/Logger.h"
#include <fstream>
#include <filesystem>
#include <cstring>

namespace Kosmos {

std::unique_ptr<VertModel> VertLoader::Load(const std::string& filepath, const std::string& textureDir) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("VertLoader: Failed to open file: " + filepath);
        return nullptr;
    }
    
    auto model = std::make_unique<VertModel>();
    
    // Read number of meshes
    int32_t numMeshes = 0;
    file.read(reinterpret_cast<char*>(&numMeshes), sizeof(int32_t));
    
    LOG_INFO("VertLoader: Loading " + std::to_string(numMeshes) + " mesh groups from " + filepath);
    
    for (int32_t meshIndex = 0; meshIndex < numMeshes; ++meshIndex) {
        VertMeshGroup group;
        
        // Read mesh header
        int32_t numVertices = 0;
        int32_t numIndices = 0;
        int32_t numTris = 0;
        
        file.read(reinterpret_cast<char*>(&numVertices), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&numIndices), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&numTris), sizeof(int32_t));
        
        // Read material colors
        file.read(reinterpret_cast<char*>(&group.material.ambient), sizeof(float) * 4);
        file.read(reinterpret_cast<char*>(&group.material.diffuse), sizeof(float) * 4);
        file.read(reinterpret_cast<char*>(&group.material.specular), sizeof(float) * 4);
        file.read(reinterpret_cast<char*>(&group.material.emissive), sizeof(float) * 4);
        
        // Read texture filename lengths (8 texture slots)
        int32_t textureNameLengths[8] = {0};
        file.read(reinterpret_cast<char*>(&textureNameLengths), sizeof(int32_t) * 8);
        
        std::string ambientTextureName;
        std::string diffuseTextureName;
        
        // Read texture filenames
        for (int i = 0; i < 8; ++i) {
            if (textureNameLengths[i] > 0) {
                char textureNameBuffer[512] = {0};
                file.read(textureNameBuffer, textureNameLengths[i]);
                std::string textureName(textureNameBuffer);
                
                // Slot 0 = ambient, Slot 1 = diffuse (main texture)
                if (i == 0) {
                    ambientTextureName = textureName;
                } else if (i == 1) {
                    diffuseTextureName = textureName;
                }
            }
        }
        
        // Load textures
        auto tryLoadTexture = [&textureDir](const std::string& basename) -> std::shared_ptr<Texture> {
            if (basename.empty()) return nullptr;
            
            // Try different extensions
            std::vector<std::string> extensions = {".dds", ".bmp", ".png", ".jpg"};
            for (const auto& ext : extensions) {
                std::string fullPath = textureDir + "/" + basename + ext;
                if (std::filesystem::exists(fullPath)) {
                    auto tex = std::make_shared<Texture>();
                    if (tex->LoadFromFile(fullPath)) {
                        LOG_INFO("VertLoader: Loaded texture: " + fullPath);
                        return tex;
                    }
                }
            }
            return nullptr;
        };
        
        if (!ambientTextureName.empty()) {
            group.ambientTexture = tryLoadTexture(ambientTextureName);
        }
        
        if (!diffuseTextureName.empty()) {
            group.diffuseTexture = tryLoadTexture(diffuseTextureName);
        }
        
        // Read vertex data
        std::vector<Vertex> vertices;
        vertices.reserve(numIndices);
        
        for (int32_t i = 0; i < numIndices; i += 3) {
            // Read triangle (3 vertices)
            struct {
                float x, y, z;
            } positions[3], normals[3];
            
            struct {
                float u, v;
            } texcoords[3];
            
            for (int j = 0; j < 3; ++j) {
                file.read(reinterpret_cast<char*>(&positions[j]), sizeof(float) * 3);
                file.read(reinterpret_cast<char*>(&normals[j]), sizeof(float) * 3);
                file.read(reinterpret_cast<char*>(&texcoords[j]), sizeof(float) * 2);
            }
            
            // Add vertices
            for (int j = 0; j < 3; ++j) {
                Vertex v;
                v.position = glm::vec3(positions[j].x, positions[j].y, positions[j].z);
                v.normal = glm::vec3(normals[j].x, normals[j].y, normals[j].z);
                v.texCoord = glm::vec2(texcoords[j].u, texcoords[j].v);
                v.color = glm::vec3(group.material.diffuse.r, group.material.diffuse.g, group.material.diffuse.b);
                vertices.push_back(v);
            }
        }
        
        // Create indices (sequential since we read triangles directly)
        std::vector<unsigned int> indices;
        indices.reserve(vertices.size());
        for (size_t i = 0; i < vertices.size(); ++i) {
            indices.push_back(static_cast<unsigned int>(i));
        }
        
        // Create and upload mesh
        group.mesh = std::make_unique<Mesh>();
        group.mesh->SetVertices(vertices);
        group.mesh->SetIndices(indices);
        group.mesh->Upload();
        
        LOG_INFO("VertLoader: Loaded mesh group " + std::to_string(meshIndex) + 
                 " (" + std::to_string(vertices.size()) + " vertices, " + 
                 std::to_string(numTris) + " triangles)");
        
        model->groups.push_back(std::move(group));
    }
    
    file.close();
    LOG_INFO("VertLoader: Successfully loaded model with " + std::to_string(numMeshes) + " groups");
    
    return model;
}

} // namespace Kosmos
