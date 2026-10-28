#pragma once

// KosmosLink wire protocol, version 2 (version 1 clients are still accepted; they just never use the later requests). The same layout is implemented by the SDK clients (sdk/src/kosmos_link.c and
// sdk/docs/PROTOCOL.md). All integers are little-endian.
//
//   frame   = u32 length (bytes after this field), u16 type, u32 request id, payload
//   string  = u16 byte count, UTF-8 bytes
//   value   = u8 tag (0 none, 1 bool, 2 int32, 3 double, 4 string), then u8 / i32 / f64 / string

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace kosmos::link
{
    constexpr std::uint16_t protocol_version = 3;
    constexpr std::uint16_t min_protocol_version = 1;
    constexpr std::uint16_t default_port = 9420;
    constexpr std::size_t max_frame = 1u << 20;

    enum MessageType : std::uint16_t
    {
        // addon to Kosmos
        msg_hello = 1,
        msg_subscribe = 2,
        msg_unsubscribe = 3,
        msg_get = 4,
        msg_set = 5,
        msg_define_key = 6,
        msg_spawn_ship = 7,
        msg_release = 8,
        msg_give = 9,
        msg_define_mfd = 10,
        msg_draw = 11,
        msg_log = 12,
        msg_ping = 13,
        // protocol 2
        msg_weather_at = 14,
        msg_set_weather = 15,
        msg_subscribe_event = 16,
        msg_unsubscribe_event = 17,
        msg_list_ships = 18,
        msg_remove_ship = 19,
        msg_place_ship = 20,
        msg_list_facilities = 21,
        // protocol 3
        msg_ui_window = 22,
        msg_ui_close = 23,
        msg_ui_notify = 24,
        msg_lockstep = 25,
        msg_step_done = 26,
        msg_claim_ship = 27,
        msg_release_ship = 28,
        msg_ship_value = 29,
        msg_ship_force = 30,
        msg_draw3d = 31,
        msg_fire = 32,
        // Kosmos to addon
        msg_welcome = 101,
        msg_error = 102,
        msg_value = 103,
        msg_event = 104,
        msg_ok = 105,
        msg_pong = 106,
        msg_weather = 107,
        msg_ship_list = 108,
        msg_facility_list = 109
    };

    /** What an addon may do; the user grants these per addon. */
    enum Capability : std::uint32_t
    {
        cap_read = 1,     // get and subscribe to properties
        cap_controls = 2, // set /controls/ and the addon's own /addons/<id>/ properties
        cap_write = 4,    // set any property
        cap_spawn = 8,    // spawn ships, release ordnance, fire ship weapons, give equipment
        cap_draw = 16,    // add MFD modes and draw on them
        cap_keys = 32,    // define key actions
        cap_world = 64,   // set the weather, place and remove ships
        cap_ui = 128,     // notices and small windows
        cap_vessel = 256, // own the behaviour of a ship
        cap_draw3d = 512, // lines, markers and labels in the 3D view
        cap_all = 1023
    };

    enum ErrorCode : std::uint16_t
    {
        err_bad_request = 1,
        err_unknown = 2,
        err_denied = 3,
        err_failed = 4,
        err_version = 5
    };

    enum ValueTag : std::uint8_t
    {
        tag_none = 0,
        tag_bool = 1,
        tag_int = 2,
        tag_double = 3,
        tag_string = 4
    };

    /** A property value on the wire. */
    struct Value
    {
        ValueTag tag = tag_none;
        bool b = false;
        std::int32_t i = 0;
        double d = 0.0;
        std::string s;
        bool operator==(const Value &o) const
        {
            if (tag != o.tag)
                return false;
            switch (tag)
            {
            case tag_bool:
                return b == o.b;
            case tag_int:
                return i == o.i;
            case tag_double:
                return d == o.d;
            case tag_string:
                return s == o.s;
            default:
                return true;
            }
        }
    };

    /** One retained drawing command of a native MFD page (units are the 400 x 400 MFD screen). */
    struct DrawCommand
    {
        std::uint8_t kind = 0; // 0 line, 1 rect, 2 fillrect, 3 circle, 4 disc, 5 tri, 6 text, 7 image
        float a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
        std::uint8_t r = 120, g = 255, bl = 140, align = 0;
        std::string text;
    };

    /** One widget of a native window. kind: 0 label, 1 button, 2 checkbox, 3 slider, 4 text input, 5 separator. */
    struct UiWidget
    {
        std::uint8_t kind = 0;
        std::string id, label, text;
        double value = 0, low = 0, high = 1;
    };

    /** One primitive of a 3D layer. kind: 0 line (a to b), 1 marker (at a), 2 label (at a), 3 ring (flat, around a), 4 sphere (three rings about a). frame: 0 lat/lon/alt, 1 body-fixed metres, 2 ship-local metres. */
    struct Primitive3
    {
        std::uint8_t kind = 0, frame = 0, r = 255, g = 255, bl = 255, alpha = 255, depth = 1;
        std::uint32_t ship = 0;
        double a[3] = {0, 0, 0}, b[3] = {0, 0, 0}, size = 1;
        std::string text;
    };

    class Writer
    {
    public:
        Writer(std::uint16_t type, std::uint32_t request)
        {
            u32(0);
            u16(type);
            u32(request);
        }
        Writer &u8(std::uint8_t v)
        {
            buf_.push_back(static_cast<char>(v));
            return *this;
        }
        Writer &u16(std::uint16_t v)
        {
            for (int n = 0; n < 2; ++n)
                buf_.push_back(static_cast<char>((v >> (8 * n)) & 0xff));
            return *this;
        }
        Writer &u32(std::uint32_t v)
        {
            for (int n = 0; n < 4; ++n)
                buf_.push_back(static_cast<char>((v >> (8 * n)) & 0xff));
            return *this;
        }
        Writer &f32(float v)
        {
            std::uint32_t bits;
            std::memcpy(&bits, &v, 4);
            return u32(bits);
        }
        Writer &f64(double v)
        {
            std::uint64_t bits;
            std::memcpy(&bits, &v, 8);
            u32(static_cast<std::uint32_t>(bits));
            return u32(static_cast<std::uint32_t>(bits >> 32));
        }
        Writer &str(const std::string &s)
        {
            const std::size_t n = s.size() > 65535 ? 65535 : s.size();
            u16(static_cast<std::uint16_t>(n));
            buf_.append(s, 0, n);
            return *this;
        }
        Writer &value(const Value &v)
        {
            u8(v.tag);
            switch (v.tag)
            {
            case tag_bool:
                return u8(v.b ? 1 : 0);
            case tag_int:
                return u32(static_cast<std::uint32_t>(v.i));
            case tag_double:
                return f64(v.d);
            case tag_string:
                return str(v.s);
            default:
                return *this;
            }
        }
        /** The finished frame with its length filled in. */
        std::string finish()
        {
            const std::uint32_t n = static_cast<std::uint32_t>(buf_.size() - 4);
            for (int k = 0; k < 4; ++k)
                buf_[static_cast<std::size_t>(k)] = static_cast<char>((n >> (8 * k)) & 0xff);
            return buf_;
        }

    private:
        std::string buf_;
    };

    /** Reads a payload; any read past the end sets ok() to false and returns zeros. */
    class Reader
    {
    public:
        Reader(const char *data, std::size_t size) : p_(data), n_(size) {}
        bool ok() const { return ok_; }
        std::size_t remaining() const { return n_ - pos_; }
        std::uint8_t u8()
        {
            return take(1) ? static_cast<std::uint8_t>(p_[pos_ - 1]) : 0;
        }
        std::uint16_t u16()
        {
            if (!take(2))
                return 0;
            return static_cast<std::uint16_t>(byte(pos_ - 2) | (byte(pos_ - 1) << 8));
        }
        std::uint32_t u32()
        {
            if (!take(4))
                return 0;
            std::uint32_t v = 0;
            for (int k = 0; k < 4; ++k)
                v |= static_cast<std::uint32_t>(byte(pos_ - 4 + static_cast<std::size_t>(k))) << (8 * k);
            return v;
        }
        float f32()
        {
            const std::uint32_t bits = u32();
            float v;
            std::memcpy(&v, &bits, 4);
            return v;
        }
        double f64()
        {
            const std::uint64_t lo = u32();
            const std::uint64_t hi = u32();
            const std::uint64_t bits = lo | (hi << 32);
            double v;
            std::memcpy(&v, &bits, 8);
            return v;
        }
        std::string str()
        {
            const std::uint16_t len = u16();
            if (!take(len))
                return {};
            return std::string(p_ + pos_ - len, len);
        }
        Value value()
        {
            Value v;
            const std::uint8_t t = u8();
            switch (t)
            {
            case tag_bool:
                v.tag = tag_bool;
                v.b = u8() != 0;
                break;
            case tag_int:
                v.tag = tag_int;
                v.i = static_cast<std::int32_t>(u32());
                break;
            case tag_double:
                v.tag = tag_double;
                v.d = f64();
                break;
            case tag_string:
                v.tag = tag_string;
                v.s = str();
                break;
            case tag_none:
                break;
            default:
                ok_ = false;
            }
            return v;
        }

    private:
        bool take(std::size_t k)
        {
            if (!ok_ || pos_ + k > n_)
            {
                ok_ = false;
                return false;
            }
            pos_ += k;
            return true;
        }
        std::uint8_t byte(std::size_t at) const { return static_cast<std::uint8_t>(p_[at]); }
        const char *p_;
        std::size_t n_, pos_ = 0;
        bool ok_ = true;
    };

    /** A frame cut from a byte stream. */
    struct Frame
    {
        std::uint16_t type = 0;
        std::uint32_t request = 0;
        std::string payload;
    };

    /** Takes one whole frame from the front of @p stream. Returns 1 for a frame, 0 when more bytes are needed, -1 when the stream is invalid. */
    inline int take_frame(std::string &stream, Frame &out)
    {
        if (stream.size() < 4)
            return 0;
        Reader r(stream.data(), 4);
        const std::uint32_t len = r.u32();
        if (len < 6 || len > max_frame)
            return -1;
        if (stream.size() < 4 + static_cast<std::size_t>(len))
            return 0;
        Reader h(stream.data() + 4, 6);
        out.type = h.u16();
        out.request = h.u32();
        out.payload.assign(stream, 10, len - 6);
        stream.erase(0, 4 + static_cast<std::size_t>(len));
        return 1;
    }
} // namespace kosmos::link
