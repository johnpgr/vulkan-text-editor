#include "base/core.h"

#include "game_api.h"

struct GameState {
    f32 player_x;
    f32 player_y;
};

GAME_UPDATE_AND_RENDER(game_update_and_render) {
    assert(memory != nullptr, "Game memory must not be null!");
    assert(input != nullptr, "Game input must not be null!");
    assert(
        memory->permanent_storage != nullptr,
        "Permanent storage must not be null!"
    );
    assert(
        memory->permanent_storage_size >= sizeof(GameState),
        "Permanent storage too small!"
    );

    GameState *game_state = (GameState *)memory->permanent_storage;
    if(!memory->is_initialized) {
        game_state->player_x = 0.0f;
        game_state->player_y = 0.0f;
        memory->is_initialized = true;
    }

    f32 move_speed = 128.0f;
    if(input->move_left.ended_down) {
        game_state->player_x -= move_speed * input->dt_for_frame;
    }
    if(input->move_right.ended_down) {
        game_state->player_x += move_speed * input->dt_for_frame;
    }
    if(input->move_up.ended_down) {
        game_state->player_y += move_speed * input->dt_for_frame;
    }
    if(input->move_down.ended_down) {
        game_state->player_y -= move_speed * input->dt_for_frame;
    }
}
