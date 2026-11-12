#pragma once

#include "flight.h"
#include "clutter.h"

#include <cmath>
#include <chrono>
#include <string>
#include <vector>

namespace kosmos
{
    inline constexpr double relief_near_surface_height_m = 250.0;
    inline constexpr double relief_vessel_camera_range_m = 10000.0;
    inline constexpr double relief_fallback_hold_height_m = 1000.0;
    inline constexpr double relief_fallback_hold_speed_mps = 500.0;

    inline bool hold_relief_fallback(double altitude, double surface_speed)
    {
        return !std::isfinite(altitude) || !std::isfinite(surface_speed) ||
               (altitude <= relief_fallback_hold_height_m && surface_speed <= relief_fallback_hold_speed_mps);
    }

    struct ReliefFallbackState
    {
        bool fallback = false;
        bool held = false;
        bool relief_was_ready = false;
        std::chrono::steady_clock::time_point ready_since{};

        void update(bool mismatch, bool hold, std::chrono::steady_clock::time_point now)
        {
            if (mismatch)
            {
                if (relief_was_ready && hold)
                    held = true;
                fallback = true;
                ready_since = {};
                return;
            }
            if (fallback)
            {
                if (held && hold)
                {
                    ready_since = {};
                    return;
                }
                if (ready_since == std::chrono::steady_clock::time_point{})
                    ready_since = now;
                else if (now - ready_since >= std::chrono::seconds(1))
                {
                    fallback = false;
                    held = false;
                    ready_since = {};
                }
            }
            if (!fallback)
                relief_was_ready = true;
        }
    };

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

    template <class CheckHeight>
    bool check_relief_clutter(const std::vector<sl::ClutterObstacle> &objects, const glm::dquat &world_to_tile,
                              double radius, const glm::dmat4 &view_projection, CheckHeight &&check_height,
                              std::string &reason)
    {
        for (const sl::ClutterObstacle &object : objects)
        {
            const double distance = glm::length(object.position);
            if (!std::isfinite(distance) || distance < 1.0 || !std::isfinite(object.radius) ||
                !std::isfinite(object.height) || object.radius < 0.0 || object.height < 0.0)
            {
                reason = "clutter: invalid position or dimensions";
                return false;
            }
            const glm::dvec3 direction = world_to_tile * (object.position / distance);
            const double extent = (object.radius + object.height) / radius;
            const glm::dvec4 clip = view_projection * glm::dvec4(direction * ((distance + object.height * 0.5) / radius), 1.0);
            bool outside = false;
            for (int axis = 0; axis < 3; ++axis)
            {
                for (double sign : {-1.0, 1.0})
                {
                    const glm::dvec3 plane(view_projection[0][3] + sign * view_projection[0][axis],
                                            view_projection[1][3] + sign * view_projection[1][axis],
                                            view_projection[2][3] + sign * view_projection[2][axis]);
                    if (clip.w + sign * clip[axis] < -extent * glm::length(plane))
                        outside = true;
                }
            }
            if (!outside && !check_height(direction, distance - radius, "clutter"))
                return false;
        }
        return true;
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
            if (distance - radius - expected <= relief_near_surface_height_m && !check_height(direction, expected, "ship gear"))
                return false;
        }
        return true;
    }
}
