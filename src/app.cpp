#include "app.h"

namespace kosmos::app
{
    int screen_w = 1280;

    int screen_h = 720;

    std::string format_time(double seconds)
    {
        const long total = static_cast<long>(std::max(seconds, 0.0));
        char buffer[64];
        if (total >= 86400)
            std::snprintf(buffer, sizeof(buffer), "%ldd %02ld:%02ld:%02ld", total / 86400, (total / 3600) % 24, (total / 60) % 60, total % 60);
        else
            std::snprintf(buffer, sizeof(buffer), "%02ld:%02ld:%02ld", total / 3600, (total / 60) % 60, total % 60);
        return buffer;
    }

    std::string format_distance(double metres)
    {
        char buffer[48];
        const double a = std::fabs(metres);
        if (a >= 1.0e9)
            std::snprintf(buffer, sizeof(buffer), "%.2f Gm", metres / 1.0e9);
        else if (a >= 1.0e5)
            std::snprintf(buffer, sizeof(buffer), "%.0f km", metres / 1.0e3);
        else if (a >= 1.0e3)
            std::snprintf(buffer, sizeof(buffer), "%.2f km", metres / 1.0e3);
        else
            std::snprintf(buffer, sizeof(buffer), "%.0f m", metres);
        return buffer;
    }
} // namespace kosmos::app
