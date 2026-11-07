#undef NDEBUG
#include "blast_sound.h"
#include "audio.h"
#include "lightning.h"
#include "weapon_sound.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{
    std::uint32_t le32(const std::vector<std::uint8_t> &wav, std::size_t offset)
    {
        return static_cast<std::uint32_t>(wav[offset]) |
               (static_cast<std::uint32_t>(wav[offset + 1]) << 8) |
               (static_cast<std::uint32_t>(wav[offset + 2]) << 16) |
               (static_cast<std::uint32_t>(wav[offset + 3]) << 24);
    }

    void check_sound(const std::vector<std::uint8_t> &wav, std::uint32_t samples)
    {
        const std::uint32_t data_size = samples * 2u;
        assert(wav.size() == 44u + data_size);
        assert(le32(wav, 4) == wav.size() - 8 && le32(wav, 24) == 22050 && le32(wav, 40) == data_size);
        assert(std::any_of(wav.begin() + 44, wav.end(), [](std::uint8_t byte) { return byte != 0; }));
        sl::Sample *sample = sl::load_sample_from_memory(wav.data(), wav.size());
        assert(sample);
        const auto voice = sl::play_sample(sample);
        assert(voice != 0 && sl::voice_is_playing(voice));
        sl::stop_voice(voice);
        sl::destroy_sample(sample);
    }
}

int main()
{
    for (int kind = 0; kind < 2; ++kind)
    {
        const auto wav = kosmos::make_blast_wav(kind);
        check_sound(wav, 22050u * (kind == 0 ? 5u : 16u));
        assert(wav == kosmos::make_blast_wav(kind));
    }
    for (int kind = 0; kind < 3; ++kind)
    {
        const auto wav = kosmos::make_thunder_wav(kind);
        check_sound(wav, static_cast<std::uint32_t>(22050 * (kind == 0 ? 3.5 : kind == 1 ? 5.0 : 6.5)));
        assert(wav == kosmos::make_thunder_wav(kind));
    }
    check_sound(kosmos::make_beam_wav(1.0, 1.0), 22050);
    check_sound(kosmos::make_zap_wav(), static_cast<std::uint32_t>(22050 * 0.38));
    sl::audio_fx_shutdown();
    std::puts("synthesised sound test: passed");
}
