#include "base/defines.h"

#if OS_LINUX
#include "memory/memory_linux.cpp"
#include "platform/platform_linux.cpp"
#elif OS_MAC
#include "memory/memory_macos.cpp"
#include "platform/platform_macos.cpp"
#elif OS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include "memory/memory_win32.cpp"
#include "platform/platform_win32.cpp"
#else
#error "Unsupported platform"
#endif

typedef int (*GameTestSymbolFn)(void);

MAIN {
    Arena global_arena = Arena::make();
    platform_window_init("Unnammed game", 1024, 768);
    platform_window_show();

    while (!platform_window_should_close()) {
        platform_window_poll_events();
    }

    global_arena.release();
    return 0;
}
