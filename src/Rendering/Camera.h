#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Kosmos {

class Camera {
public:
    Camera();
    ~Camera() = default;
    
    void SetPosition(const glm::dvec3& position);
    void SetTarget(const glm::dvec3& target);
    void SetUp(const glm::dvec3& up);
    
    void SetPerspective(float fov, float aspectRatio, float nearPlane, float farPlane);
    void SetOrthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane);
    
    glm::dvec3 GetPosition() const { return m_position; }
    glm::dvec3 GetTarget() const { return m_target; }
    glm::dvec3 GetUp() const { return m_up; }
    glm::dvec3 GetForward() const { return glm::normalize(m_target - m_position); }
    glm::dvec3 GetRight() const { return glm::normalize(glm::cross(GetForward(), m_up)); }
    
    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix() const { return m_projectionMatrix; }
    
    void MoveForward(double distance);
    void MoveRight(double distance);
    void MoveUp(double distance);
    
    void RotateAroundTarget(double deltaYaw, double deltaPitch);
    void Orbit(const glm::dvec3& target, double distance, double yaw, double pitch);
    
private:
    glm::dvec3 m_position;
    glm::dvec3 m_target;
    glm::dvec3 m_up;
    
    glm::mat4 m_projectionMatrix;
    
    float m_fov;
    float m_aspectRatio;
    float m_nearPlane;
    float m_farPlane;
};

} // namespace Kosmos
