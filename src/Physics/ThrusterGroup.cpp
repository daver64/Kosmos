#include "ThrusterGroup.h"
#include <algorithm>

namespace Kosmos {

ThrusterGroup::ThrusterGroup(GroupType type, const std::string& name)
    : m_type(type)
    , m_name(name)
    , m_groupLevel(0.0)
{
}

void ThrusterGroup::AddThruster(Thruster* thruster, double weight) {
    m_thrusters.emplace_back(thruster, weight);
}

void ThrusterGroup::RemoveThruster(Thruster* thruster) {
    auto it = std::remove_if(m_thrusters.begin(), m_thrusters.end(),
        [thruster](const auto& pair) { return pair.first == thruster; });
    m_thrusters.erase(it, m_thrusters.end());
}

void ThrusterGroup::Clear() {
    m_thrusters.clear();
}

void ThrusterGroup::SetLevel(double level) {
    m_groupLevel = std::clamp(level, 0.0, 1.0);
    
    for (auto& [thruster, weight] : m_thrusters) {
        thruster->SetLevel(m_groupLevel * weight);
    }
}

double ThrusterGroup::GetMaxThrust() const {
    double total = 0.0;
    for (const auto& [thruster, weight] : m_thrusters) {
        total += thruster->GetMaxThrust() * weight;
    }
    return total;
}

double ThrusterGroup::GetCurrentThrust() const {
    double total = 0.0;
    for (const auto& [thruster, weight] : m_thrusters) {
        total += thruster->GetCurrentThrust();
    }
    return total;
}

glm::dvec3 ThrusterGroup::GetTotalThrustVector() const {
    glm::dvec3 total(0.0);
    for (const auto& [thruster, weight] : m_thrusters) {
        total += thruster->GetThrustVector();
    }
    return total;
}

void ThrusterGroup::SetThrusterWeight(Thruster* thruster, double weight) {
    for (auto& [t, w] : m_thrusters) {
        if (t == thruster) {
            w = weight;
            break;
        }
    }
}

// ThrusterGroupManager implementation

ThrusterGroup* ThrusterGroupManager::CreateGroup(ThrusterGroup::GroupType type, const std::string& name) {
    auto group = std::make_unique<ThrusterGroup>(type, name);
    ThrusterGroup* ptr = group.get();
    m_groups.push_back(std::move(group));
    return ptr;
}

ThrusterGroup* ThrusterGroupManager::GetGroup(ThrusterGroup::GroupType type) {
    for (auto& group : m_groups) {
        if (group->GetType() == type) {
            return group.get();
        }
    }
    return nullptr;
}

ThrusterGroup* ThrusterGroupManager::GetGroupByName(const std::string& name) {
    for (auto& group : m_groups) {
        if (group->GetName() == name) {
            return group.get();
        }
    }
    return nullptr;
}

void ThrusterGroupManager::RemoveGroup(ThrusterGroup::GroupType type) {
    auto it = std::remove_if(m_groups.begin(), m_groups.end(),
        [type](const auto& group) { return group->GetType() == type; });
    m_groups.erase(it, m_groups.end());
}

void ThrusterGroupManager::SetMainLevel(double level) {
    if (auto* group = GetGroup(ThrusterGroup::GroupType::MAIN)) {
        group->SetLevel(level);
    }
}

void ThrusterGroupManager::SetRetroLevel(double level) {
    if (auto* group = GetGroup(ThrusterGroup::GroupType::RETRO)) {
        group->SetLevel(level);
    }
}

void ThrusterGroupManager::SetHoverLevel(double level) {
    if (auto* group = GetGroup(ThrusterGroup::GroupType::HOVER)) {
        group->SetLevel(level);
    }
}

void ThrusterGroupManager::SetRotationControl(const glm::dvec3& angularControl) {
    // Pitch
    if (angularControl.x > 0) {
        if (auto* group = GetGroup(ThrusterGroup::GroupType::RCS_PITCH_UP)) {
            group->SetLevel(angularControl.x);
        }
    } else if (angularControl.x < 0) {
        if (auto* group = GetGroup(ThrusterGroup::GroupType::RCS_PITCH_DOWN)) {
            group->SetLevel(-angularControl.x);
        }
    }
    
    // Yaw
    if (angularControl.y > 0) {
        if (auto* group = GetGroup(ThrusterGroup::GroupType::RCS_YAW_RIGHT)) {
            group->SetLevel(angularControl.y);
        }
    } else if (angularControl.y < 0) {
        if (auto* group = GetGroup(ThrusterGroup::GroupType::RCS_YAW_LEFT)) {
            group->SetLevel(-angularControl.y);
        }
    }
    
    // Roll
    if (angularControl.z > 0) {
        if (auto* group = GetGroup(ThrusterGroup::GroupType::RCS_ROLL_RIGHT)) {
            group->SetLevel(angularControl.z);
        }
    } else if (angularControl.z < 0) {
        if (auto* group = GetGroup(ThrusterGroup::GroupType::RCS_ROLL_LEFT)) {
            group->SetLevel(-angularControl.z);
        }
    }
}

void ThrusterGroupManager::SetTranslationControl(const glm::dvec3& linearControl) {
    // X axis
    if (linearControl.x > 0) {
        if (auto* group = GetGroup(ThrusterGroup::GroupType::RCS_TRANSLATION_RIGHT)) {
            group->SetLevel(linearControl.x);
        }
    } else if (linearControl.x < 0) {
        if (auto* group = GetGroup(ThrusterGroup::GroupType::RCS_TRANSLATION_LEFT)) {
            group->SetLevel(-linearControl.x);
        }
    }
    
    // Y axis
    if (linearControl.y > 0) {
        if (auto* group = GetGroup(ThrusterGroup::GroupType::RCS_TRANSLATION_UP)) {
            group->SetLevel(linearControl.y);
        }
    } else if (linearControl.y < 0) {
        if (auto* group = GetGroup(ThrusterGroup::GroupType::RCS_TRANSLATION_DOWN)) {
            group->SetLevel(-linearControl.y);
        }
    }
    
    // Z axis
    if (linearControl.z > 0) {
        if (auto* group = GetGroup(ThrusterGroup::GroupType::RCS_TRANSLATION_FORWARD)) {
            group->SetLevel(linearControl.z);
        }
    } else if (linearControl.z < 0) {
        if (auto* group = GetGroup(ThrusterGroup::GroupType::RCS_TRANSLATION_BACK)) {
            group->SetLevel(-linearControl.z);
        }
    }
}

} // namespace Kosmos
