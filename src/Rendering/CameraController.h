#pragma once

#include "Camera.h"
#include "../Physics/Vessel.h"
#include <memory>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Kosmos {

// Forward declarations
class CelestialBody;

/**
 * Camera modes supported by the camera controller
 */
enum class CameraMode {
    FREE,           // Free-flying camera, not attached to any object
    COCKPIT,        // First-person view from vessel cockpit
    EXTERNAL,       // Third-person view following vessel
    TRACK,          // Camera tracks vessel from fixed distance
    GROUND,         // Camera on surface of celestial body
    TARGET,         // Camera looks at target from fixed position
    FOLLOW          // Camera follows object at offset
};

/**
 * Camera definition for cockpit/internal cameras
 */
struct CockpitCamera {
    std::string name;
    glm::dvec3 position;        // Position relative to vessel origin
    glm::dvec3 direction;       // View direction (normalized)
    glm::dvec3 up;              // Up vector
    float fov;                  // Field of view in degrees
    
    CockpitCamera(const std::string& n = "Default", 
                  const glm::dvec3& pos = glm::dvec3(0, 0, 0),
                  const glm::dvec3& dir = glm::dvec3(0, 0, -1),
                  const glm::dvec3& u = glm::dvec3(0, 1, 0),
                  float f = 60.0f)
        : name(n), position(pos), direction(glm::normalize(dir)), up(u), fov(f) {}
};

/**
 * Camera definition for external cameras
 */
struct ExternalCamera {
    std::string name;
    glm::dvec3 offset;          // Offset from vessel center
    double distance;            // Distance from vessel
    double yaw;                 // Rotation around vessel (radians)
    double pitch;               // Elevation angle (radians)
    bool trackVessel;           // Always look at vessel
    
    ExternalCamera(const std::string& n = "Default",
                   const glm::dvec3& off = glm::dvec3(0, 0, 0),
                   double dist = 10.0,
                   double y = 0.0,
                   double p = 0.0,
                   bool track = true)
        : name(n), offset(off), distance(dist), yaw(y), pitch(p), trackVessel(track) {}
};

/**
 * Controls camera behavior and attachment to simulation objects
 */
class CameraController {
public:
    CameraController(Camera* camera);
    ~CameraController() = default;
    
    // Mode management
    void SetMode(CameraMode mode);
    CameraMode GetMode() const { return m_mode; }
    const char* GetModeName() const;
    
    // Object attachment
    void AttachToVessel(Vessel* vessel);
    void AttachToCelestialBody(CelestialBody* body);
    void Detach();
    
    Vessel* GetAttachedVessel() const { return m_attachedVessel; }
    CelestialBody* GetAttachedBody() const { return m_attachedBody; }
    
    // Cockpit cameras
    void AddCockpitCamera(const CockpitCamera& camera);
    void SetActiveCockpitCamera(size_t index);
    void NextCockpitCamera();
    void PreviousCockpitCamera();
    size_t GetActiveCockpitCameraIndex() const { return m_activeCockpitCamera; }
    size_t GetCockpitCameraCount() const { return m_cockpitCameras.size(); }
    const CockpitCamera* GetCockpitCamera(size_t index) const;
    
    // External cameras
    void AddExternalCamera(const ExternalCamera& camera);
    void SetActiveExternalCamera(size_t index);
    void NextExternalCamera();
    void PreviousExternalCamera();
    size_t GetActiveExternalCameraIndex() const { return m_activeExternalCamera; }
    size_t GetExternalCameraCount() const { return m_externalCameras.size(); }
    const ExternalCamera* GetExternalCamera(size_t index) const;
    
    // External camera controls (for active external camera)
    void SetExternalDistance(double distance);
    void SetExternalAngles(double yaw, double pitch);
    void AdjustExternalDistance(double delta);
    void RotateExternal(double deltaYaw, double deltaPitch);
    
    // Free camera controls
    void SetFreePosition(const glm::dvec3& position);
    void SetFreeTarget(const glm::dvec3& target);
    void SetFreeTargetDirect(const glm::dvec3& target);  // Set target without changing orientation
    glm::dvec3 GetFreePosition() const { return m_freePosition; }
    glm::dvec3 GetFreeTarget() const { return m_freeTarget; }
    
    // Get forward direction from quaternion orientation
    glm::dvec3 GetFreeForward() const;
    void SetFreeForward(const glm::dvec3& forward);
    
    void MoveFree(const glm::dvec3& delta);
    void RotateFree(double deltaYaw, double deltaPitch);
    void RollFree(double deltaRoll);
    
    // Target tracking
    void SetTrackTarget(const glm::dvec3& target);
    void SetTrackDistance(double distance);
    
    // Update camera based on mode and attached object
    void Update(double deltaTime);
    
    // Utility
    glm::dvec3 GetWorldPosition() const;
    glm::dvec3 GetWorldTarget() const;
    
private:
    Camera* m_camera;
    CameraMode m_mode;
    
    // Attached objects
    Vessel* m_attachedVessel;
    CelestialBody* m_attachedBody;
    
    // Camera definitions
    std::vector<CockpitCamera> m_cockpitCameras;
    std::vector<ExternalCamera> m_externalCameras;
    size_t m_activeCockpitCamera;
    size_t m_activeExternalCamera;
    
    // Free camera state
    glm::dvec3 m_freePosition;
    glm::dvec3 m_freeTarget;
    double m_freeHeading;  // Yaw (rotation around up)
    double m_freePitch;    // Pitch (rotation around right)
    double m_freeBank;     // Roll (rotation around forward)
    
    // External camera state
    double m_externalYaw;
    double m_externalPitch;
    double m_externalDistance;
    
    // Track camera state
    glm::dvec3 m_trackTarget;
    double m_trackDistance;
    
    // Update methods for each mode
    void UpdateFreeCamera();
    void UpdateCockpitCamera();
    void UpdateExternalCamera();
    void UpdateTrackCamera();
    void UpdateGroundCamera();
    void UpdateTargetCamera();
    void UpdateFollowCamera();
    
    // Helper methods
    glm::dvec3 GetVesselPosition() const;
    glm::dmat3 GetVesselOrientation() const;
    void InitializeDefaultCameras();
};

} // namespace Kosmos
