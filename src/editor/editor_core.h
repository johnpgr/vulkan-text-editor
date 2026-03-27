#pragma once

#include "draw/draw_core.h"
#include "editor/editor_input.h"
#include "text/text_buffer.h"

struct EditorState {
    Arena* permanent_arena;
    Arena* transient_arena;
    TextDocument* document;
    u32 cursor_anchor;
    i32 desired_column;
    f32 blink_timer;
    bool dirty;
};

void init_editor_state(
    EditorState* state,
    Arena* permanent_arena,
    Arena* transient_arena
);

void editor_update(
    EditorState* state,
    EditorInput* input,
    PushCmdBuffer* cmds
);
