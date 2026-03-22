#pragma once

struct Arena;
struct GLFWwindow;

bool init_vulkan(Arena *arena, GLFWwindow *window);
void cleanup_vulkan(void);
bool draw(void);
