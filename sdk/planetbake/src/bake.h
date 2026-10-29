#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "encode.h"
#include "layers.h"

namespace planetbake
{
    constexpr int tile_size = 512;
    constexpr int max_image_width = 16384;
    constexpr int max_tiles_width = 65536;

    enum class Format
    {
        png,
        dds
    };

    struct Job
    {
        kosmos::PlanetStyle style;
        double radius = 6.371e6; // metres
        int width = 4096;        // pixels round the equator, at the finest level
        LayerSet layers = LayerSet::all();
        Format format = Format::png;
        DdsKind dds = DdsKind::bc1;
        bool tiles = false; // a Celestia-style tile pyramid per layer instead of single images
        float detail = 1.0f;          // fine land colour detail below the generator's own scale; 0 for none
        float normal_strength = 8.0f; // the real slopes of a whole planet are tiny, so a normal map wants them exaggerated
        unsigned threads = max_threads;
        std::uint64_t memory_mb = 4096;
        std::string out; // folder
        std::string name; // file name stem for single images
    };

    struct Estimate
    {
        int width = 0, height = 0; // the finest level
        int levels = 1;
        std::int64_t tiles = 0;
        int layer_count = 0;
        double disk_bytes = 0.0;
        double memory_bytes = 0.0;
        bool fits = true;       // the job can run within its memory limit
        std::string problem;    // why not, when it cannot
    };

    /** Width rounded as the job will use it: tile jobs go to 1024 * 2^N, at least 4096, image jobs to an even width. */
    int effective_width(const Job &job);
    Estimate estimate(const Job &job);

    /** Called with 0-1 progress and a line of text; returns false to stop. */
    using Progress = std::function<bool(double fraction, const std::string &text)>;

    /** Makes the files. Tiles already on disk are kept, so an interrupted tile job resumes. */
    bool run(const Job &job, const Progress &progress, std::string &error);

    /** "1.2 GB" style text. */
    std::string format_bytes(double bytes);
}
