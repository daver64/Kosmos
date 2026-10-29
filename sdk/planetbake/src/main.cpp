#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "bake.h"
#include "gui.h"

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
            layers_arg = value();
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
