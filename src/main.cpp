#include "base/defines.h"

#if OS_LINUX
#include "platform/linux.cpp"
#elif OS_MAC
#include "platform/macos.mm"
#elif OS_WINDOWS
#include "platform/win32.cpp"
#else
#error "Unsupported platform"
#endif

#include "renderer/vulkan.cpp"

typedef int (*GameTestSymbolFn)(void);

MAIN {
    (void)rvkDrawFrame;

    Arena global_arena = Arena::make();
    pwCreateWindow("Unnammed game", WIDTH, HEIGHT);
    pwSetWindowResizable(true);

    if (!rvkCreateRenderer(&global_arena)) {
        global_arena.release();
        return -1;
    };

    pwShowWindow();

    while (!pwShouldWindowClose()) {
        pwPollEvents();
    }

    rvkDestroyRenderer();
    global_arena.release();
    return 0;
}
