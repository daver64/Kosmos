#pragma once

// A pack is a folder of planet textures: single images (NAME_layer.png/.dds + NAME_info.json) or a tile pyramid per layer
// (layer/levelN/tx_C_R.ext + info.json). planetbake makes them, and can open one made by anything, show it, fill in what is
// missing, edit single tiles and lock parts of it against being overwritten.

#include <string>
#include <vector>

#include "encode.h"
#include "imageio.h"
#include "layers.h"

namespace planetbake
{
    struct PackLayer
    {
        bool present = false;
        std::string ext;     // ".png" or ".dds"
        int levels = 0;      // tile pyramids: levels 0 .. levels - 1 have a folder
        std::string file;    // single images: the file
        int w = 0, h = 0;    // single images: its size
    };

    struct Pack
    {
        std::string dir;
        std::string name;   // single images: the file name stem
        bool tiled = false;
        kosmos::PlanetStyle style; // what made it (from info.json), used to generate missing parts to match
        double radius_m = 6.371e6;
        bool has_range = false;    // info.json gave the height range
        double height_min = 0.0, height_max = 0.0;
        int width = 0;             // pixels round the equator at the finest level
        PackLayer layer[layer_count];

        int max_level() const; // tile pyramids: the finest level any layer has
        /** The path a tile has (or would have) in the layer, with the layer's own extension. */
        std::string tile_path(int layer, int level, int column, int row, const char *ext = nullptr) const;
        /** Single images: the path a layer's file has or would have. */
        std::string image_path(int layer, const char *ext = nullptr) const;
        std::string info_path() const;
    };

    /** Opens a folder as a pack. Fails when it holds no recognisable layers. */
    bool open_pack(const std::string &dir, Pack &pack, std::string &error);

    /** Which tile of a layer's pyramid is at this point of the map (u, v in 0-1, u as the preview shows it). */
    struct TileRef
    {
        int level = 0, column = 0, row = 0;
    };
    TileRef tile_at(double u, double v, int level);
    /** The map position (u0, v0, u1, v1; u as the preview shows it) a tile covers. */
    void tile_bounds(const TileRef &tile, double &u0, double &v0, double &u1, double &v1);

    /** The layer as a w x h picture the way the generator's preview shows it (RGBA, or heights, in `out`). Missing tiles are grey. */
    bool pack_preview(const Pack &pack, int layer, int w, int h, Rendered &out, std::string &error);

    bool read_pack_tile(const Pack &pack, int layer, const TileRef &tile, Image &out, std::string &error);

    /** DDS encoding of an existing file (so new tiles match their neighbours). */
    DdsKind dds_kind_of(const std::string &path);

    /**
     * Writes a layer image: height as a 16 bit PNG (from `gray` when given, else from the red channel), specular as grey, clouds as RGBA,
     * the rest as RGB; or as DDS when `dds`. Written to a temporary name and renamed.
     */
    bool write_layer_image(const std::string &path, int layer, const std::uint8_t *rgba, const std::uint16_t *gray, int w, int h, bool dds, DdsKind kind);

    // ---- locking ----
    enum class LockScope
    {
        pack,
        layer,
        tile
    };

    /** Locked when the whole pack, the layer or the tile is. For single images the tile arguments are ignored. */
    bool is_locked(const Pack &pack, int layer, const TileRef &tile);
    bool is_locked(const Pack &pack, LockScope scope, int layer, const TileRef &tile);
    bool set_locked(const Pack &pack, LockScope scope, int layer, const TileRef &tile, bool locked, std::string &error, bool edited = false);
    /** A tile lock made by importing an edit: editing the tile again is allowed, baking and filling still leave it alone. */
    bool is_edit_locked(const Pack &pack, int layer, const TileRef &tile);
    /** Number of tile locks in a layer (for showing in the window). */
    int count_tile_locks(const Pack &pack, int layer);
}
