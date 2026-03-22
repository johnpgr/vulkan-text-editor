#include "base/arena.h"
#include "base/core.h"
#include "base/log.h"

#include "game_api.h"

#define GLFW_INCLUDE_NONE
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <dlfcn.h>
#include <cstdio>
#include <cstring>

#if OS_MAC
#include <mach-o/dyld.h>
#elif OS_LINUX
#include <unistd.h>
#endif

#include "renderer/vulkan.cpp"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define PERMANENT_STORAGE_SIZE (64 * MB)
#define TRANSIENT_STORAGE_SIZE (64 * MB)

struct GameCode {
    void *library;
    GameUpdateAndRender *update_and_render;
    b32 is_valid;
};

global_variable Arena global_arena;
global_variable GLFWwindow *global_window;
global_variable b32 global_glfw_initialized;
global_variable b32 global_renderer_initialized;
global_variable GameCode global_game_code;
global_variable GameMemory global_game_memory;

internal void
unload_game_code(GameCode *game_code) {
    if(game_code->library != nullptr) {
        dlclose(game_code->library);
    }

    game_code->library = nullptr;
    game_code->update_and_render = nullptr;
    game_code->is_valid = false;
}

internal b32
get_executable_directory(char *buffer, u64 buffer_size) {
    assert(buffer != nullptr, "Executable path buffer must not be null!");
    assert(buffer_size > 0, "Executable path buffer must not be empty!");

#if OS_MAC
    u32 path_size = (u32)buffer_size;
    if(_NSGetExecutablePath(buffer, &path_size) != 0) {
        return false;
    }

    buffer[buffer_size - 1] = 0;
#elif OS_LINUX
    ssize_t size_read = readlink("/proc/self/exe", buffer, buffer_size - 1);
    if(size_read <= 0) {
        return false;
    }

    buffer[size_read] = 0;
#else
    return false;
#endif

    char *last_slash = strrchr(buffer, '/');
    if(last_slash == nullptr) {
        return false;
    }

    *last_slash = 0;
    return true;
}

internal b32
build_game_library_path(char *buffer, u64 buffer_size) {
    assert(buffer != nullptr, "Game path buffer must not be null!");

    char executable_directory[4096] = {};
    if(!get_executable_directory(
           executable_directory,
           sizeof(executable_directory)
       )) {
        return false;
    }

#if OS_MAC
    char const *library_name = "libgame.dylib";
#elif OS_LINUX
    char const *library_name = "libgame.so";
#else
    char const *library_name = "libgame";
#endif

    int written = snprintf(
        buffer,
        buffer_size,
        "%s/%s",
        executable_directory,
        library_name
    );
    return (written > 0) && ((u64)written < buffer_size);
}

internal b32
load_game_code(GameCode *game_code) {
    assert(game_code != nullptr, "Game code must not be null!");

    char library_path[4096] = {};
    if(!build_game_library_path(library_path, sizeof(library_path))) {
        LOG_FATAL("Failed to build game library path.");
        return false;
    }

    void *library = dlopen(library_path, RTLD_NOW);
    if(library == nullptr) {
        char const *error = dlerror();
        LOG_FATAL(
            "dlopen failed for %s: %s",
            library_path,
            error != nullptr ? error : "Unknown error"
        );
        return false;
    }

    GameUpdateAndRender *update_and_render =
        (GameUpdateAndRender *)dlsym(library, "game_update_and_render");
    if(update_and_render == nullptr) {
        char const *error = dlerror();
        LOG_FATAL(
            "dlsym failed for game_update_and_render: %s",
            error != nullptr ? error : "Unknown error"
        );
        dlclose(library);
        return false;
    }

    game_code->library = library;
    game_code->update_and_render = update_and_render;
    game_code->is_valid = true;
    return true;
}

internal void
cleanup(void) {
    unload_game_code(&global_game_code);

    if(global_renderer_initialized) {
        cleanup_vulkan();
        global_renderer_initialized = false;
    }

    if(global_window != nullptr) {
        glfwDestroyWindow(global_window);
        global_window = nullptr;
    }

    if(global_glfw_initialized) {
        glfwTerminate();
        global_glfw_initialized = false;
    }

    release_arena(&global_arena);
}

internal void
update_input(GLFWwindow *window, GameInput *input) {
    assert(window != nullptr, "Window must not be null!");
    assert(input != nullptr, "Input must not be null!");

    input->move_up.ended_down = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
    input->move_down.ended_down = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
    input->move_left.ended_down = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
    input->move_right.ended_down = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
}

int
main(void) {
    int result = 0;
    char const *description = nullptr;
    f64 last_counter = 0.0;

    global_arena = create_arena();
    global_game_memory.permanent_storage_size = PERMANENT_STORAGE_SIZE;
    global_game_memory.permanent_storage =
        push_size(&global_arena, PERMANENT_STORAGE_SIZE);
    global_game_memory.transient_storage_size = TRANSIENT_STORAGE_SIZE;
    global_game_memory.transient_storage =
        push_size(&global_arena, TRANSIENT_STORAGE_SIZE);

    if(!glfwInit()) {
        LOG_FATAL("glfwInit failed");
        result = -1;
        goto cleanup_label;
    }
    global_glfw_initialized = true;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    global_window = glfwCreateWindow(
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        "The Game",
        nullptr,
        nullptr
    );
    if(global_window == nullptr) {
        glfwGetError(&description);
        LOG_FATAL(
            "glfwCreateWindow failed: %s",
            description != nullptr ? description : "Unknown error"
        );
        result = -1;
        goto cleanup_label;
    }

    if(!load_game_code(&global_game_code)) {
        result = -1;
        goto cleanup_label;
    }

    if(!init_vulkan(&global_arena, global_window)) {
        result = -1;
        goto cleanup_label;
    }
    global_renderer_initialized = true;

    last_counter = glfwGetTime();
    while(!glfwWindowShouldClose(global_window)) {
        glfwPollEvents();

        f64 current_counter = glfwGetTime();
        GameInput input = {};
        input.dt_for_frame = (f32)(current_counter - last_counter);
        last_counter = current_counter;
        update_input(global_window, &input);

        global_game_code.update_and_render(&global_game_memory, &input);

        if(!draw()) {
            result = -1;
            break;
        }
    }

cleanup_label:
    cleanup();
    return result;
}
