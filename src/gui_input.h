#pragma once

#include "event.h"
#include "gui.h"

namespace kosmos::app
{
    inline bool gui_input_release_event(sl::Event::Type type)
    {
        return type == sl::Event::Type::mouse_button_up || type == sl::Event::Type::key_up ||
               type == sl::Event::Type::window || type == sl::Event::Type::window_resized;
    }

    inline void sync_gui_input_during_transition(const sl::Event &event)
    {
        if (gui_input_release_event(event.type()))
            sl::gui_handle_event(event);
    }
}
