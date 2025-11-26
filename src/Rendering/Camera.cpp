#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Kosmos {

Camera::Camera()
    : m_position(0.0, 0.0, 10.0)
    , m_target(0.0, 0.0, 0.0)
    , m_up(0.0, 1.0, 0.0)
    , m_projectionMatrix(1.0f)
    , m_fov(45.0f)
    , m_aspectRatio(16.0f / 9.0f)
    , m_nearPlane(0.1f)
    , m_farPlane(1.0e13f)  // 10 trillion meters = ~67 AU for solar system rendering
{
    SetPerspective(m_fov, m_aspectRatio, m_nearPlane, m_farPlane);
}

void Camera::SetPosition(const glm::dvec3& position) {
    m_position = position;
}

void Camera::SetTarget(const glm::dvec3& target) {
    m_target = target;
}

void Camera::SetUp(const glm::dvec3& up) {
    m_up = up;
}

void Camera::SetPerspective(float fov, float aspectRatio, float nearPlane, float farPlane) {
    m_fov = fov;
    m_aspectRatio = aspectRatio;
    m_nearPlane = nearPlane;
    m_farPlane = farPlane;
    m_projectionMatrix = glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
}

void Camera::SetOrthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane) {
    m_nearPlane = nearPlane;
    m_farPlane = farPlane;
    m_projectionMatrix = glm::ortho(left, right, bottom, top, nearPlane, farPlane);
}

glm::mat4 Camera::GetViewMatrix() const {
    // For camera-relative rendering at astronomical scales,
    // build a rotation-only view matrix to avoid precision loss
    glm::dvec3 forward = glm::normalize(m_target - m_position);
    glm::dvec3 right = glm::normalize(glm::cross(forward, m_up));
    glm::dvec3 up = glm::cross(right, forward);
    
    // Build view matrix with only rotation (no translation)
    // Camera is always at origin in view space for camera-relative rendering
    // The view matrix rotates world coordinates into camera space
    // In camera space: right=+X, up=+Y, -forward=+Z (looking down -Z)
    glm::mat4 view = glm::mat4(1.0f);
    view[0] = glm::vec4(right.x, up.x, -forward.x, 0.0f);
    view[1] = glm::vec4(right.y, up.y, -forward.y, 0.0f);
    view[2] = glm::vec4(right.z, up.z, -forward.z, 0.0f);
    view[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    
    return view;
}

void Camera::MoveForward(double distance) {
    glm::dvec3 forward = GetForward();
    m_position += forward * distance;
    m_target += forward * distance;
}

void Camera::MoveRight(double distance) {
    glm::dvec3 right = GetRight();
    m_position += right * distance;
    m_target += right * distance;
}

void Camera::MoveUp(double distance) {
    m_position += m_up * distance;
    m_target += m_up * distance;
}

void Camera::RotateAroundTarget(double deltaYaw, double deltaPitch) {
    glm::dvec3 offset = m_position - m_target;
    double distance = glm::length(offset);
    
    // Convert to spherical coordinates
    double pitch = asin(offset.y / distance);
    double yaw = atan2(offset.z, offset.x);
    
    // Apply rotation
    yaw += deltaYaw;
    pitch += deltaPitch;
    
    // Clamp pitch
    const double maxPitch = glm::radians(89.0);
    pitch = glm::clamp(pitch, -maxPitch, maxPitch);
    
    // Convert back to cartesian
    offset.x = distance * cos(pitch) * cos(yaw);
    offset.y = distance * sin(pitch);
    offset.z = distance * cos(pitch) * sin(yaw);
    
    m_position = m_target + offset;
}

void Camera::Orbit(const glm::dvec3& target, double distance, double yaw, double pitch) {
    m_target = target;
    
    // Clamp pitch
    const double maxPitch = glm::radians(89.0);
    pitch = glm::clamp(pitch, -maxPitch, maxPitch);
    
    // Calculate position from spherical coordinates
    m_position.x = target.x + distance * cos(pitch) * cos(yaw);
    m_position.y = target.y + distance * sin(pitch);
    m_position.z = target.z + distance * cos(pitch) * sin(yaw);
}

} // namespace Kosmos