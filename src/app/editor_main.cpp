// [h]
#include "base/base_mod.h"
#include "os/os_mod.h"

#define RGFW_VULKAN
#define RGFW_IMPORT
#include "third_party/rgfw/RGFW.h"
#undef RGFW_IMPORT
#undef RGFW_VULKAN

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

int main(void) {
    int result = 0;
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

    RGFW_window* window = RGFW_createWindow("Vulkan text editor", 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    if(window == nullptr) {
        LOG_FATAL("RGFW_createWindow failed.");
        result = -1;
        goto cleanup;
    }

    RGFW_window_setExitKey(window, RGFW_keyNULL);
    RGFW_window_setEnabledEvents(window, RGFW_allEventFlags);

    if(!init_vulkan(app_state.permanent_arena, window)) {
        result = -1;
        goto cleanup;
    }
    app_state.running = true;
    app_state.initialized = true;
    app_state.last_frame_time = app_get_time();

    while(!RGFW_window_shouldClose(window)) {
        f64 current_time = app_get_time();
        f32 dt_for_frame = (f32)(current_time - app_state.last_frame_time);
        app_state.last_frame_time = current_time;

        editor_input_begin_frame(&app_state.input);
        RGFW_event event;
        while(RGFW_window_checkEvent(window, &event)) {
            switch(event.type) {
                case RGFW_eventNone:
                case RGFW_keyReleased:
                    break;

                case RGFW_keyPressed: {
                    if(event.key.value == RGFW_keyEscape) {
                        RGFW_window_setShouldClose(window, RGFW_TRUE);
                        app_state.running = false;
                        break;
                    }

                    editor_input_push_key_event(
                        &app_state.input,
                        (i32)event.key.value,
                        (i32)event.key.mod,
                        true,
                        event.key.repeat
                    );
                } break;

                case RGFW_keyChar: {
                    editor_input_push_char(
                        &app_state.input,
                        event.keyChar.value
                    );
                } break;

                case RGFW_mouseScroll: {
                    editor_input_push_scroll(
                        &app_state.input,
                        event.delta.y
                    );
                } break;

                case RGFW_windowClose: {
                    RGFW_window_setShouldClose(window, RGFW_TRUE);
                    app_state.running = false;
                } break;
            }

            if(!app_state.running) {
                break;
            }
        }

        if(!app_state.running) {
            break;
        }

        editor_input_snapshot_window(&app_state.input, window, dt_for_frame);

        arena_clear(app_state.transient_arena);

        if(!begin_frame()) {
            result = -1;
            break;
        }

        editor_update(
            &app_state.editor,
            &app_state.input,
            &app_state.render_cmds
        );

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
        RGFW_window_close(window);
    }
    arena_release(app_state.transient_arena);
    arena_release(app_state.permanent_arena);

    return result;
}
