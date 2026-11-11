#ifdef NDEBUG
#undef NDEBUG
#endif

#include "rcs_visual.h"

#include <cassert>
#include <cmath>
#include <cstdio>

int main()
{
    const kosmos::StageSpec::RcsVisual layout{0.35, 2.55, 1.1, 1.35};
    std::vector<kosmos::StageSpec> stages(3);
    assert(!kosmos::app::rcs_visual_layout(stages, 0).first);
    stages[1].rcs_visual = layout;
    assert(kosmos::app::rcs_visual_layout(stages, 0).second);
    assert(kosmos::app::rcs_visual_layout(stages, 1).second);
    const auto separated = kosmos::app::rcs_visual_layout(stages, 2);
    assert(separated.first && !separated.second);
    const glm::vec3 centre(0.0f, 0.0f, 1.45f);
    for (int axis = 0; axis < 3; ++axis)
        for (float sign : {-1.0f, 1.0f})
        {
            glm::vec3 command(0.0f);
            command[axis] = sign;
            const auto rotation = kosmos::app::rcs_visual_jets(layout, glm::vec3(0), command);
            assert(rotation.size() == 2);
            glm::vec3 net_force(0), net_torque(0);
            for (const auto &jet : rotation)
            {
                assert(std::fabs(glm::length(glm::vec2(jet.position)) -
                                 (jet.position.z < centre.z ? 1.1f : 1.35f)) < 1e-5f);
                assert(std::fabs(jet.position.z - 0.35f) < 1e-5f || std::fabs(jet.position.z - 2.55f) < 1e-5f);
                net_force += jet.push;
                net_torque += glm::cross(jet.position - centre, jet.push);
            }
            assert(glm::length(net_force) < 1e-6f);
            assert(net_torque[axis] * sign > 0.0f);
            const auto translation = kosmos::app::rcs_visual_jets(layout, command, glm::vec3(0));
            assert(translation.size() == 1 && translation[0].push[axis] == sign);
            assert(translation[0].position.z >= 0.35f && translation[0].position.z <= 2.55f);
        }
    assert(kosmos::app::rcs_visual_jets(layout, glm::vec3(0), glm::vec3(0)).empty());
    std::puts("rcs_visual_test passed");
}
