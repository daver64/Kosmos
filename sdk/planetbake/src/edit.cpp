#include "edit.h"

#include <filesystem>
#include <functional>

#include "settings.h"

namespace planetbake
{
    namespace fs = std::filesystem;

    namespace
    {
        std::string tile_name(const TileRef &t) { return "tx_" + std::to_string(t.column) + "_" + std::to_string(t.row); }

        fs::path original_path(const Pack &pack, int layer, const TileRef &tile)
        {
            return fs::path(pack.dir) / ".originals" / layer_name(layer) / ("level" + std::to_string(tile.level)) / fs::path(pack.tile_path(layer, tile.level, tile.column, tile.row)).filename();
        }

        bool check(const Pack &pack, int layer, const TileRef &tile, std::string &error)
        {
            if (!pack.tiled || !pack.layer[layer].present)
            {
                error = std::string("the pack has no tiled ") + layer_name(layer) + " layer";
                return false;
            }
            if (tile.level < 0 || tile.level >= pack.layer[layer].levels)
            {
                error = "the layer has no level " + std::to_string(tile.level);
                return false;
            }
            return true;
        }
    }

    std::string edit_work_path(const Pack &pack, int layer, const TileRef &tile)
    {
        const std::string tag = fs::path(pack.dir).filename().string() + "-" + std::to_string(std::hash<std::string>()(pack.dir) % 100000);
        return (fs::path(cache_dir()) / "edit" / tag / layer_name(layer) / ("level" + std::to_string(tile.level)) / (tile_name(tile) + ".png")).string();
    }

    bool begin_edit(const Pack &pack, int layer, const TileRef &tile, std::string &work_file, std::string &error)
    {
        if (!check(pack, layer, tile, error))
            return false;
        if (is_locked(pack, layer, tile) && !is_edit_locked(pack, layer, tile))
        {
            error = "this tile is locked (unlock it first if you really want to change it)";
            return false;
        }
        const std::string tile_file = pack.tile_path(layer, tile.level, tile.column, tile.row);
        std::error_code ec;
        if (!fs::exists(tile_file, ec))
        {
            error = "this tile does not exist yet: fill the pack first";
            return false;
        }
        const fs::path backup = original_path(pack, layer, tile);
        if (!fs::exists(backup, ec))
        {
            fs::create_directories(backup.parent_path(), ec);
            fs::copy_file(tile_file, backup, ec);
            if (ec)
            {
                error = "cannot back up the original to " + backup.string();
                return false;
            }
        }
        work_file = edit_work_path(pack, layer, tile);
        if (fs::exists(work_file, ec))
            return true; // an earlier working copy, perhaps with unfinished work
        fs::create_directories(fs::path(work_file).parent_path(), ec);
        Image img;
        if (!read_image(tile_file, img, error))
            return false;
        if (!write_layer_image(work_file, layer, img.rgba.data(), img.gray.empty() ? nullptr : img.gray.data(), img.w, img.h, false, DdsKind::rgba))
        {
            error = "cannot write " + work_file;
            return false;
        }
        return true;
    }

    bool import_edit(const Pack &pack, int layer, const TileRef &tile, const std::string &picture, std::string &error)
    {
        if (!check(pack, layer, tile, error))
            return false;
        if (is_locked(pack, layer, tile) && !is_edit_locked(pack, layer, tile))
        {
            error = "this tile is locked (unlock it first if you really want to change it)";
            return false;
        }
        const std::string tile_file = pack.tile_path(layer, tile.level, tile.column, tile.row);
        std::error_code ec;
        int tw = 0, th = 0;
        if (!image_size(tile_file, tw, th))
        {
            error = "cannot read the tile " + tile_file;
            return false;
        }
        Image img;
        if (!read_image(picture, img, error))
            return false;
        if (img.w != tw || img.h != th)
        {
            error = "the edited picture is " + std::to_string(img.w) + " x " + std::to_string(img.h) + " but the tile is " + std::to_string(tw) + " x " + std::to_string(th) +
                    ": keep the size";
            return false;
        }
        const fs::path backup = original_path(pack, layer, tile);
        if (!fs::exists(backup, ec))
        {
            fs::create_directories(backup.parent_path(), ec);
            fs::copy_file(tile_file, backup, ec);
        }
        const bool dds = pack.layer[layer].ext == ".dds";
        if (!write_layer_image(tile_file, layer, img.rgba.data(), img.gray.empty() ? nullptr : img.gray.data(), img.w, img.h, dds, dds ? dds_kind_of(backup.string()) : DdsKind::rgba))
        {
            error = "cannot write " + tile_file;
            return false;
        }
        return set_locked(pack, LockScope::tile, layer, tile, true, error, true);
    }

    bool restore_original(const Pack &pack, int layer, const TileRef &tile, std::string &error)
    {
        if (!check(pack, layer, tile, error))
            return false;
        const fs::path backup = original_path(pack, layer, tile);
        std::error_code ec;
        if (!fs::exists(backup, ec))
        {
            error = "there is no backup of this tile";
            return false;
        }
        fs::copy_file(backup, pack.tile_path(layer, tile.level, tile.column, tile.row), fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            error = "cannot restore the tile";
            return false;
        }
        fs::remove(edit_work_path(pack, layer, tile), ec);
        return set_locked(pack, LockScope::tile, layer, tile, false, error);
    }
}
