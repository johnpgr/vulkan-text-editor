#include "base/defines.h"

#include "platform/linux.cpp"
#include "platform/macos.mm"
#include "platform/win32.cpp"
#include "renderer/vulkan.cpp"

typedef int (*GameTestSymbolFn)(void);

#define WIDTH 800
#define HEIGHT 600

main {
    (void)renderer::draw_frame;

    Arena global_arena = Arena::create();
    platform::create_window("Unnammed game", WIDTH, HEIGHT);
    platform::set_window_resizable(true);

    if (!renderer::create(&global_arena)) {
        global_arena.release();
        return -1;
    };

    defer {
        renderer::destroy();
        global_arena.release();
    };

    platform::show_window();

    while (!platform::should_window_close()) {
        platform::poll_events();
    }

    return 0;
}
