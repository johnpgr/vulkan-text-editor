#pragma once

#include "draw/draw_core.h"

struct Arena;
struct RGFW_window;

bool init_vulkan(Arena* arena, RGFW_window* window);
void cleanup_vulkan(void);
bool begin_frame(void);
bool render_drain_cmd_buffer(PushCmdBuffer* buffer);
