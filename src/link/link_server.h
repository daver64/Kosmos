#pragma once

#include "link_protocol.h"
#include "sl.h"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kosmos::link
{
    /** What the game does for an addon's requests that go beyond the property tree. Each returns an error message, or empty on success. */
    class LinkHost
    {
    public:
        virtual ~LinkHost() = default;
        virtual std::string sim_version() { return "unknown"; }
        /** Adds key actions from a keys.json document. */
        virtual std::string define_keys(const std::string &json) { (void)json; return "not supported"; }
        /** Spawns a ship next to the flown one, offset in the flown ship's axes (metres). */
        virtual std::string spawn_ship(const std::string &name, double x, double y, double z, std::uint32_t &handle) { (void)name; (void)x; (void)y; (void)z; handle = 0; return "not supported"; }
        virtual std::string release_ordnance(int hardpoint) { (void)hardpoint; return "not supported"; }
        virtual std::string give_equipment(const std::string &id, int count) { (void)id; (void)count; return "not supported"; }
        /** Adds an MFD page. The page's drawing arrives through LinkServer::page(). */
        virtual std::string define_mfd(const std::string &addon, const std::string &id, const std::string &title) { (void)addon; (void)id; (void)title; return "not supported"; }
        virtual void log(const std::string &addon, const std::string &text) { (void)addon; (void)text; }
    };

    /** Decides what an addon may do: asked every poll so that a change the player makes takes effect at once. */
    class LinkPolicy
    {
    public:
        virtual ~LinkPolicy() = default;
        /** The capabilities to grant @p addon, which asked for @p requested; @p token is the one it presented (empty when none). */
        virtual std::uint32_t granted(const std::string &addon, const std::string &token, std::uint32_t requested) = 0;
        /** Called when an addon says hello, so a window can list it. */
        virtual void seen(const std::string &addon, const std::string &name, std::uint32_t requested) { (void)addon; (void)name; (void)requested; }
    };

    struct ClientInfo
    {
        int id = 0;
        std::string addon, name, version;
        std::uint32_t requested = 0, granted = 0;
        std::size_t subscriptions = 0;
    };

    /** An MFD page an addon draws. */
    struct Page
    {
        std::string addon, id;
        std::vector<DrawCommand> commands;
        std::chrono::steady_clock::time_point wanted{}, notified{};
        bool drawn = false;
    };

    /**
     * The game side of KosmosLink: listens on the loopback interface, speaks the protocol of link_protocol.h to any number of
     * addon programs, and reads and writes the property tree on their behalf. Everything runs on the thread that calls poll().
     */
    class LinkServer
    {
    public:
        LinkServer(sl::PropertyTree &props, LinkHost &host, LinkPolicy *policy);
        ~LinkServer();

        /** Starts listening on 127.0.0.1:@p port (0 picks a free port). */
        bool start(std::uint16_t port);
        void stop();
        bool running() const;
        std::uint16_t port() const;

        /** Accepts connections, reads and answers requests, and tells clients whose grants changed. */
        void poll();
        /** Pushes changed subscribed properties and asks addons to redraw the pages that are on screen. */
        void flush();
        void broadcast_event(const std::string &name, const std::string &arg);

        std::vector<ClientInfo> clients() const;
        /** The page an addon draws, or null. */
        Page *page(const std::string &addon, const std::string &id);
        /** Whether the addon that owns a page is still connected. */
        bool page_live(const std::string &addon) const;

    private:
        struct Client;
        void handle(Client &c, const Frame &f);
        void reply_error(Client &c, std::uint32_t request, ErrorCode code, const std::string &message);
        void reply_ok(Client &c, std::uint32_t request, std::uint32_t handle);
        void send(Client &c, const std::string &frame);
        Value read(const std::string &path) const;
        std::string write(const Client &c, const std::string &path, const Value &value);
        bool may_write(const Client &c, const std::string &path) const;

        sl::PropertyTree &props_;
        LinkHost &host_;
        LinkPolicy *policy_;
        sl::TcpListener listener_;
        std::vector<std::unique_ptr<Client>> clients_;
        std::map<std::string, Page> pages_;
        int next_id_ = 1;
    };
} // namespace kosmos::link
