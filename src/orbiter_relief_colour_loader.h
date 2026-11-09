#pragma once

#include "bitmap_release.h"
#include "orbiter_tile_colour.h"
#include "planet_data.h"

#include "dds.h"
#include "error.h"

namespace kosmos
{
    inline sl::Bitmap *load_orbiter_relief_colour(const OrbiterImagery &image, const MeshSource *meshes,
                                                  const glm::dquat &frame, int level, int column, int row)
    {
        constexpr std::size_t decoded_limit = 512u * 512u * 4u;
        OrbiterTree::Tile ancestor;
        std::vector<std::uint8_t> bytes;
        if (!image.read_tile(level + 4, row, column, ancestor, bytes, 2u << 20, decoded_limit) ||
            ancestor.level != level + 4)
        {
            sl::detail::set_error("Orbiter imagery tile is unreadable");
            return nullptr;
        }
        sl::DdsOptions decoding;
        decoding.allow_compressed_upload = false;
        decoding.max_decoded_bytes = decoded_limit;
        sl::Bitmap *bitmap = sl::load_dds_from_memory(bytes.data(), bytes.size(), decoding);
        if (bitmap && meshes && !apply_orbiter_mesh_colours(*bitmap, *meshes, frame, level, column, row))
        {
            discard_bitmap(bitmap);
            sl::detail::set_error("Orbiter imagery tile has invalid pixel data");
            return nullptr;
        }
        return bitmap;
    }
}
