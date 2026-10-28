#pragma once

#include "link_server.h"

#include <string>
#include <vector>

namespace kosmos::link
{
    /** An installed addon folder. */
    struct AddonRef
    {
        std::string id, name, directory;
    };

    /** Capability names as written in addon.json ("read", "controls", "write", "spawn", "draw", "keys"). */
    std::uint32_t capability_from_name(const std::string &name);
    std::string capability_names(std::uint32_t caps);

    /** One addon with a native program, or an outside program that has connected. */
    struct NativeEntry
    {
        std::string id, name, directory;
        /** The program for this platform, from the addon.json "native" block; empty for programs started by hand. */
        std::string program;
        std::uint32_t caps = 0;
        bool autostart = true;
        bool restart = false;
        /** The player allows this addon to connect and (when it has a program) to run. Saved between runs. */
        bool allowed = false;
        bool running = false;
        /** A start was tried since the player last allowed or restarted it, so a failed program is not retried every second. */
        bool attempted = false;
        int restarts = 0;
        std::string status;
        std::string token;
        long long process = 0;
    };

    /**
     * Native addons: reads the "native" block of each addon.json, remembers which ones the player allows, starts and stops
     * the allowed programs, and answers the link server's questions about what a connected program may do.
     */
    class NativeAddons : public LinkPolicy
    {
    public:
        ~NativeAddons() override;

        /** Lists the addons that declare a native program. */
        void scan(const std::vector<AddonRef> &addons);
        void set_state_file(const std::string &path) { state_file_ = path; }
        void load();
        void save() const;
        /** The address launched programs are told to connect to. */
        void set_endpoint(std::uint16_t port) { port_ = port; }

        /** Start allowed programs that are not running and notice the ones that exited. Call about once a second. */
        void update();
        /** Stops every program this class started. */
        void shutdown();
        void set_allowed(const std::string &id, bool allowed);
        void restart(const std::string &id);
        std::vector<NativeEntry> &entries() { return entries_; }

        std::uint32_t granted(const std::string &addon, const std::string &token, std::uint32_t requested) override;
        void seen(const std::string &addon, const std::string &name, std::uint32_t requested) override;

    private:
        NativeEntry *find(const std::string &id);
        bool start(NativeEntry &entry);
        void stop(NativeEntry &entry);
        std::vector<NativeEntry> entries_;
        std::string state_file_;
        std::uint16_t port_ = default_port;
    };
} // namespace kosmos::link
