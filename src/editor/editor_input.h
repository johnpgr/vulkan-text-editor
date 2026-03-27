#pragma once

#include "base/base_core.h"

struct Arena;
struct GLFWwindow;

#define EDITOR_INPUT_EVENTS_PER_BLOCK 256

struct KeyEvent {
    i32 key;
    i32 mods;
    bool pressed;
    bool repeated;
};

enum EditorInputEventType : u8 {
    EDITOR_INPUT_EVENT_KEY,
    EDITOR_INPUT_EVENT_CHAR,
    EDITOR_INPUT_EVENT_SCROLL,
};

struct EditorInputEvent {
    EditorInputEventType type;
    union {
        KeyEvent key;
        u32 codepoint;
        f32 scroll_delta;
    };
};

struct EditorInputBlock {
    EditorInputBlock* next;
    u16 count;
    EditorInputEvent events[EDITOR_INPUT_EVENTS_PER_BLOCK];
};

struct EditorInput {
    Arena* arena;
    EditorInputBlock* first_block;
    EditorInputBlock* last_block;
    EditorInputBlock* free_blocks;
    u16 first_block_index;
    f64 mouse_x;
    f64 mouse_y;
    u32 window_width;
    u32 window_height;
    f32 dt_for_frame;
};

void editor_input_init(EditorInput* input, Arena* arena);
void editor_input_snapshot_window(
    EditorInput* input,
    GLFWwindow* window,
    f32 dt_for_frame
);
void editor_input_push_key_event(
    EditorInput* input,
    i32 key,
    i32 mods,
    bool pressed,
    bool repeated
);
void editor_input_push_char(EditorInput* input, u32 codepoint);
void editor_input_push_scroll(EditorInput* input, f64 yoffset);
bool editor_input_pop_event(EditorInput* input, EditorInputEvent* out_event);
