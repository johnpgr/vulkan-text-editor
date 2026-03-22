#include "base/core.h"
#include "base/log.h"

#include <cstring>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "renderer/vulkan.h"

struct VulkanState {
    VkInstance instance;
    VkApplicationInfo app_info;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    VkSurfaceKHR surface;
    u32 graphics_queue_family_index;
    VkDebugUtilsMessengerEXT debug_messenger;
    bool dynamic_rendering_supported;
    bool initialized;
};

global_variable VulkanState vk_state = {};

#ifndef NDEBUG
global_variable char const *validation_layer_name =
    "VK_LAYER_KHRONOS_validation";
#endif
global_variable char const *portability_subset_extension_name =
    "VK_KHR_portability_subset";

internal char const *
get_glfw_error_string(void) {
    char const *description = nullptr;
    glfwGetError(&description);
    return description != nullptr ? description : "Unknown GLFW error";
}

internal bool find_graphics_queue_family(
    Arena *arena,
    VkPhysicalDevice physical_device,
    u32 *out_queue_family_index
);

internal u32 get_target_api_version(void);

internal u32 score_device(Arena *arena, VkPhysicalDevice physical_device);

internal u32
get_target_api_version(void) {
    u32 api_version = VK_API_VERSION_1_0;
    PFN_vkEnumerateInstanceVersion enumerate_instance_version =
        (PFN_vkEnumerateInstanceVersion)
            vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");

    if(enumerate_instance_version != nullptr &&
       enumerate_instance_version(&api_version) == VK_SUCCESS) {
        return api_version;
    }

    return VK_API_VERSION_1_0;
}

internal bool
has_instance_extension(Arena *arena, char const *extension_name) {
    assume(arena != nullptr);
    assume(extension_name != nullptr);

    u32 extension_count = 0;
    VkResult result = vkEnumerateInstanceExtensionProperties(
        nullptr,
        &extension_count,
        nullptr
    );
    if(result != VK_SUCCESS || extension_count == 0) {
        return false;
    }

    TemporaryMemory temporary_memory = begin_temporary_memory(arena);
    VkExtensionProperties *extensions =
        push_array(arena, extension_count, VkExtensionProperties);

    result = vkEnumerateInstanceExtensionProperties(
        nullptr,
        &extension_count,
        extensions
    );
    if(result != VK_SUCCESS) {
        end_temporary_memory(temporary_memory);
        return false;
    }

    bool found = false;
    for(u32 i = 0; i < extension_count; i++) {
        if(strcmp(extensions[i].extensionName, extension_name) == 0) {
            found = true;
            break;
        }
    }

    end_temporary_memory(temporary_memory);
    return found;
}

internal char const **
get_instance_extensions(Arena *arena, u32 *out_extension_count) {
    assume(arena != nullptr);
    assume(out_extension_count != nullptr);

    u32 glfw_extension_count = 0;
    const char **glfw_extensions =
        glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    if(glfw_extensions == nullptr || glfw_extension_count == 0) {
        *out_extension_count = 0;
        return nullptr;
    }

    u32 extra_extension_count = 0;
    if(has_instance_extension(
           arena,
           VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
       )) {
        extra_extension_count += 1;
    }

#ifndef NDEBUG
    if(has_instance_extension(arena, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        extra_extension_count += 1;
    }
#endif

    u32 extension_count = glfw_extension_count + extra_extension_count;
    const char **extensions = push_array(arena, extension_count, const char *);

    u32 extension_index = 0;
    for(u32 i = 0; i < glfw_extension_count; i++) {
        extensions[extension_index++] = glfw_extensions[i];
    }

    if(has_instance_extension(
           arena,
           VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
       )) {
        extensions[extension_index++] =
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    }

#ifndef NDEBUG
    if(has_instance_extension(arena, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        extensions[extension_index++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }
#endif

    *out_extension_count = extension_index;
    return extensions;
}

internal bool
has_layer(Arena *arena, char const *layer_name) {
    assume(arena != nullptr);
    assume(layer_name != nullptr);

    u32 layer_count = 0;
    VkResult result = vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    if(result != VK_SUCCESS || layer_count == 0) {
        return false;
    }

    TemporaryMemory temporary_memory = begin_temporary_memory(arena);
    VkLayerProperties *layers =
        push_array(arena, layer_count, VkLayerProperties);

    result = vkEnumerateInstanceLayerProperties(&layer_count, layers);
    if(result != VK_SUCCESS) {
        end_temporary_memory(temporary_memory);
        return false;
    }

    bool found = false;
    for(u32 i = 0; i < layer_count; i++) {
        if(strcmp(layers[i].layerName, layer_name) == 0) {
            found = true;
            break;
        }
    }

    end_temporary_memory(temporary_memory);
    return found;
}

#ifndef NDEBUG
internal VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_types,
    const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
    void *user_data
) {
    (void)message_types;
    (void)user_data;

    const char *message =
        (callback_data != nullptr && callback_data->pMessage != nullptr)
            ? callback_data->pMessage
            : "Unknown Vulkan validation message";

    if((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) !=
       0) {
        LOG_ERROR("Vulkan: %s", message);
    } else if(
        (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) !=
        0
    ) {
        LOG_WARN("Vulkan: %s", message);
    } else if(
        (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0
    ) {
        LOG_INFO("Vulkan: %s", message);
    } else {
        LOG_DEBUG("Vulkan: %s", message);
    }

    return VK_FALSE;
}

internal void
build_debug_messenger_create_info(
    VkDebugUtilsMessengerCreateInfoEXT *out_create_info
) {
    assume(out_create_info != nullptr);

    *out_create_info = {};
    out_create_info->sType =
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    out_create_info->messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    out_create_info->messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    out_create_info->pfnUserCallback = debug_callback;
}

internal bool
create_debug_messenger(void) {
    PFN_vkCreateDebugUtilsMessengerEXT create_debug_utils_messenger =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            vk_state.instance,
            "vkCreateDebugUtilsMessengerEXT"
        );
    if(create_debug_utils_messenger == nullptr) {
        return false;
    }

    VkDebugUtilsMessengerCreateInfoEXT create_info = {};
    build_debug_messenger_create_info(&create_info);

    return create_debug_utils_messenger(
               vk_state.instance,
               &create_info,
               nullptr,
               &vk_state.debug_messenger
           ) == VK_SUCCESS;
}
#endif

internal bool
has_device_extension(
    Arena *arena,
    VkPhysicalDevice physical_device,
    const char *extension_name
) {
    assume(arena != nullptr);
    assume(physical_device != VK_NULL_HANDLE);
    assume(extension_name != nullptr);

    u32 extension_count = 0;
    VkResult result = vkEnumerateDeviceExtensionProperties(
        physical_device,
        nullptr,
        &extension_count,
        nullptr
    );
    if(result != VK_SUCCESS || extension_count == 0) {
        return false;
    }

    TemporaryMemory temporary_memory = begin_temporary_memory(arena);
    VkExtensionProperties *extensions =
        push_array(arena, extension_count, VkExtensionProperties);

    result = vkEnumerateDeviceExtensionProperties(
        physical_device,
        nullptr,
        &extension_count,
        extensions
    );
    if(result != VK_SUCCESS) {
        end_temporary_memory(temporary_memory);
        return false;
    }

    bool found = false;
    for(u32 i = 0; i < extension_count; i++) {
        if(strcmp(extensions[i].extensionName, extension_name) == 0) {
            found = true;
            break;
        }
    }

    end_temporary_memory(temporary_memory);
    return found;
}

internal bool
supports_dynamic_rendering(Arena *arena, VkPhysicalDevice physical_device) {
    assume(arena != nullptr);
    assume(physical_device != VK_NULL_HANDLE);

    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties(physical_device, &properties);

    bool has_dynamic_rendering_extension = has_device_extension(
        arena,
        physical_device,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
    );

    VkPhysicalDeviceVulkan13Features features13 = {};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering_features = {};
    dynamic_rendering_features.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;

    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    if(properties.apiVersion >= VK_API_VERSION_1_3) {
        features2.pNext = &features13;
    } else if(has_dynamic_rendering_extension) {
        features2.pNext = &dynamic_rendering_features;
    }

    vkGetPhysicalDeviceFeatures2(physical_device, &features2);

    if(properties.apiVersion >= VK_API_VERSION_1_3) {
        return features13.dynamicRendering == VK_TRUE;
    }

    return has_dynamic_rendering_extension &&
           dynamic_rendering_features.dynamicRendering == VK_TRUE;
}

internal bool
is_device_suitable(Arena *arena, VkPhysicalDevice physical_device) {
    return score_device(arena, physical_device) > 0;
}

internal u32
score_device(Arena *arena, VkPhysicalDevice physical_device) {
    assume(arena != nullptr);
    assume(physical_device != VK_NULL_HANDLE);

    // Check if the device has a graphics queue family that supports presenting
    // to our surface
    u32 graphics_queue_family_index = 0;
    if(!find_graphics_queue_family(
           arena,
           physical_device,
           &graphics_queue_family_index
       )) {
        return 0;
    }

    VkBool32 supports_device_surface = VK_FALSE;
    VkResult surface_support_result = vkGetPhysicalDeviceSurfaceSupportKHR(
        physical_device,
        graphics_queue_family_index,
        vk_state.surface,
        &supports_device_surface
    );
    if(surface_support_result != VK_SUCCESS) {
        return 0;
    }

    // If the device doesn't support presenting to the surface, it's not
    // suitable
    if(!supports_device_surface) {
        return 0;
    }

    // Score the device based on its type and features.
    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties(physical_device, &properties);

    // Discrete GPUs are preferred over integrated ones
    VkPhysicalDeviceFeatures features = {};
    vkGetPhysicalDeviceFeatures(physical_device, &features);

    u32 score = 0;

    // Discrete GPUs are preferred over integrated ones
    if(properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    }

    // Geometry shader support is preferred, but not required
    if(features.geometryShader == VK_TRUE) {
        score += 100;
    }

    // The maximum 2D image dimension is a rough indicator of the GPU's
    // capabilities so we can use it to further differentiate between devices.
    score += properties.limits.maxImageDimension2D;
    return score;
}

internal bool
find_graphics_queue_family(
    Arena *arena,
    VkPhysicalDevice physical_device,
    u32 *out_queue_family_index
) {
    assume(arena != nullptr);
    assume(physical_device != VK_NULL_HANDLE);
    assume(out_queue_family_index != nullptr);

    u32 queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(
        physical_device,
        &queue_family_count,
        nullptr
    );
    if(queue_family_count == 0) {
        return false;
    }

    TemporaryMemory temporary_memory = begin_temporary_memory(arena);
    VkQueueFamilyProperties *queue_families =
        push_array(arena, queue_family_count, VkQueueFamilyProperties);

    vkGetPhysicalDeviceQueueFamilyProperties(
        physical_device,
        &queue_family_count,
        queue_families
    );

    bool found = false;
    for(u32 i = 0; i < queue_family_count; i++) {
        if((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 ||
           queue_families[i].queueCount == 0) {
            continue;
        }

        *out_queue_family_index = i;
        found = true;
        break;
    }

    end_temporary_memory(temporary_memory);
    return found;
}

internal bool
pick_physical_device(Arena *arena) {
    assume(arena != nullptr);

    u32 physical_device_count = 0;
    VkResult result = vkEnumeratePhysicalDevices(
        vk_state.instance,
        &physical_device_count,
        nullptr
    );
    if(result != VK_SUCCESS || physical_device_count == 0) {
        return false;
    }

    TemporaryMemory temporary_memory = begin_temporary_memory(arena);
    VkPhysicalDevice *physical_devices =
        push_array(arena, physical_device_count, VkPhysicalDevice);

    result = vkEnumeratePhysicalDevices(
        vk_state.instance,
        &physical_device_count,
        physical_devices
    );
    if(result != VK_SUCCESS) {
        end_temporary_memory(temporary_memory);
        return false;
    }

    vk_state.physical_device = VK_NULL_HANDLE;
    u32 best_score = 0;

    for(u32 i = 0; i < physical_device_count; i++) {
        if(!is_device_suitable(arena, physical_devices[i])) {
            continue;
        }

        u32 score = score_device(arena, physical_devices[i]);
        if(score <= best_score) {
            continue;
        }

        best_score = score;
        vk_state.physical_device = physical_devices[i];
    }

    b32 found_device = vk_state.physical_device != VK_NULL_HANDLE;
    end_temporary_memory(temporary_memory);
    return found_device;
}

internal bool
create_device(Arena *arena) {
    assume(arena != nullptr);

    if(vk_state.physical_device == VK_NULL_HANDLE) {
        return false;
    }

    if(!find_graphics_queue_family(
           arena,
           vk_state.physical_device,
           &vk_state.graphics_queue_family_index
       )) {
        return false;
    }

    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties(vk_state.physical_device, &properties);

    vk_state.dynamic_rendering_supported =
        supports_dynamic_rendering(arena, vk_state.physical_device);

    float graphics_queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info = {};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = vk_state.graphics_queue_family_index;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &graphics_queue_priority;

    bool needs_dynamic_rendering_extension =
        vk_state.dynamic_rendering_supported &&
        properties.apiVersion < VK_API_VERSION_1_3;
    bool needs_portability_subset_extension = has_device_extension(
        arena,
        vk_state.physical_device,
        portability_subset_extension_name
    );

    const char *device_extensions[2] = {};
    u32 device_extension_count = 0;
    if(needs_dynamic_rendering_extension) {
        device_extensions[device_extension_count++] =
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
    }
    if(needs_portability_subset_extension) {
        device_extensions[device_extension_count++] =
            portability_subset_extension_name;
    }

    VkPhysicalDeviceVulkan13Features features13 = {};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering_features = {};
    dynamic_rendering_features.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    dynamic_rendering_features.dynamicRendering = VK_TRUE;

    VkDeviceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = 1;
    create_info.pQueueCreateInfos = &queue_create_info;
    create_info.enabledExtensionCount = device_extension_count;
    create_info.ppEnabledExtensionNames =
        device_extension_count > 0 ? device_extensions : nullptr;
    if(vk_state.dynamic_rendering_supported &&
       properties.apiVersion >= VK_API_VERSION_1_3) {
        create_info.pNext = &features13;
    } else if(needs_dynamic_rendering_extension) {
        create_info.pNext = &dynamic_rendering_features;
    }

    if(vkCreateDevice(
           vk_state.physical_device,
           &create_info,
           nullptr,
           &vk_state.device
       ) != VK_SUCCESS) {
        return false;
    }

    vkGetDeviceQueue(
        vk_state.device,
        vk_state.graphics_queue_family_index,
        0,
        &vk_state.graphics_queue
    );

    return vk_state.device != VK_NULL_HANDLE &&
           vk_state.graphics_queue != VK_NULL_HANDLE;
}

void
cleanup_vulkan(void) {
    if(vk_state.device != VK_NULL_HANDLE) {
        vkDestroyDevice(vk_state.device, nullptr);
    }
    if(vk_state.surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(vk_state.instance, vk_state.surface, nullptr);
    }
#ifndef NDEBUG
    if(vk_state.debug_messenger != VK_NULL_HANDLE) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_debug_utils_messenger =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                vk_state.instance,
                "vkDestroyDebugUtilsMessengerEXT"
            );
        if(destroy_debug_utils_messenger != nullptr) {
            destroy_debug_utils_messenger(
                vk_state.instance,
                vk_state.debug_messenger,
                nullptr
            );
        }
    }
#endif
    vkDestroyInstance(vk_state.instance, nullptr);
    vk_state = {};
}

bool
init_vulkan(Arena *arena, GLFWwindow *window) {
    assume(arena != nullptr);
    assume(window != nullptr);

    bool result = false;

    if(vk_state.initialized) {
        cleanup_vulkan();
    }

    TemporaryMemory temporary_memory = begin_temporary_memory(arena);
    const char *layers[1] = {};
    u32 layer_count = 0;
    VkInstanceCreateInfo create_info = {};

#ifndef NDEBUG
    bool has_validation_layer = false;
    VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {};
#endif

    VkApplicationInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    info.pApplicationName = "Unnamed Game";
    info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    info.pEngineName = "No Engine";
    info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    info.apiVersion = get_target_api_version();
    vk_state.app_info = info;

    u32 extension_count = 0;
    const char **extensions = get_instance_extensions(arena, &extension_count);
    if(extension_count == 0 || extensions == nullptr) {
        LOG_FATAL(
            "glfwGetRequiredInstanceExtensions failed: %s",
            get_glfw_error_string()
        );
        goto cleanup;
    }

#ifndef NDEBUG
    has_validation_layer = has_layer(arena, validation_layer_name);
    if(has_validation_layer) {
        layers[layer_count++] = validation_layer_name;
    }
#endif

    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &vk_state.app_info;
    create_info.enabledExtensionCount = extension_count;
    create_info.ppEnabledExtensionNames = extensions;
    create_info.enabledLayerCount = layer_count;
    create_info.ppEnabledLayerNames = layer_count > 0 ? layers : nullptr;
    if(has_instance_extension(
           arena,
           VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
       )) {
        create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

#ifndef NDEBUG
    if(has_instance_extension(arena, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        build_debug_messenger_create_info(&debug_create_info);
        create_info.pNext = &debug_create_info;
    }
#endif

    if(vkCreateInstance(&create_info, nullptr, &vk_state.instance) !=
       VK_SUCCESS) {
        LOG_FATAL("Failed to create VK Instance!");
        goto cleanup;
    }

#ifndef NDEBUG
    if(has_instance_extension(arena, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) &&
       !create_debug_messenger()) {
        LOG_WARN("Failed to create Vulkan debug messenger.");
    }
#endif

    // The surface must be created after the instance and before picking a
    // physical device
    if(glfwCreateWindowSurface(
           vk_state.instance,
           window,
           nullptr,
           &vk_state.surface
       ) != VK_SUCCESS) {
        LOG_FATAL(
            "glfwCreateWindowSurface failed: %s",
            get_glfw_error_string()
        );
        goto cleanup;
    }

    if(!pick_physical_device(arena)) {
        LOG_FATAL(
            "Failed to find a Vulkan physical device that can present to the "
            "window!"
        );
        goto cleanup;
    }

    if(!create_device(arena)) {
        LOG_FATAL("Failed to create Vulkan logical device!");
        goto cleanup;
    }

    vk_state.initialized = true;
    result = true;

cleanup:
    if(!result && vk_state.instance != VK_NULL_HANDLE) {
        cleanup_vulkan();
    }

    end_temporary_memory(temporary_memory);
    return result;
}

bool
draw(void) {
    return true;
}
