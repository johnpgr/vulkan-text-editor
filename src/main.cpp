#include "base/arena.h"
#include "base/core.h"
#include "base/log.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "renderer/vulkan.cpp"
#include "input.h"
#include "core.cpp"

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 800

struct AppState {
    Arena* permanent_arena;
    Arena* transient_arena;
    EditorInput input;
    EditorState editor;
    PushCmdBuffer render_cmds;
    f64 last_frame_time;
    f64 fps_log_elapsed;
    u32 fps_log_frame_count;
};

internal AppState* get_app_state(GLFWwindow* window) {
    assert(window != nullptr, "Window must not be null!");

    AppState* app_state = (AppState*)glfwGetWindowUserPointer(window);
    assert(app_state != nullptr, "Window app state must not be null!");
    return app_state;
}

internal void key_callback(
    GLFWwindow* window,
    i32 key,
    i32 scancode,
    i32 action,
    i32 mods
) {
    (void)scancode;

    AppState* app_state = get_app_state(window);
    if(key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }

    editor_input_push_key_event(&app_state->input, key, mods, action);
}

internal void char_callback(GLFWwindow* window, u32 codepoint) {
    AppState* app_state = get_app_state(window);
    editor_input_push_char(&app_state->input, codepoint);
}

internal void scroll_callback(GLFWwindow* window, f64 xoffset, f64 yoffset) {
    (void)xoffset;

    AppState* app_state = get_app_state(window);
    editor_input_push_scroll(&app_state->input, yoffset);
}

int main(void) {
    int result = 0;
    f64 const fps_log_interval_seconds = 2.0;
    GLFWwindow* window = nullptr;
    bool glfw_initialized = false;
    bool renderer_initialized = false;
    AppState app_state = {};

    app_state.permanent_arena = arena_alloc();
    app_state.transient_arena = arena_alloc();
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
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        "editor",
        nullptr,
        nullptr
    );
    if(window == nullptr) {
        char const* description = nullptr;
        glfwGetError(&description);
        LOG_FATAL(
            "glfwCreateWindow failed: %s",
            description != nullptr ? description : "Unknown error"
        );
        result = -1;
        goto cleanup;
    }

    glfwSetWindowUserPointer(window, &app_state);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCharCallback(window, char_callback);
    glfwSetScrollCallback(window, scroll_callback);

    if(!init_vulkan(app_state.permanent_arena, window)) {
        result = -1;
        goto cleanup;
    }
    renderer_initialized = true;
    app_state.last_frame_time = glfwGetTime();

    while(!glfwWindowShouldClose(window)) {
        f64 current_time = glfwGetTime();
        f32 dt_for_frame = (f32)(current_time - app_state.last_frame_time);
        app_state.last_frame_time = current_time;

        editor_input_begin_frame(&app_state.input);
        glfwPollEvents();
        editor_input_snapshot_window(&app_state.input, window, dt_for_frame);

        arena_clear(app_state.transient_arena);
        editor_update_and_render(
            &app_state.editor,
            &app_state.input,
            &app_state.render_cmds
        );

        if(!begin_frame()) {
            result = -1;
            break;
        }

        if(!render_drain_cmd_buffer(&app_state.render_cmds)) {
            result = -1;
            break;
        }

        app_state.fps_log_elapsed += dt_for_frame;
        app_state.fps_log_frame_count += 1;
        if(app_state.fps_log_elapsed >= fps_log_interval_seconds) {
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
    if(renderer_initialized) {
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
