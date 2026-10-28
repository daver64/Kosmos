// C++ convenience wrapper over kosmos_link.h. Header only; link it with kosmos_link.c (or the kosmos_link library).
#ifndef KOSMOS_LINK_HPP
#define KOSMOS_LINK_HPP

#include "kosmos_link.h"

#include <functional>
#include <stdexcept>
#include <string>

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
        void log(const std::string &text) { kl_log(client_, text.c_str()); }

    private:
        kl_client *client_ = nullptr;
    };
} // namespace kosmos

#endif
