#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace toolvk {

struct Vertex { float x, y, z, r, g, b; };
struct Draw { uint32_t first, count; bool lines, depth; };

// Clip coordinates use Vulkan's depth range [0,1] and downward-positive Y.
// draw() synchronously returns tightly packed, opaque native-endian Cairo ARGB32.
// The clear alpha is ignored. Depth-enabled draws test and write depth using <;
// depth-disabled draws do neither. Instances are not thread-safe.
class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    bool init(std::string &error);
    bool has_msaa() const;
    bool draw(const std::vector<Vertex>& vertices, const std::vector<Draw>& draws,
              int width, int height, const float clear[4],
              std::vector<unsigned char>& pixels, std::string &error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
