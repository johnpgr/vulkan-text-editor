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
    (void)Renderer::DrawFrame;

    Arena global_arena = CreateArena();
    Platform::CreateWindow("Unnammed game", WIDTH, HEIGHT);
    Platform::SetWindowResizable(true);

    if (!Renderer::Create(&global_arena)) {
        global_arena.release();
        return -1;
    };

    DEFER {
        Renderer::Destroy();
        global_arena.release();
    };

    Platform::ShowWindow();

    while (!Platform::ShouldWindowClose()) {
        Platform::PollEvents();
    }

    return 0;
}
