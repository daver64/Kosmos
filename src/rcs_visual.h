#pragma once

#include "ship.h"

namespace kosmos::app
{
    struct RcsVisualJet
    {
        glm::vec3 position, push;
        float amount;
    };

    inline std::pair<bool, const StageSpec::RcsVisual *> rcs_visual_layout(const std::vector<StageSpec> &stages,
                                                                            int active_stage)
    {
        for (std::size_t s = 0; s < stages.size(); ++s)
            if (stages[s].rcs_visual)
                return {true, static_cast<int>(s) >= active_stage ? &*stages[s].rcs_visual : nullptr};
        return {false, nullptr};
    }

    inline std::vector<RcsVisualJet> rcs_visual_jets(const StageSpec::RcsVisual &layout,
                                                      const glm::vec3 &translation, const glm::vec3 &torque)
    {
        std::vector<RcsVisualJet> jets;
        const float front = static_cast<float>(layout.front_z), rear = static_cast<float>(layout.rear_z);
        const float front_radius = static_cast<float>(layout.front_radius), rear_radius = static_cast<float>(layout.rear_radius);
        const float centre = (front + rear) * 0.5f;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float amount = std::min(1.0f, std::fabs(translation[axis]));
            if (amount <= 0.01f)
                continue;
            glm::vec3 push(0.0f);
            push[axis] = translation[axis] > 0.0f ? 1.0f : -1.0f;
            const glm::vec3 position = axis == 2
                ? glm::vec3(push.z > 0.0f ? front_radius : rear_radius, 0.0f, push.z > 0.0f ? front : rear)
                : glm::vec3(-push.x * rear_radius, -push.y * rear_radius, rear);
            jets.push_back({position, push, amount});
        }
        for (int axis = 0; axis < 3; ++axis)
        {
            const float amount = std::min(1.0f, std::fabs(torque[axis]));
            if (amount < 0.05f)
                continue;
            glm::vec3 direction(0.0f);
            direction[axis] = torque[axis] > 0.0f ? 1.0f : -1.0f;
            for (int end = 0; end < 2; ++end)
            {
                if (axis == 2)
                {
                    const glm::vec3 arm(end == 0 ? rear_radius : -rear_radius, 0.0f, 0.0f);
                    jets.push_back({arm + glm::vec3(0, 0, rear), glm::normalize(glm::cross(direction, arm)), amount});
                }
                else
                {
                    const float z = end == 0 ? rear : front;
                    const float radius = end == 0 ? rear_radius : front_radius;
                    const glm::vec3 arm(0.0f, 0.0f, z - centre);
                    const glm::vec3 push = glm::normalize(glm::cross(direction, arm));
                    jets.push_back({glm::vec3(-push.x * radius, -push.y * radius, z), push, amount});
                }
            }
        }
        return jets;
    }
}
