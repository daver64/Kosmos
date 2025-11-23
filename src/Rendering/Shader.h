#pragma once

#include <string>
#include <glm/glm.hpp>

// Forward declare OpenGL types
typedef unsigned int GLuint;
typedef int GLint;

namespace Kosmos {

class Shader {
public:
    Shader();
    ~Shader();
    
    bool LoadFromFiles(const std::string& vertexPath, const std::string& fragmentPath);
    bool LoadFromSource(const std::string& vertexSource, const std::string& fragmentSource);
    
    void Use() const;
    void Unuse() const;
    
    GLuint GetProgramID() const { return m_programID; }
    
    // Uniform setters
    void SetBool(const std::string& name, bool value) const;
    void SetInt(const std::string& name, int value) const;
    void SetFloat(const std::string& name, float value) const;
    void SetVec3(const std::string& name, const glm::vec3& value) const;
    void SetVec4(const std::string& name, const glm::vec4& value) const;
    void SetMat4(const std::string& name, const glm::mat4& value) const;
    
private:
    GLuint m_programID;
    
    bool CompileShader(GLuint shader, const std::string& source);
    bool LinkProgram(GLuint vertexShader, GLuint fragmentShader);
    GLint GetUniformLocation(const std::string& name) const;
};

} // namespace Kosmos
