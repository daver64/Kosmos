#include "tool_vulkan.h"

#ifdef KOSMOS_TOOL_VULKAN
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <gmodule.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace {

// Generated with glslc -x glsl -fshader-stage={vert,frag} -O.
// Vertex: layout(location=0) in vec3 position; layout(location=1) in vec3 color;
// layout(location=0) out vec3 vcolor;
// void main() { gl_Position=vec4(position,1.0); vcolor=color; }
// Fragment: layout(location=0) in vec3 vcolor;
// layout(location=0) out vec4 outColor;
// void main() { outColor=vec4(vcolor,1.0); }
// Both sources have a "#version 450" first line.
const uint32_t vertex_spv[] = {
    0x07230203u, 0x00010000u, 0x000d000bu, 0x0000001fu, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
    0x0009000fu, 0x00000000u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x0000000du, 0x00000012u, 0x0000001cu,
    0x0000001du, 0x00050048u, 0x0000000bu, 0x00000000u, 0x0000000bu, 0x00000000u, 0x00050048u, 0x0000000bu,
    0x00000001u, 0x0000000bu, 0x00000001u, 0x00050048u, 0x0000000bu, 0x00000002u, 0x0000000bu, 0x00000003u,
    0x00050048u, 0x0000000bu, 0x00000003u, 0x0000000bu, 0x00000004u, 0x00030047u, 0x0000000bu, 0x00000002u,
    0x00040047u, 0x00000012u, 0x0000001eu, 0x00000000u, 0x00040047u, 0x0000001cu, 0x0000001eu, 0x00000000u,
    0x00040047u, 0x0000001du, 0x0000001eu, 0x00000001u, 0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u,
    0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u, 0x00000006u, 0x00000004u,
    0x00040015u, 0x00000008u, 0x00000020u, 0x00000000u, 0x0004002bu, 0x00000008u, 0x00000009u, 0x00000001u,
    0x0004001cu, 0x0000000au, 0x00000006u, 0x00000009u, 0x0006001eu, 0x0000000bu, 0x00000007u, 0x00000006u,
    0x0000000au, 0x0000000au, 0x00040020u, 0x0000000cu, 0x00000003u, 0x0000000bu, 0x0004003bu, 0x0000000cu,
    0x0000000du, 0x00000003u, 0x00040015u, 0x0000000eu, 0x00000020u, 0x00000001u, 0x0004002bu, 0x0000000eu,
    0x0000000fu, 0x00000000u, 0x00040017u, 0x00000010u, 0x00000006u, 0x00000003u, 0x00040020u, 0x00000011u,
    0x00000001u, 0x00000010u, 0x0004003bu, 0x00000011u, 0x00000012u, 0x00000001u, 0x0004002bu, 0x00000006u,
    0x00000014u, 0x3f800000u, 0x00040020u, 0x00000019u, 0x00000003u, 0x00000007u, 0x00040020u, 0x0000001bu,
    0x00000003u, 0x00000010u, 0x0004003bu, 0x0000001bu, 0x0000001cu, 0x00000003u, 0x0004003bu, 0x00000011u,
    0x0000001du, 0x00000001u, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u,
    0x00000005u, 0x0004003du, 0x00000010u, 0x00000013u, 0x00000012u, 0x00050051u, 0x00000006u, 0x00000015u,
    0x00000013u, 0x00000000u, 0x00050051u, 0x00000006u, 0x00000016u, 0x00000013u, 0x00000001u, 0x00050051u,
    0x00000006u, 0x00000017u, 0x00000013u, 0x00000002u, 0x00070050u, 0x00000007u, 0x00000018u, 0x00000015u,
    0x00000016u, 0x00000017u, 0x00000014u, 0x00050041u, 0x00000019u, 0x0000001au, 0x0000000du, 0x0000000fu,
    0x0003003eu, 0x0000001au, 0x00000018u, 0x0004003du, 0x00000010u, 0x0000001eu, 0x0000001du, 0x0003003eu,
    0x0000001cu, 0x0000001eu, 0x000100fdu, 0x00010038u,
};
const uint32_t fragment_spv[] = {
    0x07230203u, 0x00010000u, 0x000d000bu, 0x00000013u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
    0x0007000fu, 0x00000004u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00000009u, 0x0000000cu, 0x00030010u,
    0x00000004u, 0x00000007u, 0x00040047u, 0x00000009u, 0x0000001eu, 0x00000000u, 0x00040047u, 0x0000000cu,
    0x0000001eu, 0x00000000u, 0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00030016u,
    0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u, 0x00000006u, 0x00000004u, 0x00040020u, 0x00000008u,
    0x00000003u, 0x00000007u, 0x0004003bu, 0x00000008u, 0x00000009u, 0x00000003u, 0x00040017u, 0x0000000au,
    0x00000006u, 0x00000003u, 0x00040020u, 0x0000000bu, 0x00000001u, 0x0000000au, 0x0004003bu, 0x0000000bu,
    0x0000000cu, 0x00000001u, 0x0004002bu, 0x00000006u, 0x0000000eu, 0x3f800000u, 0x00050036u, 0x00000002u,
    0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x0004003du, 0x0000000au, 0x0000000du,
    0x0000000cu, 0x00050051u, 0x00000006u, 0x0000000fu, 0x0000000du, 0x00000000u, 0x00050051u, 0x00000006u,
    0x00000010u, 0x0000000du, 0x00000001u, 0x00050051u, 0x00000006u, 0x00000011u, 0x0000000du, 0x00000002u,
    0x00070050u, 0x00000007u, 0x00000012u, 0x0000000fu, 0x00000010u, 0x00000011u, 0x0000000eu, 0x0003003eu,
    0x00000009u, 0x00000012u, 0x000100fdu, 0x00010038u,
};

#define INSTANCE_FUNCTIONS(X) \
    X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) \
    X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceQueueFamilyProperties) \
    X(GetPhysicalDeviceFormatProperties) X(GetPhysicalDeviceImageFormatProperties) \
    X(CreateDevice) X(GetDeviceProcAddr)
#define DEVICE_FUNCTIONS(X) \
    X(DestroyDevice) X(DeviceWaitIdle) X(GetDeviceQueue) X(CreateCommandPool) \
    X(DestroyCommandPool) X(AllocateCommandBuffers) X(ResetCommandBuffer) \
    X(BeginCommandBuffer) X(EndCommandBuffer) X(CreateFence) X(DestroyFence) \
    X(ResetFences) X(WaitForFences) X(QueueSubmit) X(CreateBuffer) X(DestroyBuffer) \
    X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) \
    X(MapMemory) X(UnmapMemory) X(FlushMappedMemoryRanges) X(InvalidateMappedMemoryRanges) \
    X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(BindImageMemory) \
    X(CreateImageView) X(DestroyImageView) X(CreateRenderPass) X(DestroyRenderPass) \
    X(CreateFramebuffer) X(DestroyFramebuffer) X(CreateShaderModule) X(DestroyShaderModule) \
    X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateGraphicsPipelines) X(DestroyPipeline) \
    X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdSetViewport) X(CmdSetScissor) \
    X(CmdBindPipeline) X(CmdBindVertexBuffers) X(CmdDraw) X(CmdCopyImageToBuffer) X(CmdPipelineBarrier)

bool result(VkResult r, const char* operation, std::string& error) {
    if (r == VK_SUCCESS) return true;
    error = std::string(operation) + " failed (VkResult " + std::to_string(static_cast<int>(r)) + ")";
    return false;
}

}

namespace toolvk {

struct Renderer::Impl {
    GModule* module = nullptr;
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
    PFN_vkCreateInstance CreateInstance = nullptr;
#define DECLARE(name) PFN_vk##name name = nullptr;
    INSTANCE_FUNCTIONS(DECLARE)
    DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory_properties{};
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    uint32_t max_width = 0, max_height = 0;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipelines[4]{};
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize capacity = 0;
        bool coherent = false;
    } vertices, staging;
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    } color, depth, multisample;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    uint32_t width = 0, height = 0;
    bool ready = false, poisoned = false;

    ~Impl() { cleanup(); }
    void cleanup();
    void destroy_buffer(Buffer& b);
    void destroy_image(Image& i);
    void destroy_target();
    bool initialize(std::string& error);
    bool make_render_pass(std::string& error);
    bool make_pipelines(std::string& error);
    bool memory_type(uint32_t bits, VkMemoryPropertyFlags required,
                     VkMemoryPropertyFlags preferred, uint32_t& index);
    bool make_buffer(Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage, std::string& error);
    bool make_image(Image& i, VkFormat format, VkSampleCountFlagBits count,
                    VkImageUsageFlags usage, VkImageAspectFlags aspect,
                    uint32_t w, uint32_t h, std::string& error);
    bool make_target(uint32_t w, uint32_t h, std::string& error);
    bool render(const std::vector<Vertex>& v, const std::vector<Draw>& draws,
                int w, int h, const float clear[4], std::vector<unsigned char>& pixels,
                std::string& error);
};

void Renderer::Impl::destroy_buffer(Buffer& b) {
    if (b.buffer) DestroyBuffer(device, b.buffer, nullptr);
    if (b.memory) FreeMemory(device, b.memory, nullptr);
    b = {};
}
void Renderer::Impl::destroy_image(Image& i) {
    if (i.view) DestroyImageView(device, i.view, nullptr);
    if (i.image) DestroyImage(device, i.image, nullptr);
    if (i.memory) FreeMemory(device, i.memory, nullptr);
    i = {};
}
void Renderer::Impl::destroy_target() {
    if (framebuffer) DestroyFramebuffer(device, framebuffer, nullptr);
    framebuffer = VK_NULL_HANDLE;
    destroy_image(multisample);
    destroy_image(depth);
    destroy_image(color);
    destroy_buffer(staging);
    width = height = 0;
}
void Renderer::Impl::cleanup() {
    if (device) {
        if (DeviceWaitIdle) DeviceWaitIdle(device);
        destroy_target();
        destroy_buffer(vertices);
        for (auto p : pipelines) if (p) DestroyPipeline(device, p, nullptr);
        if (pipeline_layout) DestroyPipelineLayout(device, pipeline_layout, nullptr);
        if (render_pass) DestroyRenderPass(device, render_pass, nullptr);
        if (fence) DestroyFence(device, fence, nullptr);
        if (command_pool) DestroyCommandPool(device, command_pool, nullptr);
        if (DestroyDevice) DestroyDevice(device, nullptr);
    }
    if (instance && DestroyInstance) DestroyInstance(instance, nullptr);
    if (module) g_module_close(module);
}

bool Renderer::Impl::memory_type(uint32_t bits, VkMemoryPropertyFlags required,
                                  VkMemoryPropertyFlags preferred, uint32_t& index) {
    int fallback = -1;
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        auto flags = memory_properties.memoryTypes[i].propertyFlags;
        if (!(bits & (1u << i)) || (flags & required) != required) continue;
        if ((flags & preferred) == preferred) { index = i; return true; }
        if (fallback < 0) fallback = static_cast<int>(i);
    }
    if (fallback < 0) return false;
    index = static_cast<uint32_t>(fallback);
    return true;
}

bool Renderer::Impl::make_buffer(Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage,
                                  std::string& error) {
    Buffer next;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!result(CreateBuffer(device, &info, nullptr, &next.buffer), "vkCreateBuffer", error)) return false;
    VkMemoryRequirements req{};
    GetBufferMemoryRequirements(device, next.buffer, &req);
    uint32_t type;
    if (!memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, type)) {
        error = "No host-visible Vulkan buffer memory type";
        destroy_buffer(next);
        return false;
    }
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = type;
    if (!result(AllocateMemory(device, &alloc, nullptr, &next.memory), "vkAllocateMemory (buffer)", error) ||
        !result(BindBufferMemory(device, next.buffer, next.memory, 0), "vkBindBufferMemory", error)) {
        destroy_buffer(next);
        return false;
    }
    next.capacity = size;
    next.coherent = (memory_properties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    destroy_buffer(b);
    b = next;
    return true;
}

bool Renderer::Impl::make_image(Image& i, VkFormat format, VkSampleCountFlagBits count,
                                 VkImageUsageFlags usage, VkImageAspectFlags aspect,
                                 uint32_t w, uint32_t h, std::string& error) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {w, h, 1};
    info.mipLevels = info.arrayLayers = 1;
    info.samples = count;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!result(CreateImage(device, &info, nullptr, &i.image), "vkCreateImage", error)) return false;
    VkMemoryRequirements req{};
    GetImageMemoryRequirements(device, i.image, &req);
    uint32_t type;
    if (!memory_type(req.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, type)) {
        error = "No suitable Vulkan image memory type";
        return false;
    }
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = type;
    if (!result(AllocateMemory(device, &alloc, nullptr, &i.memory), "vkAllocateMemory (image)", error) ||
        !result(BindImageMemory(device, i.image, i.memory, 0), "vkBindImageMemory", error)) return false;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = i.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {aspect, 0, 1, 0, 1};
    return result(CreateImageView(device, &view, nullptr, &i.view), "vkCreateImageView", error);
}

bool Renderer::Impl::initialize(std::string& error) {
    if (!g_module_supported()) { error = "GModule does not support dynamic loading"; return false; }
#ifdef _WIN32
    module = g_module_open("vulkan-1.dll", G_MODULE_BIND_LAZY);
#else
    module = g_module_open("libvulkan.so.1", G_MODULE_BIND_LAZY);
    if (!module) module = g_module_open("libvulkan.so", G_MODULE_BIND_LAZY);
#endif
    if (!module) { error = std::string("Cannot load Vulkan runtime: ") + g_module_error(); return false; }
    gpointer symbol = nullptr;
    if (!g_module_symbol(module, "vkGetInstanceProcAddr", &symbol) || !symbol) {
        error = "Vulkan runtime has no vkGetInstanceProcAddr";
        return false;
    }
    GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(symbol);
    CreateInstance = reinterpret_cast<PFN_vkCreateInstance>(GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!CreateInstance) { error = "Vulkan runtime has no vkCreateInstance"; return false; }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Kosmos tools";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    if (!result(CreateInstance(&ici, nullptr, &instance), "vkCreateInstance", error)) return false;
#define LOAD_INSTANCE(name) \
    name = reinterpret_cast<PFN_vk##name>(GetInstanceProcAddr(instance, "vk" #name)); \
    if (!name) { error = "Vulkan instance missing vk" #name; return false; }
    INSTANCE_FUNCTIONS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
    uint32_t count = 0;
    if (!result(EnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices", error)) return false;
    if (!count) { error = "No Vulkan physical devices available"; return false; }
    std::vector<VkPhysicalDevice> devices(count);
    VkResult enumerate = EnumeratePhysicalDevices(instance, &count, devices.data());
    if (enumerate != VK_SUCCESS && enumerate != VK_INCOMPLETE)
        return result(enumerate, "vkEnumeratePhysicalDevices", error);
    devices.resize(count);
    constexpr VkFormat color_format = VK_FORMAT_B8G8R8A8_UNORM;
    const VkFormat depth_formats[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT,
                                      VK_FORMAT_D16_UNORM};
    for (auto candidate : devices) {
        uint32_t n = 0;
        GetPhysicalDeviceQueueFamilyProperties(candidate, &n, nullptr);
        std::vector<VkQueueFamilyProperties> families(n);
        GetPhysicalDeviceQueueFamilyProperties(candidate, &n, families.data());
        uint32_t family = 0;
        while (family < n && (!(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) ||
                              !families[family].queueCount)) ++family;
        if (family == n) continue;
        VkImageFormatProperties cp{};
        if (GetPhysicalDeviceImageFormatProperties(candidate, color_format, VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                0, &cp) != VK_SUCCESS || !(cp.sampleCounts & VK_SAMPLE_COUNT_1_BIT)) continue;
        VkImageFormatProperties mp{};
        VkPhysicalDeviceProperties candidate_properties{};
        GetPhysicalDeviceProperties(candidate, &candidate_properties);
        bool color_4x = GetPhysicalDeviceImageFormatProperties(candidate, color_format, VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0, &mp) == VK_SUCCESS &&
            (mp.sampleCounts & candidate_properties.limits.framebufferColorSampleCounts &
             candidate_properties.limits.framebufferDepthSampleCounts & VK_SAMPLE_COUNT_4_BIT);
        VkFormat df = VK_FORMAT_UNDEFINED;
        VkImageFormatProperties dp{};
        for (auto format : depth_formats) {
            VkImageFormatProperties candidate_depth{};
            if (GetPhysicalDeviceImageFormatProperties(candidate, format, VK_IMAGE_TYPE_2D,
                    VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0, &candidate_depth) != VK_SUCCESS ||
                !(candidate_depth.sampleCounts & VK_SAMPLE_COUNT_1_BIT)) continue;
            if (df == VK_FORMAT_UNDEFINED || (color_4x && (candidate_depth.sampleCounts & VK_SAMPLE_COUNT_4_BIT))) {
                df = format;
                dp = candidate_depth;
            }
            if (color_4x && (dp.sampleCounts & VK_SAMPLE_COUNT_4_BIT)) break;
        }
        if (df == VK_FORMAT_UNDEFINED) continue;
        physical = candidate;
        queue_family = family;
        depth_format = df;
        max_width = std::min(cp.maxExtent.width, dp.maxExtent.width);
        max_height = std::min(cp.maxExtent.height, dp.maxExtent.height);
        if (color_4x && (dp.sampleCounts & VK_SAMPLE_COUNT_4_BIT)) {
            samples = VK_SAMPLE_COUNT_4_BIT;
            max_width = std::min(max_width, mp.maxExtent.width);
            max_height = std::min(max_height, mp.maxExtent.height);
        }
        break;
    }
    if (!physical) {
        error = "No Vulkan graphics device supports BGRA8 color readback and a depth attachment";
        return false;
    }
    GetPhysicalDeviceProperties(physical, &properties);
    GetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    max_width = std::min(max_width, properties.limits.maxFramebufferWidth);
    max_height = std::min(max_height, properties.limits.maxFramebufferHeight);
    max_width = std::min(max_width, properties.limits.maxViewportDimensions[0]);
    max_height = std::min(max_height, properties.limits.maxViewportDimensions[1]);
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queue_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    if (!result(CreateDevice(physical, &dci, nullptr, &device), "vkCreateDevice", error)) return false;
#define LOAD_DEVICE(name) \
    name = reinterpret_cast<PFN_vk##name>(GetDeviceProcAddr(device, "vk" #name)); \
    if (!name) { error = "Vulkan device missing vk" #name; return false; }
    DEVICE_FUNCTIONS(LOAD_DEVICE)
#undef LOAD_DEVICE
    GetDeviceQueue(device, queue_family, 0, &queue);
    if (!queue) { error = "vkGetDeviceQueue returned a null graphics queue"; return false; }
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = queue_family;
    if (!result(CreateCommandPool(device, &pool, nullptr, &command_pool), "vkCreateCommandPool", error)) return false;
    VkCommandBufferAllocateInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cb.commandPool = command_pool;
    cb.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb.commandBufferCount = 1;
    if (!result(AllocateCommandBuffers(device, &cb, &command), "vkAllocateCommandBuffers", error)) return false;
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (!result(CreateFence(device, &fi, nullptr, &fence), "vkCreateFence", error)) return false;
    if (!make_render_pass(error) || !make_pipelines(error)) return false;
    ready = true;
    return true;
}

bool Renderer::Impl::make_render_pass(std::string& error) {
    VkAttachmentDescription a[3]{};
    a[0].format = VK_FORMAT_B8G8R8A8_UNORM;
    a[0].samples = samples;
    a[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    a[0].storeOp = samples == VK_SAMPLE_COUNT_1_BIT ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a[0].finalLayout = samples == VK_SAMPLE_COUNT_1_BIT ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    a[1].format = depth_format;
    a[1].samples = samples;
    a[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    a[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    a[2] = a[0];
    a[2].samples = VK_SAMPLE_COUNT_1_BIT;
    a[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a[2].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentReference resolve_ref{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &color_ref;
    sub.pDepthStencilAttachment = &depth_ref;
    if (samples != VK_SAMPLE_COUNT_1_BIT) sub.pResolveAttachments = &resolve_ref;
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].dstStageMask = deps[0].srcStageMask;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = samples == VK_SAMPLE_COUNT_1_BIT ? 2 : 3;
    info.pAttachments = a;
    info.subpassCount = 1;
    info.pSubpasses = &sub;
    info.dependencyCount = 2;
    info.pDependencies = deps;
    return result(CreateRenderPass(device, &info, nullptr, &render_pass), "vkCreateRenderPass", error);
}

bool Renderer::Impl::make_pipelines(std::string& error) {
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader.codeSize = sizeof(vertex_spv);
    shader.pCode = vertex_spv;
    if (!result(CreateShaderModule(device, &shader, nullptr, &vert), "vkCreateShaderModule (vertex)", error)) return false;
    shader.codeSize = sizeof(fragment_spv);
    shader.pCode = fragment_spv;
    if (!result(CreateShaderModule(device, &shader, nullptr, &frag), "vkCreateShaderModule (fragment)", error)) {
        DestroyShaderModule(device, vert, nullptr);
        return false;
    }
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    bool ok = result(CreatePipelineLayout(device, &layout, nullptr, &pipeline_layout), "vkCreatePipelineLayout", error);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[0].pName = stages[1].pName = "main";
    VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[2] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, x))},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, r))}};
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = 2;
    input.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = samples;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = states;
    VkGraphicsPipelineCreateInfo p{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    p.stageCount = 2;
    p.pStages = stages;
    p.pVertexInputState = &input;
    p.pInputAssemblyState = &assembly;
    p.pViewportState = &viewport;
    p.pRasterizationState = &raster;
    p.pMultisampleState = &ms;
    p.pDepthStencilState = &ds;
    p.pColorBlendState = &blending;
    p.pDynamicState = &dynamic;
    p.layout = pipeline_layout;
    p.renderPass = render_pass;
    for (int i = 0; ok && i < 4; ++i) {
        assembly.topology = (i & 1) ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        ds.depthTestEnable = ds.depthWriteEnable = (i & 2) ? VK_TRUE : VK_FALSE;
        ok = result(CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &p, nullptr, &pipelines[i]),
                    "vkCreateGraphicsPipelines", error);
    }
    DestroyShaderModule(device, frag, nullptr);
    DestroyShaderModule(device, vert, nullptr);
    return ok;
}

bool Renderer::Impl::make_target(uint32_t w, uint32_t h, std::string& error) {
    destroy_target();
    const auto color_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!make_image(color, VK_FORMAT_B8G8R8A8_UNORM, VK_SAMPLE_COUNT_1_BIT,
                    color_usage, VK_IMAGE_ASPECT_COLOR_BIT, w, h, error) ||
        !make_image(depth, depth_format, samples, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT, w, h, error) ||
        (samples != VK_SAMPLE_COUNT_1_BIT &&
         !make_image(multisample, VK_FORMAT_B8G8R8A8_UNORM, samples,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, w, h, error)) ||
        !make_buffer(staging, static_cast<VkDeviceSize>(w) * h * 4,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT, error)) {
        destroy_target();
        return false;
    }
    VkImageView views[3] = {samples == VK_SAMPLE_COUNT_1_BIT ? color.view : multisample.view,
                            depth.view, color.view};
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = render_pass;
    info.attachmentCount = samples == VK_SAMPLE_COUNT_1_BIT ? 2 : 3;
    info.pAttachments = views;
    info.width = w;
    info.height = h;
    info.layers = 1;
    if (!result(CreateFramebuffer(device, &info, nullptr, &framebuffer), "vkCreateFramebuffer", error)) {
        destroy_target();
        return false;
    }
    width = w;
    height = h;
    return true;
}

bool Renderer::Impl::render(const std::vector<Vertex>& v, const std::vector<Draw>& draws,
                            int w, int h, const float clear[4], std::vector<unsigned char>& pixels,
                            std::string& error) {
    if (!ready || poisoned) { error = poisoned ? "Vulkan renderer is unusable after a submission failure" :
                                                "Vulkan renderer is not initialized"; return false; }
    if (w <= 0 || h <= 0 || !clear) { error = "Invalid Vulkan render dimensions or clear color"; return false; }
    if (static_cast<uint32_t>(w) > max_width || static_cast<uint32_t>(h) > max_height) {
        error = "Vulkan render dimensions exceed device limits"; return false;
    }
    if (static_cast<size_t>(w) > std::numeric_limits<size_t>::max() / 4 / static_cast<size_t>(h) ||
        v.size() > std::numeric_limits<size_t>::max() / sizeof(Vertex) ||
        v.size() > std::numeric_limits<VkDeviceSize>::max() / sizeof(Vertex) ||
        v.size() > std::numeric_limits<uint32_t>::max()) {
        error = "Vulkan render buffer size overflow"; return false;
    }
    for (const auto& d : draws) {
        if (static_cast<size_t>(d.first) > v.size() ||
            static_cast<size_t>(d.count) > v.size() - static_cast<size_t>(d.first)) {
            error = "Vulkan draw references vertices outside the input buffer"; return false;
        }
    }
    if ((width != static_cast<uint32_t>(w) || height != static_cast<uint32_t>(h)) &&
        !make_target(static_cast<uint32_t>(w), static_cast<uint32_t>(h), error)) return false;
    VkDeviceSize vertex_bytes = static_cast<VkDeviceSize>(v.size()) * sizeof(Vertex);
    if (vertex_bytes > vertices.capacity) {
        if (!make_buffer(vertices, vertex_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, error)) return false;
    }
    if (vertex_bytes) {
        void* mapped = nullptr;
        if (!result(MapMemory(device, vertices.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory (vertices)", error)) return false;
        std::memcpy(mapped, v.data(), static_cast<size_t>(vertex_bytes));
        if (!vertices.coherent) {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = vertices.memory;
            range.size = VK_WHOLE_SIZE;
            VkResult r = FlushMappedMemoryRanges(device, 1, &range);
            UnmapMemory(device, vertices.memory);
            if (!result(r, "vkFlushMappedMemoryRanges", error)) return false;
        } else {
            UnmapMemory(device, vertices.memory);
        }
    }
    if (!result(ResetCommandBuffer(command, 0), "vkResetCommandBuffer", error)) return false;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!result(BeginCommandBuffer(command, &begin), "vkBeginCommandBuffer", error)) return false;
    VkClearValue values[2]{};
    values[0].color.float32[0] = clear[0];
    values[0].color.float32[1] = clear[1];
    values[0].color.float32[2] = clear[2];
    values[0].color.float32[3] = 1.0f;
    values[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = render_pass;
    pass.framebuffer = framebuffer;
    pass.renderArea.extent = {width, height};
    pass.clearValueCount = 2;
    pass.pClearValues = values;
    CmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {width, height}};
    CmdSetViewport(command, 0, 1, &viewport);
    CmdSetScissor(command, 0, 1, &scissor);
    if (vertex_bytes) {
        VkDeviceSize offset = 0;
        CmdBindVertexBuffers(command, 0, 1, &vertices.buffer, &offset);
        int previous = -1;
        for (const auto& d : draws) {
            if (!d.count) continue;
            int index = (d.lines ? 1 : 0) | (d.depth ? 2 : 0);
            if (index != previous) {
                CmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[index]);
                previous = index;
            }
            CmdDraw(command, d.count, 1, d.first, 0);
        }
    }
    CmdEndRenderPass(command);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {width, height, 1};
    CmdCopyImageToBuffer(command, color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &copy);
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = staging.buffer;
    barrier.size = VK_WHOLE_SIZE;
    CmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                       0, 0, nullptr, 1, &barrier, 0, nullptr);
    if (!result(EndCommandBuffer(command), "vkEndCommandBuffer", error)) return false;
    if (!result(ResetFences(device, 1, &fence), "vkResetFences", error)) return false;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    if (!result(QueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit", error)) {
        poisoned = true; return false;
    }
    if (!result(WaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences", error)) {
        poisoned = true; return false;
    }
    void* mapped = nullptr;
    if (!result(MapMemory(device, staging.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory (readback)", error)) return false;
    if (!staging.coherent) {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = staging.memory;
        range.size = VK_WHOLE_SIZE;
        if (!result(InvalidateMappedMemoryRanges(device, 1, &range), "vkInvalidateMappedMemoryRanges", error)) {
            UnmapMemory(device, staging.memory);
            return false;
        }
    }
    size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    try {
        pixels.resize(bytes);
    } catch (...) {
        UnmapMemory(device, staging.memory);
        throw;
    }
    const auto* src = static_cast<const unsigned char*>(mapped);
    const uint16_t endian = 1;
    if (*reinterpret_cast<const unsigned char*>(&endian)) {
        std::memcpy(pixels.data(), src, bytes);
        for (size_t i = 3; i < bytes; i += 4) pixels[i] = 255;
    } else {
        for (size_t i = 0; i < bytes; i += 4) {
            pixels[i] = 255;
            pixels[i + 1] = src[i + 2];
            pixels[i + 2] = src[i + 1];
            pixels[i + 3] = src[i];
        }
    }
    UnmapMemory(device, staging.memory);
    return true;
}

Renderer::Renderer() = default;
Renderer::~Renderer() = default;
bool Renderer::init(std::string& error) {
    error.clear();
    try {
        auto next = std::make_unique<Impl>();
        if (!next->initialize(error)) return false;
        impl_ = std::move(next);
        return true;
    } catch (const std::exception& e) {
        error = std::string("Vulkan initialization failed: ") + e.what();
        return false;
    }
}
bool Renderer::has_msaa() const {
    return impl_ && impl_->ready && impl_->samples == VK_SAMPLE_COUNT_4_BIT;
}
bool Renderer::draw(const std::vector<Vertex>& vertices, const std::vector<Draw>& draws,
                    int width, int height, const float clear[4], std::vector<unsigned char>& pixels,
                    std::string& error) {
    error.clear();
    try {
        if (!impl_) { error = "Vulkan renderer is not initialized"; return false; }
        return impl_->render(vertices, draws, width, height, clear, pixels, error);
    } catch (const std::exception& e) {
        error = std::string("Vulkan rendering failed: ") + e.what();
        return false;
    }
}
}

#else
namespace toolvk {
struct Renderer::Impl {};
Renderer::Renderer() = default;
Renderer::~Renderer() = default;
bool Renderer::init(std::string& error) {
    error = "Vulkan tool backend was not compiled (KOSMOS_TOOL_VULKAN is undefined)";
    return false;
}
bool Renderer::has_msaa() const { return false; }
bool Renderer::draw(const std::vector<Vertex>&, const std::vector<Draw>&, int, int,
                    const float[4], std::vector<unsigned char>&, std::string& error) {
    error = "Vulkan tool backend was not compiled (KOSMOS_TOOL_VULKAN is undefined)";
    return false;
}
}
#endif
