#include "editor/editor_input.h"

#define RGFW_IMPORT
#include "third_party/rgfw/RGFW.h"
#undef RGFW_IMPORT

void editor_input_begin_frame(EditorInput* input) {
    ASSERT(input != nullptr, "Editor input must not be null!");

    input->key_event_count = 0;
    input->char_input_count = 0;
    input->scroll_delta = 0.0f;
}

void editor_input_snapshot_window(
    EditorInput* input,
    RGFW_window* window,
    f32 dt_for_frame
) {
    ASSERT(input != nullptr, "Editor input must not be null!");
    ASSERT(window != nullptr, "Window must not be null!");

    i32 framebuffer_width = 0;
    i32 framebuffer_height = 0;
    RGFW_window_getSizeInPixels(window, &framebuffer_width, &framebuffer_height);
    i32 mouse_x = 0;
    i32 mouse_y = 0;
    RGFW_window_getMouse(window, &mouse_x, &mouse_y);
    input->mouse_x = (f64)mouse_x;
    input->mouse_y = (f64)mouse_y;

    input->window_width = framebuffer_width > 0 ? (u32)framebuffer_width : 0;
    input->window_height = framebuffer_height > 0 ? (u32)framebuffer_height : 0;
    input->dt_for_frame = dt_for_frame;
}

void editor_input_push_key_event(
    EditorInput* input,
    i32 key,
    i32 mods,
    bool pressed,
    bool repeated
) {
    ASSERT(input != nullptr, "Editor input must not be null!");

    if(input->key_event_count >= MAX_KEY_EVENTS) {
        return;
    }

    if(!pressed && !repeated) {
        return;
    }

    KeyEvent* event = input->key_events + input->key_event_count++;
    event->key = key;
    event->mods = mods;
    event->pressed = pressed && !repeated;
    event->repeated = repeated;
}

void editor_input_push_char(EditorInput* input, u32 codepoint) {
    ASSERT(input != nullptr, "Editor input must not be null!");

    if(input->char_input_count >= MAX_CHAR_INPUT_COUNT) {
        return;
    }

    input->char_inputs[input->char_input_count++] = codepoint;
}

void editor_input_push_scroll(EditorInput* input, f64 yoffset) {
    ASSERT(input != nullptr, "Editor input must not be null!");

    input->scroll_delta += (f32)yoffset;
}
