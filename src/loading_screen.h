#pragma once

#include "app.h"

namespace kosmos::app
{
    // Shown while the world loads: a dim, hazy Martian sky with what is happening printed large in orange, latest line biggest.
    struct LoadingScreen
    {
        sl::Font *big = nullptr, *small = nullptr, *title = nullptr;
        std::vector<std::string> lines;
        bool active = false;
        bool started = false;
        bool quit_requested = false;
        bool left_alt = false, right_alt = false;
        std::uint32_t last_poll = 0;

        void begin()
        {
            lines.clear();
            started = true;
            quit_requested = false;
            left_alt = sl::key_down(sl::Key::left_alt);
            right_alt = sl::key_down(sl::Key::right_alt);
            last_poll = sl::time_ms();
            big = sl::open_sans_font(44);
            small = sl::open_sans_font(26);
            title = sl::open_sans_font(76);
            active = big && small && title;
            if (!active)
                std::fprintf(stderr, "[kosmos] could not create loading screen fonts: %s\n", sl::last_error().c_str());
            sl::set_text_panel_style({false});
            note("Starting up");
        }

        void end()
        {
            if (!started)
                return;
            started = false;
            active = false;
            sl::set_text_panel_style({true});
            for (sl::Font *f : {big, small, title})
                if (f)
                    sl::close_font(f);
            big = small = title = nullptr;
        }

        void note(const std::string &text)
        {
            std::fprintf(stderr, "[kosmos] %s\n", text.c_str());
            lines.push_back(text);
            if (lines.size() > 6)
                lines.erase(lines.begin());
            pump(true);
        }

        void pump(bool force = false)
        {
            const std::uint32_t now = sl::time_ms();
            if (!force && now - last_poll < 50)
                return;
            last_poll = now;
            bool redraw = force;
            sl::Event event;
            while (sl::poll_event(&event))
            {
                if (event.type() == sl::Event::Type::key_down || event.type() == sl::Event::Type::key_up)
                {
                    if (event.key() == sl::Event::Key::left_alt)
                        left_alt = event.type() == sl::Event::Type::key_down;
                    else if (event.key() == sl::Event::Key::right_alt)
                        right_alt = event.type() == sl::Event::Type::key_down;
                }
                if (event.type() == sl::Event::Type::quit)
                    quit_requested = true;
                else if (event.type() == sl::Event::Type::key_down && !event.key_repeat() &&
                         (event.key() == sl::Event::Key::return_key || event.key() == sl::Event::Key::keypad_enter) &&
                         (left_alt || right_alt))
                {
                    if (!sl::toggle_fullscreen())
                        std::fprintf(stderr, "[kosmos] could not toggle fullscreen: %s\n", sl::last_error().c_str());
                    redraw = true;
                }
                if (event.type() == sl::Event::Type::window_resized || event.type() == sl::Event::Type::window)
                    redraw = true;
                sl::display_handle_event(event);
            }
            if (!active || !redraw)
                return;
            draw();
            sl::show_video_bitmap();
            sl::end_frame();
        }

        void draw() const
        {
            const int w = sl::screen_width(), h = sl::screen_height();
            // Dusty sky, palest at the horizon, over dim rust ground.
            const int horizon = static_cast<int>(h * 0.68f);
            const int bands = 72;
            for (int i = 0; i < bands; ++i)
            {
                const float t = static_cast<float>(i) / (bands - 1);
                const float y0 = horizon * t, y1 = horizon * (t + 1.0f / (bands - 1)) + 1.0f;
                const float k = t * t;
                sl::rectfill(sl::screen, 0, y0, static_cast<float>(w), y1,
                             {static_cast<std::uint8_t>(8 + 30 * k), static_cast<std::uint8_t>(6 + 19 * k), static_cast<std::uint8_t>(6 + 14 * k), 255});
            }
            for (int i = 0; i < 24; ++i)
            {
                const float t = static_cast<float>(i) / 23.0f;
                sl::rectfill(sl::screen, 0, horizon + (h - horizon) * t, static_cast<float>(w), horizon + (h - horizon) * (t + 1.0f / 23.0f) + 1.0f,
                             {static_cast<std::uint8_t>(26 - 18 * t), static_cast<std::uint8_t>(16 - 11 * t), static_cast<std::uint8_t>(12 - 8 * t), 255});
            }
            // Haze: broad soft glows, a pale low sun and a band of dust on the horizon.
            sl::set_blend_mode(sl::BlendMode::additive);
            for (int i = 0; i < 28; ++i)
            {
                const float r = w * (0.34f - 0.011f * i);
                sl::circlefill(sl::screen, w * 0.78f, horizon * 0.82f, r, {2, 1, 1, 255});
            }
            for (int i = 0; i < 16; ++i)
                sl::rectfill(sl::screen, 0, horizon - h * 0.015f * (16 - i), static_cast<float>(w), horizon + h * 0.012f, {1, 1, 0, 255});
            sl::set_blend_mode(sl::BlendMode::normal);

            const int left = static_cast<int>(w * 0.09f);
            sl::textout(title, left, static_cast<int>(h * 0.12f), {255, 214, 110, 255}, "KOSMOS " KOSMOS_VERSION);
            const int big_h = sl::text_height(big), small_h = sl::text_height(small);
            int y = static_cast<int>(h * 0.12f) + sl::text_height(title) + static_cast<int>(h * 0.06f);
            for (std::size_t i = 0; i < lines.size(); ++i)
            {
                const bool latest = i + 1 == lines.size();
                if (latest)
                    sl::textout(big, left, y, {255, 204, 66, 255}, lines[i] + " ...");
                else
                {
                    const float age = static_cast<float>(lines.size() - 1 - i);
                    const std::uint8_t a = static_cast<std::uint8_t>(std::max(0.0f, 190.0f - age * 32.0f));
                    sl::textout(small, left, y, {static_cast<std::uint8_t>(235 * a / 255 + 20), static_cast<std::uint8_t>(180 * a / 255 + 14), static_cast<std::uint8_t>(60 * a / 255 + 6), 255}, lines[i]);
                }
                y += static_cast<int>((latest ? big_h : small_h) * 1.2f);
            }
        }
    };

    extern LoadingScreen loading;
} // namespace kosmos::app
