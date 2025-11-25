#include "MSHXLoader.h"
#include "../Core/Logger.h"
#include <fstream>
#include <sstream>
#include <filesystem>

namespace Kosmos {

std::unique_ptr<MSHXModel> MSHXLoader::Load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR("MSHXLoader: Failed to open file: " + filepath);
        return nullptr;
    }
    
    // Parse header
    if (!ParseHeader(file)) {
        LOG_ERROR("MSHXLoader: Invalid MSHX1 header in " + filepath);
        return nullptr;
    }
    
    // Parse group count
    int groupCount = ParseGroupCount(file);
    if (groupCount <= 0) {
        LOG_ERROR("MSHXLoader: Invalid group count in " + filepath);
        return nullptr;
    }
    
    LOG_INFO("MSHXLoader: Loading " + std::to_string(groupCount) + " mesh groups from " + filepath);
    
    auto model = std::make_unique<MSHXModel>();
    model->groups.reserve(groupCount);
    
    // Parse each group
    for (int i = 0; i < groupCount; ++i) {
        MeshGroup group;
        
        // Parse material index
        if (!ParseMaterialIndex(file, group.materialIndex)) {
            LOG_ERROR("MSHXLoader: Failed to parse material index for group " + std::to_string(i));
            return nullptr;
        }
        
        // Parse texture index
        if (!ParseTextureIndex(file, group.textureIndex)) {
            LOG_ERROR("MSHXLoader: Failed to parse texture index for group " + std::to_string(i));
            return nullptr;
        }
        
        // Parse geometry
        group.mesh = std::make_unique<Mesh>();
        bool readUVCoords = (group.textureIndex >= 0);  // Try to read UVs if texture is assigned
        if (!ParseGeometry(file, group.mesh.get(), group.name, readUVCoords, group.hasUVCoords)) {
            LOG_ERROR("MSHXLoader: Failed to parse geometry for group " + std::to_string(i));
            return nullptr;
        }
        
        // Upload mesh to GPU
        group.mesh->Upload();
        
        LOG_INFO("MSHXLoader: Loaded group " + std::to_string(i) + " '" + group.name + 
                 "' (material=" + std::to_string(group.materialIndex) + 
                 ", texture=" + std::to_string(group.textureIndex) + ")");
        
        model->groups.push_back(std::move(group));
    }
    
    // Parse MATERIALS section (optional, at end of file)
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;
        
        if (keyword == "MATERIALS") {
            int matCount;
            iss >> matCount;
            LOG_INFO("MSHXLoader: Found " + std::to_string(matCount) + " material definitions");
            
            // Read material names
            std::vector<std::string> matNames;
            for (int i = 0; i < matCount; ++i) {
                if (std::getline(file, line)) {
                    matNames.push_back(line);
                }
            }
            
            // Parse each material definition
            for (int i = 0; i < matCount; ++i) {
                Material mat;
                if (std::getline(file, line)) {
                    std::istringstream nameStream(line);
                    std::string matKeyword;
                    nameStream >> matKeyword;
                    std::getline(nameStream, mat.name);
                    // Trim leading whitespace
                    size_t start = mat.name.find_first_not_of(" \t");
                    if (start != std::string::npos) {
                        mat.name = mat.name.substr(start);
                    }
                }
                
                // Read diffuse
                if (std::getline(file, line)) {
                    std::istringstream stream(line);
                    stream >> mat.diffuse.r >> mat.diffuse.g >> mat.diffuse.b >> mat.diffuse.a;
                }
                
                // Read ambient
                if (std::getline(file, line)) {
                    std::istringstream stream(line);
                    stream >> mat.ambient.r >> mat.ambient.g >> mat.ambient.b >> mat.ambient.a;
                }
                
                // Read specular + power
                if (std::getline(file, line)) {
                    std::istringstream stream(line);
                    stream >> mat.specular.r >> mat.specular.g >> mat.specular.b >> mat.specular.a >> mat.specularPower;
                }
                
                // Read emissive
                if (std::getline(file, line)) {
                    std::istringstream stream(line);
                    stream >> mat.emissive.r >> mat.emissive.g >> mat.emissive.b >> mat.emissive.a;
                }
                
                model->materials.push_back(mat);
                LOG_INFO("MSHXLoader: Material " + std::to_string(i) + " '" + mat.name + "' diffuse=(" +
                         std::to_string(mat.diffuse.r) + "," + std::to_string(mat.diffuse.g) + "," + std::to_string(mat.diffuse.b) + ")");
            }
        }
        else if (keyword == "TEXTURES") {
            int texCount;
            iss >> texCount;
            LOG_INFO("MSHXLoader: Found " + std::to_string(texCount) + " texture names");
            
            for (int i = 0; i < texCount; ++i) {
                if (std::getline(file, line)) {
                    // Trim whitespace
                    size_t start = line.find_first_not_of(" \t");
                    size_t end = line.find_last_not_of(" \t\r\n");
                    if (start != std::string::npos && end != std::string::npos) {
                        std::string texName = line.substr(start, end - start + 1);
                        model->textureNames.push_back(texName);
                        LOG_INFO("MSHXLoader: Texture " + std::to_string(i) + ": " + texName);
                    }
                }
            }
        }
    }
    
    LOG_INFO("MSHXLoader: Successfully loaded model with " + std::to_string(model->groups.size()) + " groups");
    return model;
}

bool MSHXLoader::LoadTextures(MSHXModel* model, const std::string& textureDir) {
    if (!model) return false;
    
    // If we have texture names from the TEXTURES section, use those
    if (!model->textureNames.empty()) {
        model->textures.resize(model->textureNames.size());
        
        for (size_t i = 0; i < model->textureNames.size(); ++i) {
            std::string texPath = textureDir + "/" + model->textureNames[i];
            auto texture = std::make_shared<Texture>();
            if (texture->LoadFromFile(texPath)) {
                model->textures[i] = texture;
                LOG_INFO("MSHXLoader: Loaded texture " + std::to_string(i) + " from " + model->textureNames[i]);
            } else {
                LOG_WARNING("MSHXLoader: Failed to load texture " + model->textureNames[i]);
            }
        }
        return true;
    }
    
    // Fallback: Find highest texture index referenced
    int maxTextureIndex = -1;
    for (const auto& group : model->groups) {
        if (group.textureIndex > maxTextureIndex) {
            maxTextureIndex = group.textureIndex;
        }
    }
    
    if (maxTextureIndex < 0) {
        LOG_INFO("MSHXLoader: No textures referenced in model");
        return true;
    }
    
    // Allocate texture pool
    model->textures.resize(maxTextureIndex + 1);
    
    // Load textures - try common naming patterns
    namespace fs = std::filesystem;
    
    for (const auto& entry : fs::directory_iterator(textureDir)) {
        if (!entry.is_regular_file()) continue;
        
        std::string filename = entry.path().filename().string();
        std::string ext = entry.path().extension().string();
        
        // Only load DDS files for now
        if (ext != ".dds" && ext != ".DDS") continue;
        
        // Try to extract texture index from filename
        // Common patterns: probe02.dds, probe03.dds, texture_01.dds, etc.
        std::string stem = entry.path().stem().string();
        
        // Look for trailing digits
        int textureIndex = -1;
        size_t digitStart = stem.find_last_not_of("0123456789");
        if (digitStart != std::string::npos && digitStart < stem.length() - 1) {
            std::string numberPart = stem.substr(digitStart + 1);
            try {
                textureIndex = std::stoi(numberPart);
            } catch (...) {
                // Not a valid number
                continue;
            }
        }
        
        if (textureIndex >= 0 && textureIndex < (int)model->textures.size()) {
            auto texture = std::make_shared<Texture>();
            if (texture->LoadFromFile(entry.path().string())) {
                model->textures[textureIndex] = texture;
                LOG_INFO("MSHXLoader: Loaded texture " + std::to_string(textureIndex) + 
                         " from " + filename);
            } else {
                LOG_WARNING("MSHXLoader: Failed to load texture from " + filename);
            }
        }
    }
    
    return true;
}

bool MSHXLoader::ParseHeader(std::ifstream& file) {
    std::string line;
    if (!std::getline(file, line)) {
        return false;
    }
    
    // Trim whitespace
    size_t start = line.find_first_not_of(" \t\r\n");
    if (start != std::string::npos) {
        line = line.substr(start);
    }
    
    return (line.substr(0, 5) == "MSHX1");
}

int MSHXLoader::ParseGroupCount(std::ifstream& file) {
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;
        
        if (keyword == "GROUPS") {
            int count;
            iss >> count;
            return count;
        }
    }
    return -1;
}

bool MSHXLoader::ParseMaterialIndex(std::ifstream& file, int& outIndex) {
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;
        
        if (keyword == "MATERIAL") {
            iss >> outIndex;
            return true;
        }
    }
    return false;
}

bool MSHXLoader::ParseTextureIndex(std::ifstream& file, int& outIndex) {
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;
        
        if (keyword == "TEXTURE") {
            iss >> outIndex;
            return true;
        }
    }
    return false;
}

bool MSHXLoader::ParseGeometry(std::ifstream& file, Mesh* mesh, std::string& outName, bool readUVCoords, bool& outHasUVCoords) {
    std::string line;
    int vertexCount = 0;
    int faceCount = 0;
    
    outHasUVCoords = false;  // Will be set to true if UV coords are found
    
    // Find GEOM line
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;
        
        if (keyword == "GEOM") {
            iss >> vertexCount >> faceCount;
            
            // Extract name from comment (after semicolon)
            size_t commentPos = line.find(';');
            if (commentPos != std::string::npos && commentPos + 1 < line.length()) {
                outName = line.substr(commentPos + 1);
                // Trim whitespace
                size_t start = outName.find_first_not_of(" \t\r\n");
                if (start != std::string::npos) {
                    outName = outName.substr(start);
                }
                size_t end = outName.find_last_not_of(" \t\r\n");
                if (end != std::string::npos) {
                    outName = outName.substr(0, end + 1);
                }
            } else {
                outName = "Unnamed";
            }
            
            break;
        }
    }
    
    if (vertexCount <= 0 || faceCount <= 0) {
        return false;
    }
    
    // Read vertices
    std::vector<Vertex> vertices;
    vertices.reserve(vertexCount);
    
    for (int i = 0; i < vertexCount; ++i) {
        if (!std::getline(file, line)) {
            LOG_ERROR("MSHXLoader: Unexpected end of file while reading vertices");
            return false;
        }
        
        std::istringstream iss(line);
        Vertex v;
        v.color = glm::vec3(1.0f, 1.0f, 1.0f);  // Default white
        
        // Read position and normal (always present)
        iss >> v.position.x >> v.position.y >> v.position.z;
        iss >> v.normal.x >> v.normal.y >> v.normal.z;
        
        // Try to read UV coordinates if requested
        if (readUVCoords) {
            float u, vCoord;
            if (iss >> u >> vCoord) {
                // Read UV coordinates directly without transformation
                v.texCoord.x = u;
                v.texCoord.y = vCoord;
                if (i == 0) {
                    outHasUVCoords = true;  // First vertex has UVs, assume all do
                }
            } else {
                // No UV coordinates in data
                v.texCoord.x = 0.0f;
                v.texCoord.y = 0.0f;
            }
        } else {
            v.texCoord.x = 0.0f;
            v.texCoord.y = 0.0f;
        }
        
        vertices.push_back(v);
    }
    
    // Read faces (triangles)
    std::vector<unsigned int> indices;
    indices.reserve(faceCount * 3);
    
    for (int i = 0; i < faceCount; ++i) {
        if (!std::getline(file, line)) {
            LOG_ERROR("MSHXLoader: Unexpected end of file while reading faces");
            return false;
        }
        
        std::istringstream iss(line);
        unsigned int i0, i1, i2;
        iss >> i0 >> i1 >> i2;
        
        // Validate indices
        if (i0 >= (unsigned int)vertexCount || 
            i1 >= (unsigned int)vertexCount || 
            i2 >= (unsigned int)vertexCount) {
            LOG_ERROR("MSHXLoader: Invalid face index in geometry");
            return false;
        }
        
        indices.push_back(i0);
        indices.push_back(i1);
        indices.push_back(i2);
    }
    
    mesh->SetVertices(vertices);
    mesh->SetIndices(indices);
    
    return true;
}

} // namespace Kosmos
