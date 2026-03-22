#pragma once

#include "base/typedef.h"

struct GameButtonState {
    b32 ended_down;
};

struct GameInput {
    f32 dt_for_frame;
    GameButtonState move_up;
    GameButtonState move_down;
    GameButtonState move_left;
    GameButtonState move_right;
};

struct GameMemory {
    b32 is_initialized;
    u64 permanent_storage_size;
    void *permanent_storage;
    u64 transient_storage_size;
    void *transient_storage;
};

typedef void GameUpdateAndRender(GameMemory *memory, GameInput *input);

#define GAME_UPDATE_AND_RENDER(name)                                           \
    export void name(GameMemory *memory, GameInput *input)
