#include "ship.h"
#include "ship_internal.h"

#include <algorithm>
#include <cmath>

namespace kosmos
{
    using namespace ship_detail;

    void Ship::publish(sl::PropertyTree &tree, const std::string &base)
    {
        unpublish();
        tree_ = &tree;
        base_ = base;
        const auto num = [&](const char *path, std::function<double()> getter) { tree.node(base + "/" + path)->tie_double(std::move(getter)); };
        tree.node(base + "/name")->tie_string([this] { return name; });
        tree.node(base + "/class")->tie_string([this] { return spec().name; });
        tree.node(base + "/gear-position")->tie_double([this] { return gear_position_; });
        tree.node(base + "/shield")->tie_double([this] { return spec_.shield.capacity > 0.0 ? shield_ / spec_.shield.capacity : 0.0; });
        tree.node(base + "/health")->tie_double([this] { return health_; });
        tree.node(base + "/thermal/temp")->tie_double([this] { return thermal_.temp; });
        tree.node(base + "/thermal/flux")->tie_double([this] { return thermal_.flux; });
        tree.node(base + "/thermal/peak-flux")->tie_double([this] { return thermal_.peak_flux; });
        tree.node(base + "/thermal/peak-temp")->tie_double([this] { return thermal_.peak_temp; });
        tree.node(base + "/thermal/shield")->tie_double([this] { return std::max(thermal_.shield, 0.0); });
        tree.node(base + "/thermal/blackout")->tie_bool([this] { return thermal_.blackout; });
        tree.node(base + "/chutes/open")->tie_int([this] {
            int n = 0;
            for (const ChuteRuntime &c : chutes_)
                n += c.state != ChuteState::stowed && c.state != ChuteState::gone ? 1 : 0;
            return n;
        });
        tree.node(base + "/chutes/stowed")->tie_int([this] {
            int n = 0;
            for (const ChuteRuntime &c : chutes_)
                n += c.state == ChuteState::stowed ? 1 : 0;
            return chutes_.empty() ? static_cast<int>(spec_.parachutes.size()) : n;
        });
        for (std::size_t i = 0; i < spec_.animation_controls.size(); ++i)
            tree.node(base + "/anim/" + spec_.animation_controls[i].property)->tie_double([this, i] { return animation_position(i); });
        tree.node(base + "/pod-angle-left")->tie_double([this] { return pod_angle_[0] / 3.14159265358979; });
        tree.node(base + "/pod-angle-right")->tie_double([this] { return pod_angle_[1] / 3.14159265358979; });
        tree.node(base + "/stage")->tie_int([this] { return active_stage_; });
        tree.node(base + "/stage-count")->tie_int([this] { return stage_count(); });
        tree.node(base + "/crashed")->tie_bool([this] { return crashed_; });
        tree.node(base + "/landed")->tie_bool([this] { return telemetry_.landed; });
        tree.node(base + "/on-rails")->tie_bool([this] { return on_rails(); });
        tree.node(base + "/soi")->tie_string([this] { return universe_.bodies[static_cast<std::size_t>(telemetry_.soi)].name; });
        num("x", [this] { return body().position.x; });
        num("y", [this] { return body().position.y; });
        num("z", [this] { return body().position.z; });
        num("vx", [this] { return body().velocity.x; });
        num("vy", [this] { return body().velocity.y; });
        num("vz", [this] { return body().velocity.z; });
        num("altitude", [this] { return telemetry_.altitude; });
        num("speed", [this] { return telemetry_.surface_speed; });
        num("orbital-speed", [this] { return telemetry_.orbital_speed; });
        num("vertical-speed", [this] { return telemetry_.vertical_speed; });
        num("pitch", [this] { return telemetry_.pitch; });
        num("heading", [this] { return telemetry_.heading; });
        num("roll", [this] { return telemetry_.roll; });
        num("alpha", [this] { return telemetry_.alpha; });
        // Body rates in degrees per second: nose up, nose right and right bank are positive.
        num("rate-pitch", [this] { return body().angular_velocity.x * 180.0 / pi; });
        num("rate-yaw", [this] { return -body().angular_velocity.y * 180.0 / pi; });
        num("rate-roll", [this] { return -body().angular_velocity.z * 180.0 / pi; });
        num("sideslip", [this] { return telemetry_.sideslip; });
        num("dynamic-pressure", [this] { return telemetry_.dynamic_pressure; });
        num("flight-path", [this] { return telemetry_.flight_path; });
        num("mass", [this] { return telemetry_.mass; });
        num("thrust", [this] { return telemetry_.thrust; });
        num("twr", [this] { return telemetry_.twr; });
        num("max-thrust", [this] { return telemetry_.max_thrust; });
        num("acceleration-g", [this] { return telemetry_.acceleration_g; });
        num("stage-fuel", [this] { return telemetry_.stage_fuel; });
        num("stage-fuel-capacity", [this] { return active_stage_ < static_cast<int>(spec_.stages.size()) ? spec_.stages[static_cast<std::size_t>(active_stage_)].propellant : 0.0; });
        num("stage-fuel-percent", [this] {
            const double cap = active_stage_ < static_cast<int>(spec_.stages.size()) ? spec_.stages[static_cast<std::size_t>(active_stage_)].propellant : 0.0;
            return cap > 0.0 ? 100.0 * telemetry_.stage_fuel / cap : 0.0;
        });
        num("rcs-fuel-percent", [this] { return spec_.rcs_propellant > 0.0 ? 100.0 * rcs_fuel_ / spec_.rcs_propellant : 0.0; });
        num("stage-count", [this] { return static_cast<double>(spec_.stages.size()); });
        num("stage-index", [this] { return static_cast<double>(active_stage_); });
        for (int i = 0; i < 8; ++i)
        {
            const std::string at = "stage[" + std::to_string(i) + "]/";
            tree.node(base + "/" + at + "name")->tie_string([this, i] { return i < static_cast<int>(spec_.stages.size()) ? spec_.stages[static_cast<std::size_t>(i)].name : std::string(); });
            num((at + "capacity").c_str(), [this, i] { return i < static_cast<int>(spec_.stages.size()) ? spec_.stages[static_cast<std::size_t>(i)].propellant : 0.0; });
            num((at + "fuel").c_str(), [this, i] {
                const auto &tanks = body().tanks;
                return i < static_cast<int>(tanks.size()) && i >= active_stage_ ? tanks[static_cast<std::size_t>(i)].mass : 0.0;
            });
        }
        num("fuel-flow", [this] { return fuel_flow_; });
        num("rcs-fuel", [this] { return rcs_fuel_; });
        num("rcs-fuel-capacity", [this] { return spec_.rcs_propellant; });
        for (const char *role : {"main-left", "main-right", "hover-front", "hover-aft", "pod-left", "pod-right"})
            for (const char *what : {"thrust", "level", "flow", "accel"})
                tree.node(base + "/engine/" + role + "/" + what)->tie_double([this, role = std::string(role), what = std::string(what)] { return engine_value(role, what); });
        num("rcs-flow", [this] { return rcs_flow_; });
        num("total-fuel", [this] { return telemetry_.total_fuel; });
        num("orbit/apoapsis", [this] { return telemetry_.apoapsis; });
        num("orbit/periapsis", [this] { return telemetry_.periapsis; });
        num("orbit/eccentricity", [this] { return telemetry_.eccentricity; });
        num("orbit/inclination", [this] { return telemetry_.inclination; });
        num("latitude", [this] { return telemetry_.latitude; });
        num("map/points", [this] { return static_cast<double>(track_.count); });
        num("map/impact", [this] { return track_.impact ? 1.0 : 0.0; });
        num("map/impact-latitude", [this] { return track_.impact_lat; });
        num("map/impact-longitude", [this] { return track_.impact_lon; });
        num("map/impact-time", [this] { return track_.impact_time; });
        num("map/body-spin", [this] { return universe_.bodies[static_cast<std::size_t>(telemetry_.soi)].spin; });
        for (int i = 0; i <= track_points; ++i)
        {
            const std::string at = "map/point[" + std::to_string(i) + "]/";
            num((at + "lat").c_str(), [this, i] { return track_.lat[i]; });
            num((at + "lon").c_str(), [this, i] { return track_.lon[i]; });
        }
        num("longitude", [this] { return telemetry_.longitude; });
        num("body-radius", [this] { return telemetry_.body_radius; });
        num("orbit/semi-major-axis", [this] { return telemetry_.semi_major_axis; });
        num("orbit/raan", [this] { return telemetry_.raan; });
        num("orbit/arg-periapsis", [this] { return telemetry_.arg_periapsis; });
        num("orbit/true-anomaly", [this] { return telemetry_.true_anomaly; });
        num("orbit/period", [this] { return telemetry_.period; });
        num("orbit/time-to-apoapsis", [this] { return telemetry_.time_to_apoapsis; });
        num("orbit/time-to-periapsis", [this] { return telemetry_.time_to_periapsis; });
        tree.node(base + "/orbit/bound")->tie_bool([this] { return telemetry_.bound; });
        num("position/x", [this] { return body().position.x; });
        num("position/y", [this] { return body().position.y; });
        num("position/z", [this] { return body().position.z; });
        num("velocity/x", [this] { return body().velocity.x; });
        num("velocity/y", [this] { return body().velocity.y; });
        num("velocity/z", [this] { return body().velocity.z; });
        if (systems_)
            systems_->publish(tree, base + "/systems");
    }

    void Ship::unpublish()
    {
        if (!tree_)
            return;
        // Tied nodes capture this ship, so drop the whole subtree. The base looks like /ships/ship[2].
        const std::size_t slash = base_.find_last_of('/');
        const std::string parent = base_.substr(0, slash);
        const std::string leaf = base_.substr(slash + 1);
        const std::size_t bracket = leaf.find('[');
        const std::string leaf_name = leaf.substr(0, bracket);
        const int index = bracket == std::string::npos ? 0 : std::stoi(leaf.substr(bracket + 1));
        if (sl::PropertyNode *p = tree_->find(parent.empty() ? "/" : parent))
            p->remove_child(leaf_name, index);
        tree_ = nullptr;
    }
} // namespace kosmos
