#pragma once
#include <algorithm>
#include <cstring>
#include <imgui.h>

// Sliders with arrow buttons beside them, so a value can be nudged one step at a time.
namespace ui
{
inline float step_for(const char *format)
{
    const char *dot = format ? std::strchr(format, '.') : nullptr;
    if (!dot || dot[1] < '0' || dot[1] > '9')
        return 1.0f;
    float step = 1.0f;
    for (int i = 0; i < dot[1] - '0'; ++i)
        step *= 0.1f;
    return step;
}

template <class T, class SliderFn> inline bool stepped(const char *label, T *v, T low, T high, T step, SliderFn slider)
{
    const char *hidden = std::strstr(label, "##");
    ImGui::PushID(label);
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x, button = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(std::max(40.0f, ImGui::CalcItemWidth() - 2.0f * (button + spacing)));
    bool changed = slider();
    ImGui::SameLine(0.0f, spacing);
    if (ImGui::ArrowButton("##less", ImGuiDir_Left))
    {
        *v = std::max(low, static_cast<T>(*v - step));
        changed = true;
    }
    ImGui::SameLine(0.0f, spacing);
    if (ImGui::ArrowButton("##more", ImGuiDir_Right))
    {
        *v = std::min(high, static_cast<T>(*v + step));
        changed = true;
    }
    if (!hidden || hidden != label)
    {
        ImGui::SameLine(0.0f, spacing);
        ImGui::TextUnformatted(label, hidden);
    }
    ImGui::PopID();
    return changed;
}

inline bool slider_float(const char *label, float *v, float low, float high, const char *format = "%.3f")
{
    return stepped<float>(label, v, low, high, step_for(format), [&] { return ImGui::SliderFloat("##slider", v, low, high, format); });
}

inline bool slider_int(const char *label, int *v, int low, int high, const char *format = "%d")
{
    return stepped<int>(label, v, low, high, 1, [&] { return ImGui::SliderInt("##slider", v, low, high, format); });
}
} // namespace ui
