#pragma once

#include "draw/draw_core.h"

struct Arena;
struct GLFWwindow;

bool init_vulkan(Arena* arena, GLFWwindow* window);
void cleanup_vulkan(void);
bool begin_frame(void);
bool render_drain_cmd_buffer(PushCmdBuffer* buffer);
