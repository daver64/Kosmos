#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "bake.h"
#include "edit.h"
#include "fill.h"
#include "gui.h"
#include "pack.h"
#include "settings.h"

#ifndef PLANETBAKE_VERSION
#define PLANETBAKE_VERSION "dev"
#endif

namespace
{
    using namespace planetbake;

    void usage()
    {
        std::puts(
            "planetbake " PLANETBAKE_VERSION " - bakes colour, height, normal, specular, night-lights and cloud maps of a Kosmos generated world\n"
            "\n"
            "  planetbake                       open the preview window (when built with GTK)\n"
            "  planetbake [options] --out DIR   write the maps\n"
            "\n"
            "World (these match Kosmos's own generator, so a baked world looks like the in-game one):\n"
            "  --type NAME         mercury | venus | earth | mars | gas     (default earth)\n"
            "  --seed N            (default 1)\n"
            "  --ocean F           0-1, earth-like water cover\n"
            "  --ice F             0-1, ice cap reach\n"
            "  --relief F          mountain height / crater depth multiplier\n"
            "  --tint R,G,B        main colour, 0-1 each\n"
            "  --bands N           gas giants: number of bands\n"
            "  --contrast F        gas giants: band contrast, 0-1\n"
            "  --radius-km F       planet radius (default 6371), sets height scale of normals\n"
            "\n"
            "Output:\n"
            "  --out DIR           output folder\n"
            "  --name NAME         file name stem for single images\n"
            "  --width N           pixels round the equator (single images 256-16384; tiles up to 65536)\n"
            "  --layers LIST       colour,height,normal,specular,night,clouds or all (default all)\n"
            "  --format png|dds    (default png; height is always a 16 bit PNG)\n"
            "  --dds bc1|bc3|rgba  DDS encoding (default bc1)\n"
            "  --tiles             write a tile pyramid per layer (levelN/tx_C_R.ext + tiles.ctx), resumable\n"
            "  --detail F          fine land colour detail at high resolution (default 1; 0 is the generator's smooth colour)\n"
            "  --normal-strength F (default 8; 1 is the true slope)\n"
            "  --memory MB         refuse jobs needing more than this (default 4096)\n"
            "  --estimate          print the size of the job and exit\n"
            "  --preview FILE      write a 1024x512 PNG of one layer (--layer, default colour) and exit\n"
            "  --layer NAME\n"
            "\n"
            "Existing packs (a folder of planet textures, from planetbake or anywhere else):\n"
            "  --open DIR          open a pack (the window with no other option; --preview/--layer work on it too)\n"
            "  --info              print what the pack holds (layers, levels, locks)\n"
            "  --fill              fill missing tiles, levels and layers (--layers lists new layers to make; --dry-run only counts)\n"
            "  --source [LAYER=]X  data for --fill: an image, a folder of images (Celestia/Orbiter style) or a catalogue name (--list-sources)\n"
            "  --download NAME     same as --source NAME; fetched with curl and kept in ~/.cache/planetbake\n"
            "  --no-generate       --fill uses only sources and coarser levels, never the world generator\n"
            "  --lock SPEC         pack | layer:NAME | tile:NAME:LEVEL:COLUMN:ROW  (locked parts are never overwritten; --unlock removes)\n"
            "  --edit-tile SPEC    NAME:LEVEL:COLUMN:ROW: copy the tile out and open it in your editor (--no-launch: only copy)\n"
            "  --import-tile SPEC  take the saved edit back as the tile (--from FILE for another picture); the tile is then locked\n"
            "  --restore-tile SPEC put the original of an edited tile back\n"
            "  --editor CMD        editor for this run;  --set-editor CMD  save it (default krita, in ~/.config/planetbake)\n"
            "  --list-sources      the datasets --download knows\n"
            "\n"
            "The tool never uses more than 4 threads (--threads can only lower that).");
    }

    bool parse_tint(const char *text, float tint[3])
    {
        return std::sscanf(text, "%f,%f,%f", &tint[0], &tint[1], &tint[2]) == 3;
    }

    LayerSet parse_layers(const std::string &list, bool &ok)
    {
        LayerSet set;
        ok = true;
        if (list == "all")
            return LayerSet::all();
        size_t start = 0;
        while (start <= list.size())
        {
            size_t end = list.find(',', start);
            if (end == std::string::npos)
                end = list.size();
            const int layer = layer_from_name(list.substr(start, end - start));
            if (layer < 0)
                ok = false;
            else
                set.on[layer] = true;
            start = end + 1;
        }
        return set;
    }

    struct PackArgs
    {
        std::string dir;
        std::vector<std::string> sources;
        std::string preview_file, layer_arg;
        bool layers_given, fill, no_generate, dry_run, no_launch, info;
        std::string lock, unlock, edit, import, restore, from, editor;
    };

    bool parse_tile_spec(const std::string &spec, int &layer, TileRef &tile)
    {
        char name[32];
        int level, column, row;
        if (std::sscanf(spec.c_str(), "%31[a-z]:%d:%d:%d", name, &level, &column, &row) != 4)
            return false;
        layer = layer_from_name(name);
        tile = TileRef{level, column, row};
        return layer >= 0 && level >= 0 && level < 12 && column >= 0 && row >= 0 && row < (1 << level) && column < (2 << level);
    }

    bool lock_command(const Pack &pack, const std::string &spec, bool lock, std::string &error)
    {
        LockScope scope = LockScope::pack;
        int layer = layer_colour;
        TileRef tile;
        if (spec == "pack")
            scope = LockScope::pack;
        else if (spec.rfind("layer:", 0) == 0)
        {
            scope = LockScope::layer;
            layer = layer_from_name(spec.substr(6));
            if (layer < 0)
            {
                error = "unknown layer in " + spec;
                return false;
            }
        }
        else if (spec.rfind("tile:", 0) == 0)
        {
            scope = LockScope::tile;
            if (!parse_tile_spec(spec.substr(5), layer, tile))
            {
                error = "a tile is written tile:LAYER:LEVEL:COLUMN:ROW, for example tile:colour:5:12:7";
                return false;
            }
        }
        else
        {
            error = "lock what? pack, layer:NAME or tile:NAME:LEVEL:COLUMN:ROW";
            return false;
        }
        if (!set_locked(pack, scope, layer, tile, lock, error))
            return false;
        std::printf("%s %s\n", lock ? "Locked" : "Unlocked", spec.c_str());
        return true;
    }

    void print_summary(const Pack &pack)
    {
        std::printf("%s: %s, %d wide, world %s seed %d\n", pack.dir.c_str(), pack.tiled ? "tile pyramids" : "single images", pack.width, kosmos::archetype_name(pack.style.type),
                    pack.style.seed);
        for (int i = 0; i < layer_count; ++i)
        {
            const PackLayer &l = pack.layer[i];
            if (!l.present)
                continue;
            if (pack.tiled)
                std::printf("  %-9s levels 0-%d (%s), %d tile lock%s\n", layer_name(i), l.levels - 1, l.ext.c_str(), count_tile_locks(pack, i), count_tile_locks(pack, i) == 1 ? "" : "s");
            else
                std::printf("  %-9s %d x %d (%s)\n", layer_name(i), l.w, l.h, l.ext.c_str());
            TileRef none;
            if (is_locked(pack, LockScope::layer, i, none))
                std::puts("            locked");
        }
        if (is_locked(pack, LockScope::pack, layer_colour, TileRef{}))
            std::puts("  the whole pack is locked");
        if (pack.has_range)
            std::printf("  heights %.0f m to %.0f m\n", pack.height_min, pack.height_max);
    }

    // Returns the exit code, or -1 to show the window.
    int pack_mode(const PackArgs &a, const Job &job)
    {
        Pack pack;
        std::string error;
        if (!open_pack(a.dir, pack, error))
        {
            std::fprintf(stderr, "planetbake: %s\n", error.c_str());
            return 1;
        }
        const bool action = a.info || a.fill || !a.lock.empty() || !a.unlock.empty() || !a.edit.empty() || !a.import.empty() || !a.restore.empty() || !a.preview_file.empty();
        if (!action)
        {
#ifdef PLANETBAKE_GTK
            return -1;
#else
            print_summary(pack);
            return 0;
#endif
        }
        if (a.info)
            print_summary(pack);
        auto fail = [&]() {
            std::fprintf(stderr, "planetbake: %s\n", error.c_str());
            return 1;
        };
        if (!a.lock.empty() && !lock_command(pack, a.lock, true, error))
            return fail();
        if (!a.unlock.empty() && !lock_command(pack, a.unlock, false, error))
            return fail();

        int layer;
        TileRef tile;
        if (!a.restore.empty())
        {
            if (!parse_tile_spec(a.restore, layer, tile))
            {
                error = "a tile is written LAYER:LEVEL:COLUMN:ROW, for example colour:5:12:7";
                return fail();
            }
            if (!restore_original(pack, layer, tile, error))
                return fail();
            std::printf("Restored the original of %s\n", a.restore.c_str());
        }
        if (!a.edit.empty())
        {
            if (!parse_tile_spec(a.edit, layer, tile))
            {
                error = "a tile is written LAYER:LEVEL:COLUMN:ROW, for example colour:5:12:7";
                return fail();
            }
            std::string work;
            if (!begin_edit(pack, layer, tile, work, error))
                return fail();
            std::printf("Working copy: %s\n", work.c_str());
            if (!a.no_launch)
            {
                const std::string editor = a.editor.empty() ? load_settings().editor : a.editor;
                if (!launch_editor(editor, work, error))
                    return fail();
                std::printf("Opened in %s. Save the picture there, then run: planetbake --open \"%s\" --import-tile %s\n", editor.c_str(), pack.dir.c_str(), a.edit.c_str());
            }
        }
        if (!a.import.empty())
        {
            if (!parse_tile_spec(a.import, layer, tile))
            {
                error = "a tile is written LAYER:LEVEL:COLUMN:ROW, for example colour:5:12:7";
                return fail();
            }
            const std::string picture = a.from.empty() ? edit_work_path(pack, layer, tile) : a.from;
            if (!import_edit(pack, layer, tile, picture, error))
                return fail();
            std::printf("Imported %s into %s; the tile is now locked. The original is in %s/.originals\n", picture.c_str(), a.import.c_str(), pack.dir.c_str());
        }
        if (a.fill)
        {
            FillOptions options;
            options.generate = !a.no_generate;
            options.format = job.format;
            options.dds = job.dds;
            options.threads = job.threads;
            options.memory_mb = job.memory_mb;
            options.dry_run = a.dry_run;
            double last = -1.0;
            Progress progress = [&](double f, const std::string &text) {
                if (f - last >= 0.01 || f >= 1.0 || f == 0.0)
                {
                    std::printf("\r%3.0f%%  %-70s", f * 100.0, text.c_str());
                    std::fflush(stdout);
                    last = f;
                }
                return true;
            };
            for (const std::string &s : a.sources)
                if (!add_source(options.sources, s, progress, error))
                {
                    std::puts("");
                    return fail();
                }
            if (a.layers_given)
                options.layers = job.layers;
            else
            {
                options.layers = LayerSet();
                for (int i = 0; i < layer_count; ++i)
                    options.layers.on[i] = pack.layer[i].present || !options.sources.layer[i].path.empty();
            }
            FillReport report;
            const bool ok = fill_pack(pack, options, progress, report, error);
            std::puts("");
            if (!ok)
                return fail();
            std::printf("%s %lld tile%s/image%s: ", a.dry_run ? "Would write" : "Wrote", static_cast<long long>(report.written()), report.written() == 1 ? "" : "s", report.written() == 1 ? "" : "s");
            std::int64_t from_source = 0;
            for (std::int64_t n : report.from_source)
                from_source += n;
            std::printf("%lld from sources, %lld from coarser levels, %lld generated. %lld already there, %lld locked, %lld with no data.\n", static_cast<long long>(from_source),
                        static_cast<long long>(report.from_parent), static_cast<long long>(report.generated), static_cast<long long>(report.kept), static_cast<long long>(report.locked),
                        static_cast<long long>(report.no_data));
        }
        if (!a.preview_file.empty())
        {
            const int l = layer_from_name(a.layer_arg);
            Rendered r;
            if (l < 0 || !pack_preview(pack, l, 1024, 512, r, error))
            {
                if (l < 0)
                    error = "unknown layer " + a.layer_arg;
                return fail();
            }
            bool ok;
            if (l == layer_height)
                ok = write_png_gray16(a.preview_file, r.height.data(), r.w, r.h);
            else if (l == layer_clouds)
                ok = write_png_rgba(a.preview_file, r.layer[l].data(), r.w, r.h);
            else if (l == layer_specular)
                ok = write_png_gray(a.preview_file, r.layer[l].data(), r.w, r.h);
            else
                ok = write_png_rgb(a.preview_file, r.layer[l].data(), r.w, r.h);
            if (!ok)
            {
                error = "cannot write " + a.preview_file;
                return fail();
            }
        }
        return 0;
    }

    bool preview(const Job &job, int layer, const std::string &path, std::string &error)
    {
        World world(job.style, job.radius, job.normal_strength, job.detail);
        LayerSet one;
        one.on[layer] = true;
        if (!world.applicable(layer))
        {
            error = std::string("the ") + layer_name(layer) + " layer does not apply to this world";
            return false;
        }
        Rendered r;
        if (!render_region(world, Region{0, 0, 1, 1, 1024, 512}, one, r, job.threads, error))
            return false;
        bool ok;
        if (layer == layer_height)
            ok = write_png_gray16(path, r.height.data(), r.w, r.h);
        else if (layer == layer_clouds)
            ok = write_png_rgba(path, r.layer[layer].data(), r.w, r.h);
        else if (layer == layer_specular)
            ok = write_png_gray(path, r.layer[layer].data(), r.w, r.h);
        else
            ok = write_png_rgb(path, r.layer[layer].data(), r.w, r.h);
        if (!ok)
            error = "cannot write " + path;
        return ok;
    }
}

int main(int argc, char **argv)
{
    Job job;
    job.style.type = kosmos::Archetype::earth;
    std::string preview_file, layer_arg = "colour", layers_arg = "all";
    std::string open_dir, lock_spec, unlock_spec, edit_spec, import_spec, restore_spec, from_file, editor_override, set_editor;
    std::vector<std::string> source_args;
    bool info_only = false, do_fill = false, no_generate = false, dry_run = false, list_sources = false, no_launch = false;
    bool layers_given = false;
    bool estimate_only = false, any_option = false;
    unsigned threads = max_threads;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto value = [&]() -> const char * {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "planetbake: %s needs a value\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        any_option = true;
        if (a == "--help" || a == "-h")
        {
            usage();
            return 0;
        }
        else if (a == "--version")
        {
            std::puts("planetbake " PLANETBAKE_VERSION);
            return 0;
        }
        else if (a == "--type" || a == "--world")
        {
            job.style.type = kosmos::archetype_from_name(value());
            if (!job.style.valid())
            {
                std::fprintf(stderr, "planetbake: unknown world type\n");
                return 2;
            }
        }
        else if (a == "--seed")
            job.style.seed = std::atoi(value());
        else if (a == "--ocean")
            job.style.ocean = static_cast<float>(std::atof(value()));
        else if (a == "--ice")
            job.style.ice = static_cast<float>(std::atof(value()));
        else if (a == "--relief")
            job.style.relief = static_cast<float>(std::atof(value()));
        else if (a == "--tint")
        {
            if (!parse_tint(value(), job.style.tint))
            {
                std::fprintf(stderr, "planetbake: --tint wants R,G,B\n");
                return 2;
            }
        }
        else if (a == "--bands")
            job.style.bands = std::atoi(value());
        else if (a == "--contrast")
            job.style.contrast = static_cast<float>(std::atof(value()));
        else if (a == "--radius-km")
            job.radius = std::atof(value()) * 1000.0;
        else if (a == "--out")
            job.out = value();
        else if (a == "--name")
            job.name = value();
        else if (a == "--width")
            job.width = std::atoi(value());
        else if (a == "--layers")
        {
            layers_arg = value();
            layers_given = true;
        }
        else if (a == "--format")
        {
            const std::string f = value();
            if (f == "png")
                job.format = Format::png;
            else if (f == "dds")
                job.format = Format::dds;
            else
            {
                std::fprintf(stderr, "planetbake: --format is png or dds\n");
                return 2;
            }
        }
        else if (a == "--dds")
        {
            const std::string f = value();
            if (f == "bc1")
                job.dds = DdsKind::bc1;
            else if (f == "bc3")
                job.dds = DdsKind::bc3;
            else if (f == "rgba")
                job.dds = DdsKind::rgba;
            else
            {
                std::fprintf(stderr, "planetbake: --dds is bc1, bc3 or rgba\n");
                return 2;
            }
        }
        else if (a == "--tiles")
            job.tiles = true;
        else if (a == "--detail")
            job.detail = static_cast<float>(std::max(0.0, std::atof(value())));
        else if (a == "--normal-strength")
            job.normal_strength = static_cast<float>(std::atof(value()));
        else if (a == "--memory")
            job.memory_mb = static_cast<std::uint64_t>(std::atoll(value()));
        else if (a == "--threads")
            threads = static_cast<unsigned>(std::max(1, std::atoi(value())));
        else if (a == "--estimate")
            estimate_only = true;
        else if (a == "--preview")
            preview_file = value();
        else if (a == "--layer")
            layer_arg = value();
        else if (a == "--open")
            open_dir = value();
        else if (a == "--info")
            info_only = true;
        else if (a == "--fill")
            do_fill = true;
        else if (a == "--source" || a == "--download")
            source_args.push_back(value());
        else if (a == "--no-generate")
            no_generate = true;
        else if (a == "--dry-run")
            dry_run = true;
        else if (a == "--list-sources")
            list_sources = true;
        else if (a == "--lock")
            lock_spec = value();
        else if (a == "--unlock")
            unlock_spec = value();
        else if (a == "--edit-tile")
            edit_spec = value();
        else if (a == "--import-tile")
            import_spec = value();
        else if (a == "--restore-tile")
            restore_spec = value();
        else if (a == "--from")
            from_file = value();
        else if (a == "--no-launch")
            no_launch = true;
        else if (a == "--editor")
            editor_override = value();
        else if (a == "--set-editor")
            set_editor = value();
        else
        {
            std::fprintf(stderr, "planetbake: unknown option %s (try --help)\n", a.c_str());
            return 2;
        }
    }
    job.threads = std::min(threads, max_threads);

    bool ok = true;
    job.layers = parse_layers(layers_arg, ok);
    if (!ok)
    {
        std::fprintf(stderr, "planetbake: unknown layer in --layers\n");
        return 2;
    }
    std::string error;

    if (list_sources)
    {
        for (const CatalogEntry &e : catalogue())
            std::printf("%-18s %-8s %s\n", e.name.c_str(), e.layer.c_str(), e.note.c_str());
        return 0;
    }
    if (!set_editor.empty())
    {
        Settings s = load_settings();
        s.editor = set_editor;
        if (!save_settings(s, error))
        {
            std::fprintf(stderr, "planetbake: %s\n", error.c_str());
            return 1;
        }
        std::printf("The tile editor is now: %s\n", s.editor.c_str());
        if (open_dir.empty())
            return 0;
    }
    if (!open_dir.empty())
    {
        PackArgs args{open_dir, source_args, preview_file, layer_arg, layers_given, do_fill, no_generate, dry_run, no_launch, info_only, lock_spec, unlock_spec, edit_spec, import_spec,
                      restore_spec, from_file, editor_override};
        const int code = pack_mode(args, job);
        if (code >= 0)
            return code;
        return run_gui(job, open_dir);
    }

    if (!any_option)
        return run_gui(job);

    if (!preview_file.empty())
    {
        const int layer = layer_from_name(layer_arg);
        if (layer < 0)
        {
            std::fprintf(stderr, "planetbake: unknown layer %s\n", layer_arg.c_str());
            return 2;
        }
        if (!preview(job, layer, preview_file, error))
        {
            std::fprintf(stderr, "planetbake: %s\n", error.c_str());
            return 1;
        }
        return 0;
    }

    const Estimate e = estimate(job);
    std::printf("%d x %d, %d layer%s%s, about %s on disk, about %s of memory\n", e.width, e.height, e.layer_count, e.layer_count == 1 ? "" : "s",
                job.tiles ? (", " + std::to_string(e.tiles) + " tiles a layer").c_str() : "", format_bytes(e.disk_bytes).c_str(), format_bytes(e.memory_bytes).c_str());
    if (!e.fits)
    {
        std::fprintf(stderr, "planetbake: %s\n", e.problem.c_str());
        return 1;
    }
    if (estimate_only)
        return 0;
    if (job.out.empty())
    {
        std::fprintf(stderr, "planetbake: --out DIR is needed\n");
        return 2;
    }
    double last = -1.0;
    const bool done = run(job, [&](double f, const std::string &text) {
        if (f - last >= 0.01 || f >= 1.0)
        {
            std::printf("\r%3.0f%%  %-60s", f * 100.0, text.c_str());
            std::fflush(stdout);
            last = f;
        }
        return true;
    }, error);
    std::puts("");
    if (!done)
    {
        std::fprintf(stderr, "planetbake: %s\n", error.c_str());
        return 1;
    }
    std::printf("Wrote %s\n", job.out.c_str());
    return 0;
}
