#include "CameraController.h"
#include "../Physics/CelestialBody.h"
#include "../Core/Logger.h"
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>

namespace Kosmos {

CameraController::CameraController(Camera* camera)
    : m_camera(camera)
    , m_mode(CameraMode::FREE)
    , m_attachedVessel(nullptr)
    , m_attachedBody(nullptr)
    , m_activeCockpitCamera(0)
    , m_activeExternalCamera(0)
    , m_freePosition(0.0, 0.0, 10.0)
    , m_freeTarget(0.0, 0.0, 0.0)
    , m_freeHeading(0.0)
    , m_freePitch(0.0)
    , m_freeBank(0.0)
    , m_externalYaw(0.0)
    , m_externalPitch(glm::radians(20.0))
    , m_externalDistance(10.0)
    , m_trackTarget(0.0, 0.0, 0.0)
    , m_trackDistance(100.0)
{
    InitializeDefaultCameras();
}

void CameraController::InitializeDefaultCameras() {
    // Add default cockpit camera
    m_cockpitCameras.push_back(CockpitCamera(
        "Cockpit",
        glm::dvec3(0.0, 1.5, 2.0),  // Slightly forward and up
        glm::dvec3(0.0, 0.0, -1.0),  // Looking forward
        glm::dvec3(0.0, 1.0, 0.0),   // Up vector
        60.0f                         // FOV
    ));
    
    // Add default external cameras
    m_externalCameras.push_back(ExternalCamera(
        "External Rear",
        glm::dvec3(0.0, 0.0, 0.0),
        10.0,
        glm::radians(180.0),
        glm::radians(15.0),
        true
    ));
    
    m_externalCameras.push_back(ExternalCamera(
        "External Front",
        glm::dvec3(0.0, 0.0, 0.0),
        10.0,
        glm::radians(0.0),
        glm::radians(15.0),
        true
    ));
    
    m_externalCameras.push_back(ExternalCamera(
        "External Top",
        glm::dvec3(0.0, 0.0, 0.0),
        15.0,
        glm::radians(180.0),
        glm::radians(89.0),
        true
    ));
}

void CameraController::SetMode(CameraMode mode) {
    if (m_mode == mode) return;
    
    CameraMode oldMode = m_mode;
    m_mode = mode;
    
    LOG_INFO("Camera mode changed from " + std::string(GetModeName()) + " to " + std::string(GetModeName()));
    
    // Initialize mode-specific state
    switch (m_mode) {
        case CameraMode::FREE:
            m_freePosition = m_camera->GetPosition();
            m_freeTarget = m_camera->GetTarget();
            // Calculate Euler angles from current direction
            {
                glm::dvec3 forward = glm::normalize(m_freeTarget - m_freePosition);
                m_freeHeading = atan2(forward.x, -forward.z);  // Yaw
                m_freePitch = asin(forward.y);                 // Pitch
                m_freeBank = 0.0;                              // Roll
            }
            break;
            
        case CameraMode::EXTERNAL:
            if (m_attachedVessel && m_externalCameras.size() > 0) {
                const auto& extCam = m_externalCameras[m_activeExternalCamera];
                m_externalYaw = extCam.yaw;
                m_externalPitch = extCam.pitch;
                m_externalDistance = extCam.distance;
            }
            break;
            
        default:
            break;
    }
}

const char* CameraController::GetModeName() const {
    switch (m_mode) {
        case CameraMode::FREE: return "Free";
        case CameraMode::COCKPIT: return "Cockpit";
        case CameraMode::EXTERNAL: return "External";
        case CameraMode::TRACK: return "Track";
        case CameraMode::GROUND: return "Ground";
        case CameraMode::TARGET: return "Target";
        case CameraMode::FOLLOW: return "Follow";
        default: return "Unknown";
    }
}

void CameraController::AttachToVessel(Vessel* vessel) {
    m_attachedVessel = vessel;
    m_attachedBody = nullptr;
    
    if (vessel) {
        LOG_INFO("Camera attached to vessel: " + vessel->GetName());
        
        // Switch to cockpit mode by default when attaching to vessel
        if (m_mode == CameraMode::FREE) {
            SetMode(CameraMode::COCKPIT);
        }
    } else {
        LOG_INFO("Camera detached from vessel");
    }
}

void CameraController::AttachToCelestialBody(CelestialBody* body) {
    m_attachedBody = body;
    m_attachedVessel = nullptr;
    
    if (body) {
        LOG_INFO("Camera attached to celestial body");
    }
}

void CameraController::Detach() {
    m_attachedVessel = nullptr;
    m_attachedBody = nullptr;
    SetMode(CameraMode::FREE);
}

// Cockpit camera management
void CameraController::AddCockpitCamera(const CockpitCamera& camera) {
    m_cockpitCameras.push_back(camera);
}

void CameraController::SetActiveCockpitCamera(size_t index) {
    if (index < m_cockpitCameras.size()) {
        m_activeCockpitCamera = index;
        LOG_INFO("Switched to cockpit camera: " + m_cockpitCameras[index].name);
    }
}

void CameraController::NextCockpitCamera() {
    if (m_cockpitCameras.empty()) return;
    m_activeCockpitCamera = (m_activeCockpitCamera + 1) % m_cockpitCameras.size();
    LOG_INFO("Switched to cockpit camera: " + m_cockpitCameras[m_activeCockpitCamera].name);
}

void CameraController::PreviousCockpitCamera() {
    if (m_cockpitCameras.empty()) return;
    m_activeCockpitCamera = (m_activeCockpitCamera + m_cockpitCameras.size() - 1) % m_cockpitCameras.size();
    LOG_INFO("Switched to cockpit camera: " + m_cockpitCameras[m_activeCockpitCamera].name);
}

const CockpitCamera* CameraController::GetCockpitCamera(size_t index) const {
    if (index < m_cockpitCameras.size()) {
        return &m_cockpitCameras[index];
    }
    return nullptr;
}

// External camera management
void CameraController::AddExternalCamera(const ExternalCamera& camera) {
    m_externalCameras.push_back(camera);
}

void CameraController::SetActiveExternalCamera(size_t index) {
    if (index < m_externalCameras.size()) {
        m_activeExternalCamera = index;
        const auto& extCam = m_externalCameras[index];
        m_externalYaw = extCam.yaw;
        m_externalPitch = extCam.pitch;
        m_externalDistance = extCam.distance;
        LOG_INFO("Switched to external camera: " + extCam.name);
    }
}

void CameraController::NextExternalCamera() {
    if (m_externalCameras.empty()) return;
    SetActiveExternalCamera((m_activeExternalCamera + 1) % m_externalCameras.size());
}

void CameraController::PreviousExternalCamera() {
    if (m_externalCameras.empty()) return;
    SetActiveExternalCamera((m_activeExternalCamera + m_externalCameras.size() - 1) % m_externalCameras.size());
}

const ExternalCamera* CameraController::GetExternalCamera(size_t index) const {
    if (index < m_externalCameras.size()) {
        return &m_externalCameras[index];
    }
    return nullptr;
}

// External camera controls
void CameraController::SetExternalDistance(double distance) {
    m_externalDistance = glm::clamp(distance, 1.0, 10000.0);
}

void CameraController::SetExternalAngles(double yaw, double pitch) {
    m_externalYaw = yaw;
    m_externalPitch = glm::clamp(pitch, glm::radians(-89.0), glm::radians(89.0));
}

void CameraController::AdjustExternalDistance(double delta) {
    m_externalDistance = glm::clamp(m_externalDistance + delta, 1.0, 10000.0);
}

void CameraController::RotateExternal(double deltaYaw, double deltaPitch) {
    m_externalYaw += deltaYaw;
    m_externalPitch = glm::clamp(m_externalPitch + deltaPitch, glm::radians(-89.0), glm::radians(89.0));
}

// Free camera controls
void CameraController::SetFreePosition(const glm::dvec3& position) {
    m_freePosition = position;
}

void CameraController::SetFreeTarget(const glm::dvec3& target) {
    m_freeTarget = target;
    // Don't recalculate angles - preserve user's orientation
}

// Set target without changing orientation (for camera tracking)
void CameraController::SetFreeTargetDirect(const glm::dvec3& target) {
    m_freeTarget = target;
}

// Compute forward direction from quaternion
glm::dvec3 CameraController::GetFreeForward() const {
    // Build quaternion from Euler angles: heading (yaw), pitch, bank (roll)
    // Apply rotations in order: yaw around Y, pitch around X, roll around Z
    glm::dquat quat = glm::angleAxis(m_freeHeading, glm::dvec3(0, 1, 0)) *
                      glm::angleAxis(m_freePitch, glm::dvec3(1, 0, 0)) *
                      glm::angleAxis(m_freeBank, glm::dvec3(0, 0, 1));
    glm::dmat3 rotMat = glm::mat3_cast(quat);
    return -rotMat[2];  // Negative Z column (camera looks down -Z)
}

// Set forward direction by computing quaternion
void CameraController::SetFreeForward(const glm::dvec3& forward) {
    // Update target to point in the new direction
    double distance = glm::length(m_freeTarget - m_freePosition);
    m_freeTarget = m_freePosition + glm::normalize(forward) * distance;
    // Don't recalculate angles - this would cause gimbal lock issues
}

void CameraController::MoveFree(const glm::dvec3& delta) {
    // Get current direction vectors from phi/theta
    glm::dvec3 forward = GetFreeForward();
    glm::dvec3 up = glm::dvec3(0, 1, 0);  // World up
    glm::dvec3 right = glm::normalize(glm::cross(forward, up));
    
    m_freePosition += right * delta.x + up * delta.y + forward * delta.z;
    
    // Update target to maintain distance
    double distance = glm::length(m_freeTarget - m_freePosition);
    m_freeTarget = m_freePosition + forward * distance;
}

void CameraController::RotateFree(double deltaYaw, double deltaPitch) {
    // Accumulate Euler angles (heading = yaw, pitch = pitch)
    m_freeHeading -= deltaYaw;  // Negative for correct direction
    m_freePitch += deltaPitch;
    
    // Update target
    glm::dvec3 forward = GetFreeForward();
    double distance = glm::length(m_freeTarget - m_freePosition);
    m_freeTarget = m_freePosition + forward * distance;
}

void CameraController::RollFree(double deltaRoll) {
    // Accumulate bank angle (roll)
    m_freeBank += deltaRoll;
    
    // Update target
    glm::dvec3 forward = GetFreeForward();
    double distance = glm::length(m_freeTarget - m_freePosition);
    m_freeTarget = m_freePosition + forward * distance;
}

// Track camera controls
void CameraController::SetTrackTarget(const glm::dvec3& target) {
    m_trackTarget = target;
}

void CameraController::SetTrackDistance(double distance) {
    m_trackDistance = distance;
}

// Update camera based on current mode
void CameraController::Update(double deltaTime) {
    switch (m_mode) {
        case CameraMode::FREE:
            UpdateFreeCamera();
            break;
        case CameraMode::COCKPIT:
            UpdateCockpitCamera();
            break;
        case CameraMode::EXTERNAL:
            UpdateExternalCamera();
            break;
        case CameraMode::TRACK:
            UpdateTrackCamera();
            break;
        case CameraMode::GROUND:
            UpdateGroundCamera();
            break;
        case CameraMode::TARGET:
            UpdateTargetCamera();
            break;
        case CameraMode::FOLLOW:
            UpdateFollowCamera();
            break;
    }
}

void CameraController::UpdateFreeCamera() {
    m_camera->SetPosition(m_freePosition);
    m_camera->SetTarget(m_freeTarget);
    
    // Build quaternion from Euler angles: heading (yaw), pitch, bank (roll)
    glm::dquat quat = glm::angleAxis(m_freeHeading, glm::dvec3(0, 1, 0)) *
                      glm::angleAxis(m_freePitch, glm::dvec3(1, 0, 0)) *
                      glm::angleAxis(m_freeBank, glm::dvec3(0, 0, 1));
    glm::dmat3 rotMat = glm::mat3_cast(quat);
    glm::dvec3 up = glm::dvec3(rotMat[1]);
    m_camera->SetUp(up);
}

void CameraController::UpdateCockpitCamera() {
    if (!m_attachedVessel || m_cockpitCameras.empty()) {
        return;
    }
    
    const auto& cockpitCam = m_cockpitCameras[m_activeCockpitCamera];
    
    // Get vessel position and orientation
    glm::dvec3 vesselPos = GetVesselPosition();
    glm::dmat3 vesselRot = GetVesselOrientation();
    
    // Transform cockpit camera position and direction by vessel orientation
    glm::dvec3 worldPos = vesselPos + vesselRot * cockpitCam.position;
    glm::dvec3 worldDir = vesselRot * cockpitCam.direction;
    glm::dvec3 worldUp = vesselRot * cockpitCam.up;
    
    m_camera->SetPosition(worldPos);
    m_camera->SetTarget(worldPos + worldDir);
    m_camera->SetUp(worldUp);
}

void CameraController::UpdateExternalCamera() {
    if (!m_attachedVessel || m_externalCameras.empty()) {
        return;
    }
    
    const auto& extCam = m_externalCameras[m_activeExternalCamera];
    
    // Get vessel position
    glm::dvec3 vesselPos = GetVesselPosition();
    glm::dmat3 vesselRot = GetVesselOrientation();
    
    // Calculate camera position in spherical coordinates around vessel
    glm::dvec3 offset;
    offset.x = m_externalDistance * cos(m_externalPitch) * cos(m_externalYaw);
    offset.y = m_externalDistance * sin(m_externalPitch);
    offset.z = m_externalDistance * cos(m_externalPitch) * sin(m_externalYaw);
    
    glm::dvec3 worldOffset = vesselRot * extCam.offset;
    glm::dvec3 cameraPos = vesselPos + worldOffset + offset;
    
    m_camera->SetPosition(cameraPos);
    
    if (extCam.trackVessel) {
        // Look at vessel
        m_camera->SetTarget(vesselPos + worldOffset);
    } else {
        // Look in a fixed direction
        glm::dvec3 forward = -glm::normalize(offset);
        m_camera->SetTarget(cameraPos + forward);
    }
    
    m_camera->SetUp(glm::dvec3(0, 1, 0));
}

void CameraController::UpdateTrackCamera() {
    if (!m_attachedVessel) {
        return;
    }
    
    glm::dvec3 vesselPos = GetVesselPosition();
    glm::dvec3 toVessel = vesselPos - m_trackTarget;
    double distance = glm::length(toVessel);
    
    if (distance > 0.001) {
        glm::dvec3 direction = toVessel / distance;
        glm::dvec3 cameraPos = m_trackTarget + direction * m_trackDistance;
        
        m_camera->SetPosition(cameraPos);
        m_camera->SetTarget(vesselPos);
        m_camera->SetUp(glm::dvec3(0, 1, 0));
    }
}

void CameraController::UpdateGroundCamera() {
    // TODO: Implement ground camera on celestial body surface
    if (m_attachedBody) {
        // Position camera on surface looking at horizon
    }
}

void CameraController::UpdateTargetCamera() {
    // TODO: Implement target camera (fixed position looking at target)
}

void CameraController::UpdateFollowCamera() {
    // TODO: Implement follow camera (follows object at offset)
}

glm::dvec3 CameraController::GetVesselPosition() const {
    if (m_attachedVessel) {
        return m_attachedVessel->GetPosition();
    }
    return glm::dvec3(0.0);
}

glm::dmat3 CameraController::GetVesselOrientation() const {
    // TODO: Get actual vessel orientation when rotation is implemented
    // For now, return identity (vessel aligned with world axes)
    return glm::dmat3(1.0);
}

glm::dvec3 CameraController::GetWorldPosition() const {
    return m_camera->GetPosition();
}

glm::dvec3 CameraController::GetWorldTarget() const {
    return m_camera->GetTarget();
}

} // namespace Kosmos
