#pragma once

namespace platform {

ArrayList<const char*> get_vulkan_instance_extensions(Arena* arena);

bool create_vulkan_surface(VkInstance instance, VkSurfaceKHR* out_surface);

}
