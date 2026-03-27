#include "editor/editor_input.h"

#include <GLFW/glfw3.h>

internal EditorInputBlock* editor_input_alloc_block(EditorInput* input) {
    ASSERT(input != nullptr, "Editor input must not be null!");

    EditorInputBlock* block = input->free_blocks;
    if(block != nullptr) {
        input->free_blocks = block->next;
        *block = {};
        return block;
    }

    ASSERT(input->arena != nullptr, "Editor input arena must not be null!");
    block = push_struct(input->arena, EditorInputBlock);
    *block = {};
    return block;
}

internal void editor_input_recycle_head_block(EditorInput* input) {
    ASSERT(input != nullptr, "Editor input must not be null!");

    EditorInputBlock* block = input->first_block;
    ASSERT(block != nullptr, "Editor input queue must not be empty!");

    input->first_block = block->next;
    if(input->first_block == nullptr)
        input->last_block = nullptr;
    input->first_block_index = 0;

    block->next = input->free_blocks;
    block->count = 0;
    input->free_blocks = block;
}

internal EditorInputEvent* editor_input_push_event(EditorInput* input) {
    ASSERT(input != nullptr, "Editor input must not be null!");

    if(input->last_block == nullptr ||
       input->last_block->count >= ARRAY_COUNT(input->last_block->events)) {
        EditorInputBlock* block = editor_input_alloc_block(input);
        if(input->last_block != nullptr)
            input->last_block->next = block;
        else
            input->first_block = block;
        input->last_block = block;
    }

    return &input->last_block->events[input->last_block->count++];
}

void editor_input_init(EditorInput* input, Arena* arena) {
    ASSERT(input != nullptr, "Editor input must not be null!");
    ASSERT(arena != nullptr, "Editor input arena must not be null!");

    *input = {};
    input->arena = arena;
}

void editor_input_snapshot_window(
    EditorInput* input,
    GLFWwindow* window,
    f32 dt_for_frame
) {
    ASSERT(input != nullptr, "Editor input must not be null!");
    ASSERT(window != nullptr, "Window must not be null!");

    i32 framebuffer_width = 0;
    i32 framebuffer_height = 0;
    glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
    f64 mouse_x = 0.0;
    f64 mouse_y = 0.0;
    glfwGetCursorPos(window, &mouse_x, &mouse_y);
    input->mouse_x = mouse_x;
    input->mouse_y = mouse_y;

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

    if(!pressed && !repeated) {
        return;
    }

    EditorInputEvent* event = editor_input_push_event(input);
    event->type = EDITOR_INPUT_EVENT_KEY;
    event->key.key = key;
    event->key.mods = mods;
    event->key.pressed = pressed && !repeated;
    event->key.repeated = repeated;
}

void editor_input_push_char(EditorInput* input, u32 codepoint) {
    ASSERT(input != nullptr, "Editor input must not be null!");

    EditorInputEvent* event = editor_input_push_event(input);
    event->type = EDITOR_INPUT_EVENT_CHAR;
    event->codepoint = codepoint;
}

void editor_input_push_scroll(EditorInput* input, f64 yoffset) {
    ASSERT(input != nullptr, "Editor input must not be null!");

    EditorInputEvent* event = editor_input_push_event(input);
    event->type = EDITOR_INPUT_EVENT_SCROLL;
    event->scroll_delta = (f32)yoffset;
}

bool editor_input_pop_event(EditorInput* input, EditorInputEvent* out_event) {
    ASSERT(input != nullptr, "Editor input must not be null!");
    ASSERT(out_event != nullptr, "Editor input event output must not be null!");

    while(input->first_block != nullptr) {
        EditorInputBlock* block = input->first_block;
        if(input->first_block_index < block->count) {
            *out_event = block->events[input->first_block_index++];
            if(input->first_block_index >= block->count)
                editor_input_recycle_head_block(input);
            return true;
        }

        editor_input_recycle_head_block(input);
    }

    return false;
}
