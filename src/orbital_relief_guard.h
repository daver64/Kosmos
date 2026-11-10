#pragma once

#include "flight.h"

#include <cmath>
#include <string>

namespace kosmos
{
    inline bool relief_vessel_near_camera(const glm::dvec3 &origin, const sl::FlightBody &vessel,
                                          const glm::dvec3 &eye, double range)
    {
        const double origin_distance = glm::length(origin - eye);
        if (!std::isfinite(origin_distance) || origin_distance <= range)
            return true;
        for (const sl::GearContact &gear : vessel.gear)
        {
            const glm::dvec3 contact = vessel.position + vessel.direction_to_world(gear.position);
            const double distance = glm::length(contact - eye);
            if (!std::isfinite(distance) || distance <= range)
                return true;
        }
        return false;
    }

    template <class CollisionHeight, class CheckHeight>
    bool check_relief_gear_contacts(const sl::FlightBody &vessel, const glm::dvec3 &body_position,
                                    const glm::dquat &world_to_tile, double radius, const glm::dvec3 &eye,
                                    double camera_range,
                                    CollisionHeight &&collision_height, CheckHeight &&check_height,
                                    std::string &reason)
    {
        for (const sl::GearContact &gear : vessel.gear)
        {
            const glm::dvec3 world_contact = vessel.position + vessel.direction_to_world(gear.position);
            const double camera_distance = glm::length(world_contact - eye);
            if (!std::isfinite(camera_distance))
            {
                reason = "ship gear position is invalid";
                return false;
            }
            if (camera_distance > camera_range)
                continue;
            const glm::dvec3 contact = world_contact - body_position;
            const double distance = glm::length(contact);
            if (!std::isfinite(distance) || distance < 1.0)
            {
                reason = "ship gear position is invalid";
                return false;
            }
            const glm::dvec3 direction = world_to_tile * (contact / distance);
            const double expected = collision_height(direction);
            if (!std::isfinite(expected))
            {
                reason = "ship gear: collision height is not finite";
                return false;
            }
            if (distance - radius - expected <= 250.0 && !check_height(direction, expected, "ship gear"))
                return false;
        }
        return true;
    }
}
