#pragma once

// Editing one tile of a pack in an outside paint program: copy it out, open it, take the saved result back.

#include <string>

#include "pack.h"

namespace planetbake
{
    /** Where the working copy of a tile lives (in the cache, one folder per pack). */
    std::string edit_work_path(const Pack &pack, int layer, const TileRef &tile);

    /**
     * Makes the working copy (a PNG; an earlier unfinished one is kept) and backs the original up to PACK/.originals/ the first time.
     * Refuses a tile that is locked, other than by an earlier edit.
     */
    bool begin_edit(const Pack &pack, int layer, const TileRef &tile, std::string &work_file, std::string &error);

    /**
     * Puts the edited picture back as the tile, in the tile's own format, and locks the tile so baking and filling leave it alone.
     * The picture must be the same size as the tile.
     */
    bool import_edit(const Pack &pack, int layer, const TileRef &tile, const std::string &picture, std::string &error);

    /** Puts the original back (and unlocks the tile). */
    bool restore_original(const Pack &pack, int layer, const TileRef &tile, std::string &error);
}
