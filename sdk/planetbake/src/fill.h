#pragma once

// Filling a pack's gaps: missing tiles, missing levels and missing layers, from other images on disk, from public datasets
// that planetbake downloads, from the pack's own coarser levels, or from the generator.

#include <string>
#include <vector>

#include "bake.h"
#include "pack.h"

namespace planetbake
{
    /** An equirectangular picture (longitude -180 to 180 left to right, north at the top) that data is taken from. */
    struct SourceFile
    {
        std::string path;
        bool has_range = false; // height sources: the elevations (metres) the file's darkest and brightest values stand for
        double height_min = 0.0, height_max = 0.0;
    };

    struct Sources
    {
        SourceFile layer[layer_count];
    };

    /** A public dataset planetbake knows how to download. */
    struct CatalogEntry
    {
        std::string name, layer, url, file, note;
        bool has_range = false;
        double height_min = 0.0, height_max = 0.0;
    };

    /** The built-in list plus any entries in ~/.config/planetbake/sources.json. */
    std::vector<CatalogEntry> catalogue();

    /** Downloads a catalogue entry into the cache (kept for next time); curl does the transfer. */
    bool download_source(const CatalogEntry &entry, SourceFile &out, const Progress &progress, std::string &error);

    /**
     * Adds a source from a command line argument: "[LAYER=]PATH" where PATH is an image or a folder of images (as in a Celestia
     * or Orbiter texture folder; files are matched to layers by name), or a catalogue name to download. Images that are not PNG
     * or DDS are converted with ImageMagick when it is installed.
     */
    bool add_source(Sources &sources, const std::string &argument, const Progress &progress, std::string &error);

    struct FillOptions
    {
        Sources sources;
        LayerSet layers = LayerSet::all(); // which layers to fill (a layer the pack lacks is made whole)
        bool generate = true;              // last resort: the world generator, from the pack's info.json
        Format format = Format::png;       // for a layer the pack does not have yet
        DdsKind dds = DdsKind::bc1;
        unsigned threads = max_threads;
        std::uint64_t memory_mb = 4096;
        bool dry_run = false;              // count only
    };

    struct FillReport
    {
        std::int64_t from_source[layer_count] = {};
        std::int64_t from_parent = 0, generated = 0, kept = 0, locked = 0, no_data = 0;
        std::int64_t written() const;
    };

    /** Fills the pack in place. Existing and locked files are never touched. */
    bool fill_pack(Pack &pack, const FillOptions &options, const Progress &progress, FillReport &report, std::string &error);
}
