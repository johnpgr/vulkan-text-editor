#pragma once

#include "input.h"
#include "push_cmds.cpp"

struct EditorState {
    Arena *permanent_arena;
    Arena *transient_arena;
    i32 cursor_column;
    i32 cursor_row;
    f32 blink_timer;
    bool dirty;
};

internal void init_editor_state(
    EditorState *state,
    Arena *permanent_arena,
    Arena *transient_arena
) {
    assert(state != nullptr, "Editor state must not be null!");
    assert(permanent_arena != nullptr, "Permanent arena must not be null!");
    assert(transient_arena != nullptr, "Transient arena must not be null!");

    *state = {};
    state->permanent_arena = permanent_arena;
    state->transient_arena = transient_arena;
    state->dirty = true;
}

internal void move_cursor(EditorState *state, EditorInput *input) {
    assert(state != nullptr, "Editor state must not be null!");
    assert(input != nullptr, "Editor input must not be null!");

    i32 max_columns = 0;
    i32 max_rows = 0;
    if(input->window_width > 128) {
        max_columns = (i32)((input->window_width - 96) / 14);
    }
    if(input->window_height > 144) {
        max_rows = (i32)((input->window_height - 112) / 28);
    }

    bool moved = false;
    for(u32 event_index = 0; event_index < input->key_event_count; ++event_index) {
        KeyEvent *event = input->key_events + event_index;
        switch(event->key) {
            case GLFW_KEY_LEFT: {
                --state->cursor_column;
                moved = true;
            } break;

            case GLFW_KEY_RIGHT: {
                ++state->cursor_column;
                moved = true;
            } break;

            case GLFW_KEY_UP: {
                --state->cursor_row;
                moved = true;
            } break;

            case GLFW_KEY_DOWN: {
                ++state->cursor_row;
                moved = true;
            } break;
        }
    }

    state->cursor_column = clamp(state->cursor_column, 0, max_columns);
    state->cursor_row = clamp(state->cursor_row, 0, max_rows);

    if(moved || input->char_input_count > 0 || input->scroll_delta != 0.0f) {
        state->blink_timer = 0.0f;
        state->dirty = true;
    }
}

internal void push_cursor(EditorState *state, EditorInput *input, PushCmdBuffer *cmds) {
    assert(state != nullptr, "Editor state must not be null!");
    assert(input != nullptr, "Editor input must not be null!");
    assert(cmds != nullptr, "Push command buffer must not be null!");

    f32 const blink_period = 1.0f;
    f32 const cursor_width = 3.0f;
    f32 const cursor_height = 22.0f;
    f32 const margin_left = 96.0f;
    f32 const margin_top = 88.0f;
    f32 const cell_width = 14.0f;
    f32 const cell_height = 28.0f;

    state->blink_timer += input->dt_for_frame;
    while(state->blink_timer >= blink_period) {
        state->blink_timer -= blink_period;
    }

    bool cursor_visible = state->blink_timer < (blink_period * 0.5f);
    if(!cursor_visible) {
        return;
    }

    vec2 position = vec2(
        margin_left + state->cursor_column * cell_width,
        margin_top + state->cursor_row * cell_height
    );
    vec2 center = vec2(
        position.x + 0.5f * cursor_width,
        position.y + 0.5f * cursor_height
    );

    push_rect(
        cmds,
        center,
        vec2(cursor_width, cursor_height),
        vec4(0.94f, 0.96f, 0.99f, 1.0f)
    );
}

internal void editor_update_and_render(
    EditorState *state,
    EditorInput *input,
    PushCmdBuffer *cmds
) {
    assert(state != nullptr, "Editor state must not be null!");
    assert(input != nullptr, "Editor input must not be null!");
    assert(cmds != nullptr, "Push command buffer must not be null!");

    push_cmd_buffer_reset(cmds);
    push_clear(cmds, vec4(0.04f, 0.05f, 0.08f, 1.0f));

    move_cursor(state, input);
    push_cursor(state, input, cmds);
}
