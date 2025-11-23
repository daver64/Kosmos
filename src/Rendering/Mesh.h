#pragma once

#include <vector>
#include <glm/glm.hpp>

// Forward declare OpenGL types
typedef unsigned int GLuint;

namespace Kosmos {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec3 color;
};

class Mesh {
public:
    Mesh();
    ~Mesh();
    
    void SetVertices(const std::vector<Vertex>& vertices);
    void SetIndices(const std::vector<unsigned int>& indices);
    
    void Upload();
    void Render() const;
    void Clear();
    
    static Mesh* CreateCube(float size = 1.0f);
    static Mesh* CreateSphere(float radius = 1.0f, int segments = 32, int rings = 16);
    static Mesh* CreatePlane(float width = 1.0f, float height = 1.0f);
    
private:
    std::vector<Vertex> m_vertices;
    std::vector<unsigned int> m_indices;
    
    GLuint m_vao;
    GLuint m_vbo;
    GLuint m_ebo;
    
    bool m_uploaded;
};

} // namespace Kosmos
