#include "scenario_thumbnail.h"

#include "json.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>
#include <vector>

#include <png.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kosmos
{
    namespace
    {
        namespace fs = std::filesystem;

        bool generated_thumbnail(const fs::path &scenario, const fs::path &relative)
        {
            if (relative.parent_path() != fs::path("thumbs") || relative.extension() != ".png")
                return false;
            const std::string name = relative.stem().u8string();
            const std::string prefix = scenario.stem().u8string() + "-";
            if (name.rfind(prefix, 0) != 0)
                return false;
            const std::string suffix = name.substr(prefix.size());
            const std::size_t dash = suffix.find('-');
            const auto digits = [](unsigned char ch) { return std::isdigit(ch) != 0; };
            return dash != std::string::npos && dash > 0 && dash + 1 < suffix.size() &&
                   std::all_of(suffix.begin(), suffix.begin() + static_cast<std::ptrdiff_t>(dash), digits) &&
                   std::all_of(suffix.begin() + static_cast<std::ptrdiff_t>(dash + 1), suffix.end(), digits);
        }

        bool thumbnail_referenced(const fs::path &scenario, const fs::path &image, bool &referenced, std::string &error)
        {
            referenced = false;
            std::error_code ec;
            const fs::path old = fs::weakly_canonical(image, ec);
            if (ec)
            {
                error = "Cannot inspect old thumbnail " + image.u8string() + ": " + ec.message();
                return false;
            }
            fs::path root = scenario.parent_path();
            for (fs::path parent = root; !parent.empty(); parent = parent.parent_path())
            {
                if (parent.filename() == "scenarios")
                {
                    root = parent;
                    break;
                }
                if (parent == parent.parent_path())
                    break;
            }
            fs::recursive_directory_iterator it(root, ec), end;
            for (; !ec && it != end; it.increment(ec))
            {
                if (it->path().extension() != ".json" || it->path() == scenario)
                    continue;
                std::ifstream in(it->path(), std::ios::binary);
                if (!in)
                {
                    error = "Cannot check thumbnail references in " + it->path().u8string();
                    return false;
                }
                const nlohmann::json other = nlohmann::json::parse(in, nullptr, false);
                if (in.bad() || other.is_discarded())
                {
                    error = "Cannot check thumbnail references in " + it->path().u8string();
                    return false;
                }
                if (!other.is_object() || !other.contains("thumbnail") || !other["thumbnail"].is_string())
                    continue;
                const fs::path candidate = fs::weakly_canonical(it->path().parent_path() / fs::u8path(other["thumbnail"].get<std::string>()), ec);
                if (ec)
                    break;
                if (candidate == old)
                {
                    referenced = true;
                    return true;
                }
            }
            if (ec)
            {
                error = "Cannot check thumbnail references: " + ec.message();
                return false;
            }
            return true;
        }
    }

    bool write_thumbnail_png(const std::string &path, const std::vector<std::uint8_t> &pixels,
                             int width, int height, std::string &error)
    {
        error.clear();
        if (width <= 0 || height <= 0 ||
            static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() / 4 / static_cast<std::size_t>(height) ||
            pixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4)
        {
            error = "Invalid thumbnail pixel buffer";
            return false;
        }
        const fs::path file_path = fs::u8path(path);
#ifdef _WIN32
        FILE *file = _wfopen(file_path.c_str(), L"wb");
#else
        FILE *file = std::fopen(file_path.c_str(), "wb");
#endif
        if (!file)
        {
            error = "Cannot open thumbnail " + path;
            return false;
        }
        png_image image{};
        image.version = PNG_IMAGE_VERSION;
        image.width = static_cast<png_uint_32>(width);
        image.height = static_cast<png_uint_32>(height);
        image.format = PNG_FORMAT_RGBA;
        const bool written = png_image_write_to_stdio(&image, file, 0, pixels.data(), 0, nullptr) != 0;
        if (!written)
            error = "Cannot encode thumbnail " + path + ": " + image.message;
        png_image_free(&image);
        if (std::fclose(file) != 0)
        {
            if (error.empty())
                error = "Cannot finish writing thumbnail " + path;
            return false;
        }
        return written;
    }

    bool save_scenario_thumbnail(const std::string &scenario_path,
                                 const std::function<bool(const std::string &)> &capture,
                                 std::string &image_path, std::string &error)
    {
        namespace fs = std::filesystem;
        image_path.clear();
        error.clear();
        if (scenario_path.empty())
        {
            error = "No saved scenario is active; open or launch a scenario first.";
            return false;
        }
        const fs::path scenario = fs::u8path(scenario_path);
        std::error_code ec;
        if (fs::is_symlink(scenario, ec))
        {
            error = "Cannot update a symlinked scenario; open its original file: " + scenario_path;
            return false;
        }
        if (ec)
        {
            error = "Cannot inspect scenario " + scenario_path + ": " + ec.message();
            return false;
        }
        std::ifstream in(scenario, std::ios::binary);
        if (!in)
        {
            error = "Cannot open scenario " + scenario_path;
            return false;
        }
        const std::string original((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (in.bad())
        {
            error = "Cannot read scenario " + scenario_path;
            return false;
        }
        nlohmann::json json;
        try
        {
            json = nlohmann::json::parse(original);
        }
        catch (const nlohmann::json::exception &e)
        {
            error = "Cannot read scenario " + scenario_path + ": " + e.what();
            return false;
        }
        if (!json.is_object() || !json.contains("ships") || !json["ships"].is_array())
        {
            error = "Not a scenario: " + scenario_path;
            return false;
        }
        in.close();
        const fs::path directory = scenario.parent_path() / "thumbs";
        fs::create_directories(directory, ec);
        if (ec)
        {
            error = "Cannot create thumbnail folder " + directory.u8string() + ": " + ec.message();
            return false;
        }
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        fs::path image;
        fs::path temp;
        for (unsigned int i = 0;; ++i)
        {
            const std::string suffix = std::to_string(stamp) + "-" + std::to_string(i);
            image = directory / fs::u8path(scenario.stem().u8string() + "-" + suffix + ".png");
            temp = scenario.parent_path() / fs::u8path(scenario.filename().u8string() + "." + suffix + ".tmp");
            const bool image_exists = fs::exists(image, ec);
            if (!ec && !image_exists)
            {
                const bool temp_exists = fs::exists(temp, ec);
                if (!ec && !temp_exists)
                    break;
            }
            if (ec)
            {
                error = "Cannot check thumbnail destination: " + ec.message();
                return false;
            }
        }
        if (!capture(image.u8string()))
        {
            fs::remove(image, ec);
            error = "Could not capture scenario thumbnail " + image.u8string();
            return false;
        }
        json["thumbnail"] = (fs::path("thumbs") / image.filename()).generic_u8string();
        {
            std::ofstream out(temp, std::ios::binary);
            if (out)
            {
                out << json.dump(2) << '\n';
                out.close();
            }
            if (!out)
            {
                fs::remove(temp, ec);
                fs::remove(image, ec);
                error = "Cannot write updated scenario " + scenario_path;
                return false;
            }
        }
        const auto permissions = fs::status(scenario, ec).permissions();
        if (!ec)
            fs::permissions(temp, permissions, ec);
        if (!ec)
        {
            std::ifstream current(scenario, std::ios::binary);
            const std::string latest((std::istreambuf_iterator<char>(current)), std::istreambuf_iterator<char>());
            if (!current || latest != original)
                ec = std::make_error_code(std::errc::state_not_recoverable);
        }
        if (ec)
        {
            const std::string why = ec.message();
            fs::remove(temp, ec);
            fs::remove(image, ec);
            error = "Cannot update scenario " + scenario_path + " (it may have changed): " + why;
            return false;
        }
#ifdef _WIN32
        if (!MoveFileExW(temp.c_str(), scenario.c_str(), MOVEFILE_REPLACE_EXISTING))
            ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
        fs::rename(temp, scenario, ec);
#endif
        if (ec)
        {
            const std::string why = ec.message();
            fs::remove(temp, ec);
            fs::remove(image, ec);
            error = "Cannot update scenario " + scenario_path + ": " + why;
            return false;
        }
        image_path = image.u8string();
        std::vector<fs::path> old_images;
        fs::directory_iterator it(directory, ec), end;
        for (; !ec && it != end; it.increment(ec))
        {
            const fs::path previous = it->path();
            if (previous == image || !generated_thumbnail(scenario, fs::path("thumbs") / previous.filename()))
                continue;
            const fs::file_status status = it->symlink_status(ec);
            if (ec)
                break;
            if (fs::is_regular_file(status))
                old_images.push_back(previous);
        }
        if (ec)
        {
            error = "Saved new thumbnail, but cannot inspect old thumbnails: " + ec.message();
            return false;
        }
        for (const fs::path &previous : old_images)
        {
            bool referenced = false;
            if (!thumbnail_referenced(scenario, previous, referenced, error))
            {
                error = "Saved new thumbnail, but could not remove the old one: " + error;
                return false;
            }
            if (!referenced && !fs::remove(previous, ec) && ec)
            {
                error = "Saved new thumbnail, but cannot remove old thumbnail " + previous.u8string() + ": " + ec.message();
                return false;
            }
        }
        return true;
    }
}
