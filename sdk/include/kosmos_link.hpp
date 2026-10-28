// C++ convenience wrapper over kosmos_link.h. Header only; link it with kosmos_link.c (or the kosmos_link library).
#ifndef KOSMOS_LINK_HPP
#define KOSMOS_LINK_HPP

#include "kosmos_link.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace kosmos
{
    class Link
    {
    public:
        using ValueFn = std::function<void(const std::string &path, const kl_value &value)>;
        using EventFn = std::function<void(const std::string &name, const std::string &arg)>;

        /** Connects using the environment Kosmos sets when it starts your program; @p id and @p name describe the addon. Throws on failure. */
        Link(const char *id, const char *name, unsigned capabilities, const char *version = "1.0")
        {
            kl_options o;
            kl_options_from_env(&o);
            o.addon_id = id;
            o.name = name;
            o.version = version;
            o.capabilities = capabilities;
            char err[256] = "";
            client_ = kl_connect(&o, err, sizeof err);
            if (!client_)
                throw std::runtime_error(err);
            kl_set_value_handler(client_, [](const char *p, const kl_value *v, void *u) {
                Link *self = static_cast<Link *>(u);
                if (self->on_value)
                    self->on_value(p, *v);
            }, this);
            kl_set_event_handler(client_, [](const char *n, const char *a, void *u) {
                Link *self = static_cast<Link *>(u);
                if (self->on_event)
                    self->on_event(n, a ? a : "");
            }, this);
        }
        ~Link() { kl_close(client_); }
        Link(const Link &) = delete;
        Link &operator=(const Link &) = delete;

        ValueFn on_value;
        EventFn on_event;

        kl_client *raw() { return client_; }
        unsigned granted() const { return kl_granted(client_); }

        /** Waits up to @p timeout_ms for messages and runs the handlers; false once the connection is gone. */
        bool poll(int timeout_ms = 0) { return kl_poll(client_, timeout_ms) >= 0; }

        void subscribe(const std::string &path, int hz = 10) { kl_subscribe(client_, path.c_str(), hz); }
        double get(const std::string &path, double fallback = 0.0)
        {
            kl_value v;
            if (kl_get(client_, path.c_str(), &v) != KL_OK)
                return fallback;
            return v.type == KL_DOUBLE ? v.real : v.type == KL_INT ? v.integer : v.type == KL_BOOL ? v.boolean : fallback;
        }
        std::string get_string(const std::string &path)
        {
            kl_value v;
            return kl_get(client_, path.c_str(), &v) == KL_OK && v.type == KL_STRING ? v.string : "";
        }
        void set(const std::string &path, double value) { kl_set_double(client_, path.c_str(), value); }
        void set(const std::string &path, bool value) { kl_set_bool(client_, path.c_str(), value); }
        void set(const std::string &path, const std::string &value) { kl_set_string(client_, path.c_str(), value.c_str()); }
        /** Weather at a place (body "" is the flown ship's); false when the body has none or the request failed. */
        bool weather_at(double latitude, double longitude, double altitude, kl_weather &out, const std::string &body = "") { return kl_weather_at(client_, body.c_str(), latitude, longitude, altitude, &out) == KL_OK; }
        bool set_weather(const std::string &mode, const std::string &arg = "") { return kl_set_weather(client_, mode.c_str(), arg.c_str()) == KL_OK; }
        bool subscribe_event(const std::string &name) { return kl_subscribe_event(client_, name.c_str()) == KL_OK; }
        std::vector<kl_ship_info> ships(double radius = 0.0, bool buildings = false)
        {
            std::vector<kl_ship_info> out(256);
            int n = 0;
            if (kl_list_ships(client_, radius, buildings, out.data(), static_cast<int>(out.size()), &n) != KL_OK)
                n = 0;
            out.resize(static_cast<std::size_t>(n));
            return out;
        }
        std::vector<kl_facility> facilities(double latitude, double longitude, double radius_km = 0.0, const std::string &kind = "", int max = 20, const std::string &body = "")
        {
            std::vector<kl_facility> out(static_cast<std::size_t>(max > 0 ? max : 1));
            int n = 0;
            if (kl_list_facilities(client_, kind.c_str(), body.c_str(), latitude, longitude, radius_km, out.data(), static_cast<int>(out.size()), &n) != KL_OK)
                n = 0;
            out.resize(static_cast<std::size_t>(n));
            return out;
        }
        void log(const std::string &text) { kl_log(client_, text.c_str()); }
        void notify(const std::string &text, double seconds = 4.0) { kl_ui_notify(client_, text.c_str(), seconds); }
        /** Lockstep: wait for the "step" event, compute, then step_done(frame). See the SDK manual. */
        bool lockstep(bool enable, int max_wait_ms = 100, double fixed_dt = 0.0) { return kl_lockstep(client_, enable, max_wait_ms, fixed_dt) == KL_OK; }
        bool fire(int ship, const std::string &group, bool on) { return kl_fire(client_, ship, group.c_str(), on ? 1 : 0) == KL_OK; }
        void step_done(unsigned frame) { kl_step_done(client_, frame); }
        bool claim_ship(unsigned index, bool passthrough = false) { return kl_claim_ship(client_, index, passthrough) == KL_OK; }
        bool release_ship(unsigned index) { return kl_release_ship(client_, index) == KL_OK; }
        void ship_value(unsigned index, const std::string &name, double value) { kl_ship_value(client_, index, name.c_str(), value); }
        void ship_force(unsigned index, const double force[3], const double torque[3]) { kl_ship_force(client_, index, force, torque); }

    private:
        kl_client *client_ = nullptr;
    };
} // namespace kosmos

#endif
