#pragma once

#include "base/core.h"

#define MAX_KEY_EVENTS 128
#define MAX_CHAR_INPUT_COUNT 64

struct KeyEvent {
    i32 key;
    i32 mods;
    bool pressed;
    bool repeated;
};

struct EditorInput {
    KeyEvent key_events[MAX_KEY_EVENTS];
    u32 key_event_count;
    u32 char_inputs[MAX_CHAR_INPUT_COUNT];
    u32 char_input_count;
    f32 scroll_delta;
    f64 mouse_x;
    f64 mouse_y;
    u32 window_width;
    u32 window_height;
    f32 dt_for_frame;
};

internal void editor_input_begin_frame(EditorInput* input) {
    assert(input != nullptr, "Editor input must not be null!");

    input->key_event_count = 0;
    input->char_input_count = 0;
    input->scroll_delta = 0.0f;
}

internal void editor_input_snapshot_window(
    EditorInput* input,
    GLFWwindow* window,
    f32 dt_for_frame
) {
    assert(input != nullptr, "Editor input must not be null!");
    assert(window != nullptr, "Window must not be null!");

    int framebuffer_width = 0;
    int framebuffer_height = 0;
    glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
    glfwGetCursorPos(window, &input->mouse_x, &input->mouse_y);

    input->window_width = framebuffer_width > 0 ? (u32)framebuffer_width : 0;
    input->window_height = framebuffer_height > 0 ? (u32)framebuffer_height : 0;
    input->dt_for_frame = dt_for_frame;
}

internal void editor_input_push_key_event(
    EditorInput* input,
    i32 key,
    i32 mods,
    i32 action
) {
    assert(input != nullptr, "Editor input must not be null!");

    if(input->key_event_count >= MAX_KEY_EVENTS) {
        return;
    }

    if(action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }

    KeyEvent* event = input->key_events + input->key_event_count++;
    event->key = key;
    event->mods = mods;
    event->pressed = action == GLFW_PRESS;
    event->repeated = action == GLFW_REPEAT;
}

internal void editor_input_push_char(EditorInput* input, u32 codepoint) {
    assert(input != nullptr, "Editor input must not be null!");

    if(input->char_input_count >= MAX_CHAR_INPUT_COUNT) {
        return;
    }

    input->char_inputs[input->char_input_count++] = codepoint;
}

internal void editor_input_push_scroll(EditorInput* input, f64 yoffset) {
    assert(input != nullptr, "Editor input must not be null!");

    input->scroll_delta += (f32)yoffset;
}
