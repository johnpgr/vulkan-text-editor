#if OS_WINDOWS

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <vulkan/vulkan_win32.h>

struct Win32PlatformState {
    HINSTANCE instance;
    ATOM window_class;
    HWND window;
    int width;
    int height;
    bool class_registered;
    bool initialized;
    bool should_close;
    bool visible;
    bool resizable;
};

internal Win32PlatformState win32_state = {};
internal Arena win32_scratch_arena = {};
internal bool win32_scratch_initialized = false;

internal void pFail(const char* message) {
    LOG_FATAL("%s", message);
    abort();
}

internal Arena* pGetScratchWin32(void) {
    if (!win32_scratch_initialized) {
        win32_scratch_arena = Arena::make();
        win32_scratch_initialized = true;
    }
    return &win32_scratch_arena;
}

internal wchar_t* pToWideStringWin32(Arena* scratch, String text) {
    int utf8_length = (int)text.size;
    int wide_length = MultiByteToWideChar(
        CP_UTF8,
        0,
        (const char*)text.str,
        utf8_length,
        nullptr,
        0
    );
    if (wide_length <= 0) {
        return nullptr;
    }

    wchar_t* result =
        scratch->push<wchar_t>((u64)wide_length + 1, alignof(wchar_t));

    int written = MultiByteToWideChar(
        CP_UTF8,
        0,
        (const char*)text.str,
        utf8_length,
        result,
        wide_length
    );
    if (written != wide_length) {
        return nullptr;
    }

    result[wide_length] = 0;
    return result;
}

internal bool pGetExecutableDirWin32(wchar_t* buffer, DWORD buffer_count) {
    DWORD length = GetModuleFileNameW(nullptr, buffer, buffer_count);
    if (length == 0 || length == buffer_count) {
        return false;
    }

    wchar_t* slash = wcsrchr(buffer, L'\\');
    if (!slash) {
        return false;
    }
    *slash = 0;
    return true;
}

internal bool pdlTryLoadLibraryWin32(
    const wchar_t* path,
    HMODULE* out_library
) {
    assert(path != nullptr, "Library path must not be null!");
    assert(out_library != nullptr, "Output handle must not be null!");

    HMODULE library = LoadLibraryW(path);
    if (!library) {
        return false;
    }

    *out_library = library;
    return true;
}

internal DWORD pwGetWindowStyleWin32(void) {
    DWORD style = WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX;
    if (win32_state.resizable) {
        style |= WS_MAXIMIZEBOX | WS_THICKFRAME;
    }
    return style;
}

internal LRESULT CALLBACK
pwWindowProcWin32(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CLOSE:
            win32_state.should_close = true;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            win32_state.window = nullptr;
            win32_state.visible = false;
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            win32_state.width = (int)LOWORD(lparam);
            win32_state.height = (int)HIWORD(lparam);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

internal bool pwRegisterWindowClassWin32(void) {
    if (win32_state.class_registered) {
        return true;
    }

    win32_state.instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = pwWindowProcWin32;
    window_class.hInstance = win32_state.instance;
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    window_class.lpszClassName = L"cpp_gaming_window_class";

    win32_state.window_class = RegisterClassExW(&window_class);
    win32_state.class_registered = win32_state.window_class != 0;
    return win32_state.class_registered;
}

internal String pFromWidePathWin32(
    Arena* arena,
    Arena* scratch,
    const wchar_t* path
) {
    int utf8_length = WideCharToMultiByte(
        CP_UTF8,
        0,
        path,
        -1,
        nullptr,
        0,
        nullptr,
        nullptr
    );
    assert(utf8_length > 0, "Wide to UTF-8 conversion failed!");

    char* buffer = scratch->push<char>((u64)utf8_length, alignof(char));

    int written = WideCharToMultiByte(
        CP_UTF8,
        0,
        path,
        -1,
        buffer,
        utf8_length,
        nullptr,
        nullptr
    );
    assert(written == utf8_length, "Wide to UTF-8 conversion failed!");

    return String::copy(arena, String::fromCStr(buffer));
}

internal PlatformErrorCode pfsGetCopyErrorWin32(DWORD error) {
    switch (error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return PlatformErrorCode::PLATFORM_ERROR_FILE_NOT_FOUND;
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            return PlatformErrorCode::PLATFORM_ERROR_FILE_LOCKED;
        case ERROR_FILE_EXISTS:
        case ERROR_ALREADY_EXISTS:
            return PlatformErrorCode::PLATFORM_ERROR_FILE_EXISTS;
        default:
            return PlatformErrorCode::PLATFORM_ERROR_UNKNOWN;
    }
}

void pwCreateWindow(String title, int width, int height) {
    if (win32_state.initialized) {
        pwDestroyWindow();
    }

    win32_state.resizable = false;
    win32_state.width = width;
    win32_state.height = height;
    win32_state.should_close = false;

    if (!pwRegisterWindowClassWin32()) {
        DWORD error = GetLastError();
        LOG_FATAL(
            "RegisterClassExW failed with error %lu",
            (unsigned long)error
        );
        abort();
    }

    Arena* scratch = pGetScratchWin32();
    u64 scratch_mark = scratch->mark();
    defer {
        scratch->restore(scratch_mark);
    };

    wchar_t* wide_title = pToWideStringWin32(
        scratch,
        title.size > 0 ? title : String::lit("cpp-gaming")
    );
    if (!wide_title) {
        pFail("Window title conversion failed.");
    }

    RECT rect = {0, 0, width, height};
    DWORD style = pwGetWindowStyleWin32();
    AdjustWindowRectEx(&rect, style, FALSE, 0);

    win32_state.window = CreateWindowExW(
        0,
        L"cpp_gaming_window_class",
        wide_title,
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        win32_state.instance,
        nullptr
    );

    if (!win32_state.window) {
        DWORD error = GetLastError();
        LOG_FATAL(
            "CreateWindowExW failed with error %lu",
            (unsigned long)error
        );
        abort();
    }

    win32_state.initialized = true;
    win32_state.visible = false;
}

void pwDestroyWindow(void) {
    if (!win32_state.initialized) {
        return;
    }

    if (win32_state.window) {
        DestroyWindow(win32_state.window);
        win32_state.window = nullptr;
    }

    if (win32_state.class_registered) {
        UnregisterClassW(L"cpp_gaming_window_class", win32_state.instance);
    }

    win32_state = {};
}

bool pwShouldWindowClose(void) {
    return win32_state.should_close;
}

void pwPollEvents(void) {
    if (!win32_state.initialized) {
        return;
    }

    MSG message = {};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
        if (message.message == WM_QUIT) {
            win32_state.should_close = true;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void pwSetWindowTitle(String title) {
    if (!win32_state.window) {
        return;
    }

    Arena* scratch = pGetScratchWin32();
    u64 scratch_mark = scratch->mark();
    defer {
        scratch->restore(scratch_mark);
    };

    wchar_t* wide_title = pToWideStringWin32(scratch, title);
    if (!wide_title) {
        return;
    }

    SetWindowTextW(win32_state.window, wide_title);
}

void pwGetWindowSize(int* width, int* height) {
    if (width) {
        *width = win32_state.width;
    }
    if (height) {
        *height = win32_state.height;
    }
}

void pwSetWindowSize(int width, int height) {
    if (!win32_state.window) {
        return;
    }

    RECT rect = {0, 0, width, height};
    DWORD style = (DWORD)GetWindowLongPtrW(win32_state.window, GWL_STYLE);
    DWORD ex_style =
        (DWORD)GetWindowLongPtrW(win32_state.window, GWL_EXSTYLE);
    AdjustWindowRectEx(&rect, style, FALSE, ex_style);

    SetWindowPos(
        win32_state.window,
        nullptr,
        0,
        0,
        rect.right - rect.left,
        rect.bottom - rect.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
    );

    win32_state.width = width;
    win32_state.height = height;
}

void pwSetWindowResizable(bool resizable) {
    win32_state.resizable = resizable;
    if (!win32_state.window) {
        return;
    }

    LONG_PTR style = GetWindowLongPtrW(win32_state.window, GWL_STYLE);
    style &= ~(WS_MAXIMIZEBOX | WS_THICKFRAME);
    if (resizable) {
        style |= WS_MAXIMIZEBOX | WS_THICKFRAME;
    }

    SetWindowLongPtrW(win32_state.window, GWL_STYLE, style);
    SetWindowPos(
        win32_state.window,
        nullptr,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
            SWP_FRAMECHANGED
    );
}

void pwPresentWindow(void) {
    if (win32_state.window) {
        UpdateWindow(win32_state.window);
    }
}

void pwShowWindow(void) {
    if (!win32_state.initialized || win32_state.visible ||
        !win32_state.window) {
        return;
    }

    ShowWindow(win32_state.window, SW_SHOW);
    UpdateWindow(win32_state.window);
    win32_state.visible = true;
}

void paCreateAudio(void) {
}

void paDestroyAudio(void) {
}

void paUpdateAudioBuffer(void) {
}

void paSetAudioVolume(f32 volume) {
    (void)volume;
}

bool pdlLoadLibrary(Arena* arena, String name, DynLib* out_library) {
    if (!arena || !out_library || name.size == 0) {
        return false;
    }

    *out_library = {};
    out_library->arena = arena;

    Arena* scratch = pGetScratchWin32();
    u64 scratch_mark = scratch->mark();
    defer {
        scratch->restore(scratch_mark);
    };

    String filename = strConcat(
        arena,
        strConcat(arena, pdlGetLibraryPrefix(), name),
        pdlGetLibraryExtension()
    );
    wchar_t* wide_filename = pToWideStringWin32(scratch, filename);
    if (!wide_filename) {
        *out_library = {};
        return false;
    }

    HMODULE module = nullptr;
    wchar_t path_buffer[MAX_PATH] = {};
    if (pGetExecutableDirWin32(path_buffer, MAX_PATH)) {
        wchar_t candidate[MAX_PATH] = {};
        _snwprintf_s(
            candidate,
            MAX_PATH,
            _TRUNCATE,
            L"%ls\\%ls",
            path_buffer,
            wide_filename
        );
        if (pdlTryLoadLibraryWin32(candidate, &module)) {
            out_library->filename =
                pFromWidePathWin32(arena, scratch, candidate);
        }
    }

    if (!module) {
        DWORD cwd_length = GetCurrentDirectoryW(MAX_PATH, path_buffer);
        if (cwd_length > 0 && cwd_length < MAX_PATH) {
            wchar_t candidate[MAX_PATH] = {};
            _snwprintf_s(
                candidate,
                MAX_PATH,
                _TRUNCATE,
                L"%ls\\%ls",
                path_buffer,
                wide_filename
            );
            if (pdlTryLoadLibraryWin32(candidate, &module)) {
                out_library->filename =
                    pFromWidePathWin32(arena, scratch, candidate);
            }
        }
    }

    if (!module && pdlTryLoadLibraryWin32(wide_filename, &module)) {
        out_library->filename = String::copy(arena, filename);
    }

    if (!module) {
        *out_library = {};
        return false;
    }

    out_library->name = String::copy(arena, name);
    out_library->internal_data = module;
    out_library->internal_data_size = sizeof(HMODULE);
    out_library->watch_id = 0;
    out_library->functions = ArrayList<DynLibFn>::make(arena);
    return true;
}

bool pdlUnloadLibrary(DynLib* library) {
    if (!library || !library->internal_data) {
        return false;
    }

    if (FreeLibrary((HMODULE)library->internal_data) == 0) {
        return false;
    }

    *library = {};
    return true;
}

void* pdlLoadFunction(String name, DynLib* library) {
    if (!library || !library->internal_data || !library->arena ||
        name.size == 0) {
        return nullptr;
    }

    for (ArrayListNode<DynLibFn>* node = library->functions.first;
         node != nullptr;
         node = node->next) {
        if (node->value.name.equals(name)) {
            return node->value.pfn;
        }
    }

    Arena* scratch = pGetScratchWin32();
    u64 scratch_mark = scratch->mark();
    defer {
        scratch->restore(scratch_mark);
    };

    const char* symbol_name = name.toCStr(scratch);
    FARPROC symbol = GetProcAddress((HMODULE)library->internal_data, symbol_name);
    if (!symbol) {
        return nullptr;
    }

    DynLibFn function = {};
    function.name = String::copy(library->arena, name);
    function.pfn = (void*)symbol;
    library->functions.push(function);
    return function.pfn;
}

String pdlGetLibraryExtension(void) {
    return String::lit(".dll");
}

String pdlGetLibraryPrefix(void) {
    return String::lit("");
}

PlatformErrorCode pfsCopyFile(
    String source,
    String dest,
    bool overwrite_if_exists
) {
    Arena* scratch = pGetScratchWin32();
    u64 scratch_mark = scratch->mark();
    defer {
        scratch->restore(scratch_mark);
    };

    wchar_t* wide_source = pToWideStringWin32(scratch, source);
    wchar_t* wide_dest = pToWideStringWin32(scratch, dest);
    if (!wide_source || !wide_dest) {
        return PlatformErrorCode::PLATFORM_ERROR_UNKNOWN;
    }

    BOOL copied =
        CopyFileW(wide_source, wide_dest, overwrite_if_exists ? FALSE : TRUE);
    DWORD error = copied != 0 ? 0 : GetLastError();

    if (copied != 0) {
        return PlatformErrorCode::PLATFORM_ERROR_SUCCESS;
    }

    return pfsGetCopyErrorWin32(error);
}

ArrayList<const char*> pvkGetInstanceExtensions(Arena* arena) {
    ASSUME(arena != nullptr);

    ArrayList<const char*> extensions = ArrayList<const char*>::make(arena);
    extensions.push(VK_KHR_SURFACE_EXTENSION_NAME);
    extensions.push(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    return extensions;
}

bool pvkCreateSurface(VkInstance instance, VkSurfaceKHR* out_surface) {
    if (instance == VK_NULL_HANDLE || out_surface == nullptr ||
        win32_state.instance == nullptr || win32_state.window == nullptr) {
        return false;
    }

    *out_surface = VK_NULL_HANDLE;

    VkWin32SurfaceCreateInfoKHR create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    create_info.hinstance = win32_state.instance;
    create_info.hwnd = win32_state.window;

    VkResult result =
        vkCreateWin32SurfaceKHR(instance, &create_info, nullptr, out_surface);
    return result == VK_SUCCESS;
}

#endif
