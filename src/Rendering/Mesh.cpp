#include "Mesh.h"
#include <GL/glew.h>
#include <cmath>

namespace Kosmos {

Mesh::Mesh()
    : m_vao(0)
    , m_vbo(0)
    , m_ebo(0)
    , m_uploaded(false)
{
}

Mesh::~Mesh() {
    Clear();
}

void Mesh::SetVertices(const std::vector<Vertex>& vertices) {
    m_vertices = vertices;
    m_uploaded = false;
}

void Mesh::SetIndices(const std::vector<unsigned int>& indices) {
    m_indices = indices;
    m_uploaded = false;
}

void Mesh::Upload() {
    if (m_vertices.empty()) {
        return;
    }
    
    // Generate buffers
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    
    glBindVertexArray(m_vao);
    
    // Upload vertex data
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(Vertex), m_vertices.data(), GL_STATIC_DRAW);
    
    // Position attribute
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    
    // Normal attribute
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    
    // TexCoord attribute
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord));
    
    // Color attribute
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
    
    // Upload indices if present
    if (!m_indices.empty()) {
        glGenBuffers(1, &m_ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(unsigned int), m_indices.data(), GL_STATIC_DRAW);
    }
    
    glBindVertexArray(0);
    m_uploaded = true;
}

void Mesh::Render() const {
    if (!m_uploaded || m_vao == 0) {
        return;
    }
    
    glBindVertexArray(m_vao);
    
    if (!m_indices.empty()) {
        glDrawElements(GL_TRIANGLES, m_indices.size(), GL_UNSIGNED_INT, 0);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, m_vertices.size());
    }
    
    glBindVertexArray(0);
}

void Mesh::Clear() {
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_ebo != 0) {
        glDeleteBuffers(1, &m_ebo);
        m_ebo = 0;
    }
    m_uploaded = false;
}

Mesh* Mesh::CreateCube(float size) {
    Mesh* mesh = new Mesh();
    float half = size * 0.5f;
    
    std::vector<Vertex> vertices = {
        // Front face
        {{-half, -half,  half}, {0, 0, 1}, {0, 0}, {1, 1, 1}},
        {{ half, -half,  half}, {0, 0, 1}, {1, 0}, {1, 1, 1}},
        {{ half,  half,  half}, {0, 0, 1}, {1, 1}, {1, 1, 1}},
        {{-half,  half,  half}, {0, 0, 1}, {0, 1}, {1, 1, 1}},
        
        // Back face
        {{ half, -half, -half}, {0, 0, -1}, {0, 0}, {1, 1, 1}},
        {{-half, -half, -half}, {0, 0, -1}, {1, 0}, {1, 1, 1}},
        {{-half,  half, -half}, {0, 0, -1}, {1, 1}, {1, 1, 1}},
        {{ half,  half, -half}, {0, 0, -1}, {0, 1}, {1, 1, 1}},
        
        // Right face
        {{ half, -half,  half}, {1, 0, 0}, {0, 0}, {1, 1, 1}},
        {{ half, -half, -half}, {1, 0, 0}, {1, 0}, {1, 1, 1}},
        {{ half,  half, -half}, {1, 0, 0}, {1, 1}, {1, 1, 1}},
        {{ half,  half,  half}, {1, 0, 0}, {0, 1}, {1, 1, 1}},
        
        // Left face
        {{-half, -half, -half}, {-1, 0, 0}, {0, 0}, {1, 1, 1}},
        {{-half, -half,  half}, {-1, 0, 0}, {1, 0}, {1, 1, 1}},
        {{-half,  half,  half}, {-1, 0, 0}, {1, 1}, {1, 1, 1}},
        {{-half,  half, -half}, {-1, 0, 0}, {0, 1}, {1, 1, 1}},
        
        // Top face
        {{-half,  half,  half}, {0, 1, 0}, {0, 0}, {1, 1, 1}},
        {{ half,  half,  half}, {0, 1, 0}, {1, 0}, {1, 1, 1}},
        {{ half,  half, -half}, {0, 1, 0}, {1, 1}, {1, 1, 1}},
        {{-half,  half, -half}, {0, 1, 0}, {0, 1}, {1, 1, 1}},
        
        // Bottom face
        {{-half, -half, -half}, {0, -1, 0}, {0, 0}, {1, 1, 1}},
        {{ half, -half, -half}, {0, -1, 0}, {1, 0}, {1, 1, 1}},
        {{ half, -half,  half}, {0, -1, 0}, {1, 1}, {1, 1, 1}},
        {{-half, -half,  half}, {0, -1, 0}, {0, 1}, {1, 1, 1}},
    };
    
    std::vector<unsigned int> indices = {
        0, 1, 2,  2, 3, 0,    // Front
        4, 5, 6,  6, 7, 4,    // Back
        8, 9, 10, 10, 11, 8,  // Right
        12, 13, 14, 14, 15, 12, // Left
        16, 17, 18, 18, 19, 16, // Top
        20, 21, 22, 22, 23, 20  // Bottom
    };
    
    mesh->SetVertices(vertices);
    mesh->SetIndices(indices);
    mesh->Upload();
    
    return mesh;
}

Mesh* Mesh::CreateSphere(float radius, int segments, int rings) {
    Mesh* mesh = new Mesh();
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    
    // Generate vertices
    for (int ring = 0; ring <= rings; ++ring) {
        float phi = M_PI * ring / rings;
        for (int segment = 0; segment <= segments; ++segment) {
            float theta = 2.0f * M_PI * segment / segments;
            
            float x = radius * sin(phi) * cos(theta);
            float y = radius * cos(phi);
            float z = radius * sin(phi) * sin(theta);
            
            glm::vec3 position(x, y, z);
            glm::vec3 normal = glm::normalize(position);
            glm::vec2 texCoord(
                (float)segment / segments,
                (float)ring / rings
            );
            
            vertices.push_back({position, normal, texCoord, {1, 1, 1}});
        }
    }
    
    // Generate indices
    for (int ring = 0; ring < rings; ++ring) {
        for (int segment = 0; segment < segments; ++segment) {
            int current = ring * (segments + 1) + segment;
            int next = current + segments + 1;
            
            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);
            
            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }
    
    mesh->SetVertices(vertices);
    mesh->SetIndices(indices);
    mesh->Upload();
    
    return mesh;
}

Mesh* Mesh::CreatePlane(float width, float height) {
    Mesh* mesh = new Mesh();
    float halfW = width * 0.5f;
    float halfH = height * 0.5f;
    
    std::vector<Vertex> vertices = {
        {{-halfW, 0, -halfH}, {0, 1, 0}, {0, 0}, {1, 1, 1}},
        {{ halfW, 0, -halfH}, {0, 1, 0}, {1, 0}, {1, 1, 1}},
        {{ halfW, 0,  halfH}, {0, 1, 0}, {1, 1}, {1, 1, 1}},
        {{-halfW, 0,  halfH}, {0, 1, 0}, {0, 1}, {1, 1, 1}},
    };
    
    std::vector<unsigned int> indices = {0, 1, 2, 2, 3, 0};
    
    mesh->SetVertices(vertices);
    mesh->SetIndices(indices);
    mesh->Upload();
    
    return mesh;
}

} // namespace Kosmos