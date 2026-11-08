#undef NDEBUG
#include "scenario_thumbnail.h"
#include "json.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int main()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / ("kosmos-thumbnail-test-" + std::to_string(stamp));
    fs::create_directories(dir / "thumbs");
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
    std::string image, error;
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
