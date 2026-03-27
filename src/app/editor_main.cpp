// [h]
#include "base/base_mod.h"
#include "os/os_mod.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#if OS_WINDOWS
#define NOMINMAX
#include <windows.h>
#else
#include <time.h>
#endif

#include "editor/editor_mod.h"
#include "render/render_mod.h"

// [cpp]
#include "base/base_mod.cpp"
#include "os/os_mod.cpp"
#include "editor/editor_mod.cpp"
#include "render/render_mod.cpp"

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 800
#define FPS_LOG_INTERVAL_SECONDS 2.0F

struct AppState {
    Arena* permanent_arena;
    Arena* transient_arena;
    EditorInput input;
    EditorState editor;
    PushCmdBuffer render_cmds;
    f64 last_frame_time;
    f64 fps_log_elapsed;
    u32 fps_log_frame_count;
    bool initialized;
    bool running;
};

internal f64 app_get_time(void) {
    local_persist bool initialized = false;
    local_persist f64 start_time = 0.0;

    f64 current_time = get_ticks_f64();
    if(!initialized) {
        start_time = current_time;
        initialized = true;
    }

    return current_time - start_time;
}

internal void glfw_key_callback(
    GLFWwindow* window,
    int key,
    int /*scancode*/,
    int action,
    int mods
) {
    AppState* app = (AppState*)glfwGetWindowUserPointer(window);
    if(action == GLFW_PRESS || action == GLFW_REPEAT) {
        if(key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            app->running = false;
            return;
        }
        editor_input_push_key_event(
            &app->input,
            key,
            mods,
            action == GLFW_PRESS,
            action == GLFW_REPEAT
        );
    }
}

internal void glfw_char_callback(GLFWwindow* window, unsigned int codepoint) {
    AppState* app = (AppState*)glfwGetWindowUserPointer(window);
    editor_input_push_char(&app->input, codepoint);
}

internal void glfw_scroll_callback(
    GLFWwindow* window,
    double /*xoffset*/,
    double yoffset
) {
    AppState* app = (AppState*)glfwGetWindowUserPointer(window);
    editor_input_push_scroll(&app->input, yoffset);
}

internal void glfw_window_close_callback(GLFWwindow* window) {
    AppState* app = (AppState*)glfwGetWindowUserPointer(window);
    app->running = false;
}

int main(void) {
    int result = 0;
    AppState app_state = {};
    GLFWwindow* window = nullptr;
    bool glfw_initialized = false;

    app_state.permanent_arena = arena_alloc();
    app_state.transient_arena = arena_alloc();
    editor_input_init(&app_state.input, app_state.permanent_arena);
    app_state.render_cmds =
        create_push_cmd_buffer(app_state.permanent_arena, (u32)(128 * KB));
    init_editor_state(
        &app_state.editor,
        app_state.permanent_arena,
        app_state.transient_arena
    );

    if(!glfwInit()) {
        LOG_FATAL("glfwInit failed.");
        result = -1;
        goto cleanup;
    }
    glfw_initialized = true;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        "Vulkan text editor",
        nullptr,
        nullptr
    );
    if(window == nullptr) {
        LOG_FATAL("glfwCreateWindow failed.");
        result = -1;
        goto cleanup;
    }

    glfwSetWindowUserPointer(window, &app_state);
    glfwSetKeyCallback(window, glfw_key_callback);
    glfwSetCharCallback(window, glfw_char_callback);
    glfwSetScrollCallback(window, glfw_scroll_callback);
    glfwSetWindowCloseCallback(window, glfw_window_close_callback);

    if(!init_vulkan(app_state.permanent_arena, window)) {
        result = -1;
        goto cleanup;
    }
    app_state.running = true;
    app_state.initialized = true;
    app_state.last_frame_time = app_get_time();

    while(!glfwWindowShouldClose(window)) {
        f64 current_time = app_get_time();
        f32 dt_for_frame = (f32)(current_time - app_state.last_frame_time);
        app_state.last_frame_time = current_time;

        glfwPollEvents();

        if(!app_state.running) {
            break;
        }

        editor_input_snapshot_window(&app_state.input, window, dt_for_frame);

        arena_clear(app_state.transient_arena);

        editor_update(
            &app_state.editor,
            &app_state.input,
            &app_state.render_cmds
        );

        if(!begin_frame()) {
            result = -1;
            break;
        }

        if(!render_submit(&app_state.render_cmds)) {
            result = -1;
            break;
        }

        if(!end_frame()) {
            result = -1;
            break;
        }

        app_state.fps_log_elapsed += dt_for_frame;
        app_state.fps_log_frame_count += 1;
        if(app_state.fps_log_elapsed >= FPS_LOG_INTERVAL_SECONDS) {
            f64 average_fps =
                (f64)app_state.fps_log_frame_count / app_state.fps_log_elapsed;
            f64 average_ms_per_frame = 1000.0 / average_fps;
            LOG_INFO(
                "FPS: %.2f (%.2f ms/frame)",
                average_fps,
                average_ms_per_frame
            );

            app_state.fps_log_elapsed = 0.0;
            app_state.fps_log_frame_count = 0;
        }
    }

cleanup:
    if(app_state.initialized) {
        cleanup_vulkan();
    }
    if(window != nullptr) {
        glfwDestroyWindow(window);
    }
    if(glfw_initialized) {
        glfwTerminate();
    }
    arena_release(app_state.transient_arena);
    arena_release(app_state.permanent_arena);

    return result;
}
