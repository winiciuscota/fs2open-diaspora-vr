#pragma once

#include "osapi/osapi.h"

namespace graphics::vulkan {

// geometryShader device feature enabled (thick model outlines, main-g.sdr)
extern bool Vk_geometry_shader_enabled;

class VulkanRenderer;

void initialize_function_pointers();
bool initialize(std::unique_ptr<os::GraphicsOperations>&& graphicsOps);

VulkanRenderer* getRendererInstance();

void cleanup();

} // namespace graphics::vulkan
