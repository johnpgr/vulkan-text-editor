#pragma once

namespace Platform {

ArrayList<const char*> GetVulkanInstanceExtensions(Arena* arena);

bool CreateVulkanSurface(VkInstance instance, VkSurfaceKHR* out_surface);

}
