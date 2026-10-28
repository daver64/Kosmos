#pragma once

#include <cstdint>
#include <vector>

namespace kosmos
{
    /**
     * A beam pulse as a mono WAV in memory: a deep whump as it fires, then an electrical hum that swells and dies away over
     * `seconds`. `pitch` scales the hum (1 is about 55 Hz).
     */
    std::vector<std::uint8_t> make_beam_wav(double seconds, double pitch);

    /** One sparkle-gun shot as a mono WAV: a bright, falling plasma zap with a short crackle. */
    std::vector<std::uint8_t> make_zap_wav();
} // namespace kosmos
