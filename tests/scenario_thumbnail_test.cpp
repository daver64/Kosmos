#undef NDEBUG
#include "scenario_thumbnail.h"
#include "json.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <png.h>

namespace fs = std::filesystem;

int main()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / ("kosmos-thumbnail-test-" + std::to_string(stamp));
    fs::create_directories(dir / "thumbs");
    const int width = 320, height = 180;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
            pixels[i] = static_cast<std::uint8_t>(x % 16);
            pixels[i + 1] = static_cast<std::uint8_t>(y % 8);
            pixels[i + 2] = 123;
            pixels[i + 3] = 128;
        }
    const fs::path compressed = dir / "compressed.png";
    std::string error;
    assert(kosmos::write_thumbnail_png(compressed.string(), pixels, width, height, error));
    assert(fs::file_size(compressed) < pixels.size() / 10);
    png_image decoded{};
    decoded.version = PNG_IMAGE_VERSION;
    assert(png_image_begin_read_from_file(&decoded, compressed.string().c_str()));
    assert(decoded.width == width && decoded.height == height);
    decoded.format = PNG_FORMAT_RGBA;
    std::vector<std::uint8_t> result_pixels(PNG_IMAGE_SIZE(decoded));
    assert(png_image_finish_read(&decoded, nullptr, result_pixels.data(), 0, nullptr));
    png_image_free(&decoded);
    assert(result_pixels == pixels);
    assert(!kosmos::write_thumbnail_png(compressed.string(), pixels, width - 1, height, error) && !error.empty());
    assert(!kosmos::write_thumbnail_png((dir / "missing/out.png").string(), pixels, width, height, error) && !error.empty());
    const fs::path integration_scenario = dir / "integrated.json";
    std::ofstream(integration_scenario) << R"({"ships":[]})";
    std::string integrated_image;
    const auto png_capture = [&](const std::string &path) {
        return kosmos::write_thumbnail_png(path, pixels, width, height, error);
    };
    assert(kosmos::save_scenario_thumbnail(integration_scenario.string(), png_capture, integrated_image, error));
    nlohmann::json integrated_json;
    std::ifstream(integration_scenario) >> integrated_json;
    assert(dir / integrated_json["thumbnail"].get<std::string>() == fs::path(integrated_image));
    assert(fs::file_size(integrated_image) < pixels.size() / 10);
    const fs::path scenario = dir / "flight.json";
    const fs::path existing = dir / "thumbs" / "existing.png";
    const fs::path orphan = dir / "thumbs" / "flight-123-0.png";
    const fs::path unrelated = dir / "thumbs" / "other-123-0.png";
    std::ofstream(existing) << "original";
    std::ofstream(orphan) << "old";
    std::ofstream(unrelated) << "other";
    std::ofstream(scenario) << R"({"name":"Test","thumbnail":"thumbs/existing.png","ships":[],"other":{"keep":42}})";
    auto capture = [](const std::string &path) {
        std::ofstream out(path);
        out << "image";
        return static_cast<bool>(out);
    };
    std::string image;
    assert(kosmos::save_scenario_thumbnail(scenario.string(), capture, image, error));
    assert(fs::is_regular_file(image) && fs::is_regular_file(existing));
    assert(!fs::exists(orphan) && fs::is_regular_file(unrelated));
    nlohmann::json result;
    std::ifstream(scenario) >> result;
    assert(result["other"]["keep"] == 42);
    assert(dir / result["thumbnail"].get<std::string>() == fs::path(image));
    const std::string first = image;
    assert(kosmos::save_scenario_thumbnail(scenario.string(), capture, image, error));
    assert(image != first && !fs::exists(first) && fs::is_regular_file(image));
    const std::string second = image;
    const fs::path other = dir / "other.json";
    std::ofstream(other) << nlohmann::json{{"ships", nlohmann::json::array()}, {"thumbnail", fs::relative(second, dir).generic_string()}};
    assert(kosmos::save_scenario_thumbnail(scenario.string(), capture, image, error));
    assert(fs::is_regular_file(second) && fs::is_regular_file(image));
    const std::string third = image;
    fs::remove(other);
    assert(kosmos::save_scenario_thumbnail(scenario.string(), capture, image, error));
    assert(!fs::exists(second) && !fs::exists(third));
    const std::string current = image;
    const auto failed_capture = [](const std::string &) { return false; };
    assert(!kosmos::save_scenario_thumbnail(scenario.string(), failed_capture, image, error));
    std::ifstream(scenario) >> result;
    assert(dir / result["thumbnail"].get<std::string>() == fs::path(current));
    assert(fs::is_regular_file(current));
    std::ofstream(other) << "{ invalid";
    assert(!kosmos::save_scenario_thumbnail(scenario.string(), capture, image, error));
    assert(error.find("Saved new thumbnail") != std::string::npos && fs::is_regular_file(current));
    assert(fs::is_regular_file(image));
    fs::remove(other);
    const std::string latest = image;
    const auto concurrent_change = [&scenario, &capture](const std::string &path) {
        std::ofstream(scenario) << R"({"name":"Changed elsewhere","ships":[]})";
        return capture(path);
    };
    assert(!kosmos::save_scenario_thumbnail(scenario.string(), concurrent_change, image, error));
    std::ifstream(scenario) >> result;
    assert(result["name"] == "Changed elsewhere" && !result.contains("thumbnail"));
    assert(fs::is_regular_file(current) && fs::is_regular_file(latest));
    assert(!kosmos::save_scenario_thumbnail("", capture, image, error) && !error.empty());
    assert(!kosmos::save_scenario_thumbnail((dir / "missing.json").string(), capture, image, error));
    fs::remove_all(dir);
    std::cout << "scenario_thumbnail_test ok\n";
}
