#pragma once

#include <cstdint>
#include <vector>

namespace kosmos
{
    /**
     * A blast as a mono WAV in memory. Kind 0 is a bomb: a sharp crack and a deep thump. Kind 1 is a nuclear burst: a long,
     * bassy, rolling rumble under the thump.
     */
    std::vector<std::uint8_t> make_blast_wav(int kind);
} // namespace kosmos
