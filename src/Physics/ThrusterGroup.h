#pragma once

#include "Thruster.h"
#include <string>
#include <vector>
#include <memory>

namespace Kosmos {

class ThrusterGroup {
public:
    enum class GroupType {
        MAIN,
        RETRO,
        HOVER,
        RCS_PITCH_UP,
        RCS_PITCH_DOWN,
        RCS_YAW_LEFT,
        RCS_YAW_RIGHT,
        RCS_ROLL_LEFT,
        RCS_ROLL_RIGHT,
        RCS_TRANSLATION_FORWARD,
        RCS_TRANSLATION_BACK,
        RCS_TRANSLATION_LEFT,
        RCS_TRANSLATION_RIGHT,
        RCS_TRANSLATION_UP,
        RCS_TRANSLATION_DOWN,
        CUSTOM
    };
    
    ThrusterGroup(GroupType type, const std::string& name = "");
    ~ThrusterGroup() = default;
    
    void AddThruster(Thruster* thruster, double weight = 1.0);
    void RemoveThruster(Thruster* thruster);
    void Clear();
    
    void SetLevel(double level);
    double GetLevel() const { return m_groupLevel; }
    
    double GetMaxThrust() const;
    double GetCurrentThrust() const;
    glm::dvec3 GetTotalThrustVector() const;
    
    void SetThrusterWeight(Thruster* thruster, double weight);
    
    GroupType GetType() const { return m_type; }
    const std::string& GetName() const { return m_name; }
    
    const std::vector<std::pair<Thruster*, double>>& GetThrusters() const {
        return m_thrusters;
    }
    
private:
    GroupType m_type;
    std::string m_name;
    double m_groupLevel;
    std::vector<std::pair<Thruster*, double>> m_thrusters;
};

class ThrusterGroupManager {
public:
    ThrusterGroupManager() = default;
    ~ThrusterGroupManager() = default;
    
    ThrusterGroup* CreateGroup(ThrusterGroup::GroupType type, const std::string& name = "");
    ThrusterGroup* GetGroup(ThrusterGroup::GroupType type);
    ThrusterGroup* GetGroupByName(const std::string& name);
    
    void RemoveGroup(ThrusterGroup::GroupType type);
    
    void SetMainLevel(double level);
    void SetRetroLevel(double level);
    void SetHoverLevel(double level);
    
    void SetRotationControl(const glm::dvec3& angularControl);
    void SetTranslationControl(const glm::dvec3& linearControl);
    
    const std::vector<std::unique_ptr<ThrusterGroup>>& GetGroups() const {
        return m_groups;
    }
    
private:
    std::vector<std::unique_ptr<ThrusterGroup>> m_groups;
};

} // namespace Kosmos
