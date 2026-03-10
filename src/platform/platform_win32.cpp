#if OS_WINDOWS

#include <windows.h>

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

internal void platform_fail(const char* message) {
    log_fatal("%s", message);
    abort();
}

internal Arena* win32_scratch(void) {
    if (!win32_scratch_initialized) {
        win32_scratch_arena = Arena::make();
        win32_scratch_initialized = true;
    }
    return &win32_scratch_arena;
}

internal wchar_t* win32_string_to_wide(Arena* scratch, String text) {
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

internal bool win32_executable_dir(wchar_t* buffer, DWORD buffer_count) {
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

internal bool win32_try_load_library(
    const wchar_t* path,
    HMODULE* out_library
) {
    assert_msg(path != nullptr, "Library path must not be null!");
    assert_msg(out_library != nullptr, "Output library must not be null!");

    HMODULE library = LoadLibraryW(path);
    if (!library) {
        return false;
    }

    *out_library = library;
    return true;
}

internal DWORD win32_window_style(void) {
    DWORD style = WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX;
    if (win32_state.resizable) {
        style |= WS_MAXIMIZEBOX | WS_THICKFRAME;
    }
    return style;
}

internal LRESULT CALLBACK
win32_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
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

internal bool win32_register_window_class(void) {
    if (win32_state.class_registered) {
        return true;
    }

    win32_state.instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = win32_window_proc;
    window_class.hInstance = win32_state.instance;
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class.lpszClassName = L"cpp_gaming_window_class";

    win32_state.window_class = RegisterClassExW(&window_class);
    win32_state.class_registered = win32_state.window_class != 0;
    return win32_state.class_registered;
}

internal String
win32_wide_path_to_string(Arena* arena, Arena* scratch, const wchar_t* path) {
    int utf8_length =
        WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    assert_msg(utf8_length > 0, "Wide to UTF-8 conversion failed!");

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
    assert_msg(written == utf8_length, "Wide to UTF-8 conversion failed!");

    return String::copy(arena, String::from_cstr(buffer));
}

internal PlatformErrorCode win32_copy_error(DWORD error) {
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

void platform_window_init(String title, int width, int height) {
    if (win32_state.initialized) {
        platform_window_cleanup();
    }

    win32_state.resizable = true;
    win32_state.width = width;
    win32_state.height = height;
    win32_state.should_close = false;

    if (!win32_register_window_class()) {
        DWORD error = GetLastError();
        log_fatal(
            "RegisterClassExW failed with error %lu",
            (unsigned long)error
        );
        abort();
    }

    Arena* scratch = win32_scratch();
    u64 scratch_mark = scratch->mark();
    wchar_t* wide_title = win32_string_to_wide(
        scratch,
        title.size > 0 ? title : String::lit("cpp-gaming")
    );
    if (!wide_title) {
        scratch->restore(scratch_mark);
        platform_fail("Window title conversion failed.");
    }

    RECT rect = {0, 0, width, height};
    DWORD style = win32_window_style();
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
    scratch->restore(scratch_mark);

    if (!win32_state.window) {
        DWORD error = GetLastError();
        log_fatal(
            "CreateWindowExW failed with error %lu",
            (unsigned long)error
        );
        abort();
    }

    win32_state.initialized = true;
    win32_state.visible = false;
}

void platform_window_cleanup(void) {
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

bool platform_window_should_close(void) {
    return win32_state.should_close;
}

void platform_window_poll_events(void) {
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

void platform_window_set_title(String title) {
    if (!win32_state.window) {
        return;
    }

    Arena* scratch = win32_scratch();
    u64 scratch_mark = scratch->mark();
    wchar_t* wide_title = win32_string_to_wide(scratch, title);
    if (!wide_title) {
        scratch->restore(scratch_mark);
        return;
    }

    SetWindowTextW(win32_state.window, wide_title);
    scratch->restore(scratch_mark);
}

void platform_window_get_size(int* width, int* height) {
    if (width) {
        *width = win32_state.width;
    }
    if (height) {
        *height = win32_state.height;
    }
}

void platform_window_set_size(int width, int height) {
    if (!win32_state.window) {
        return;
    }

    RECT rect = {0, 0, width, height};
    DWORD style = (DWORD)GetWindowLongPtrW(win32_state.window, GWL_STYLE);
    DWORD ex_style = (DWORD)GetWindowLongPtrW(win32_state.window, GWL_EXSTYLE);
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

void platform_window_set_resizable(bool resizable) {
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

void platform_window_present(void) {
    if (win32_state.window) {
        UpdateWindow(win32_state.window);
    }
}

void platform_window_show(void) {
    if (!win32_state.initialized || win32_state.visible ||
        !win32_state.window) {
        return;
    }

    ShowWindow(win32_state.window, SW_SHOW);
    UpdateWindow(win32_state.window);
    win32_state.visible = true;
}

void platform_audio_init(void) {
}

void platform_audio_cleanup(void) {
}

void platform_audio_update_buffer(void) {
}

void platform_audio_set_volume(f32 volume) {
    (void)volume;
}

bool platform_dynamic_library_load(
    Arena* arena,
    String name,
    DynLib* out_library
) {
    if (!arena || !out_library || name.size == 0) {
        return false;
    }

    *out_library = {};
    out_library->arena = arena;

    Arena* scratch = win32_scratch();
    u64 scratch_mark = scratch->mark();
    String filename = str_concat(
        arena,
        str_concat(arena, platform_dynamic_library_prefix(), name),
        platform_dynamic_library_extension()
    );
    wchar_t* wide_filename = win32_string_to_wide(scratch, filename);
    if (!wide_filename) {
        scratch->restore(scratch_mark);
        *out_library = {};
        return false;
    }

    HMODULE module = nullptr;
    wchar_t path_buffer[MAX_PATH] = {};
    if (win32_executable_dir(path_buffer, MAX_PATH)) {
        wchar_t candidate[MAX_PATH] = {};
        _snwprintf_s(
            candidate,
            MAX_PATH,
            _TRUNCATE,
            L"%ls\\%ls",
            path_buffer,
            wide_filename
        );
        if (win32_try_load_library(candidate, &module)) {
            out_library->filename =
                win32_wide_path_to_string(arena, scratch, candidate);
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
            if (win32_try_load_library(candidate, &module)) {
                out_library->filename =
                    win32_wide_path_to_string(arena, scratch, candidate);
            }
        }
    }

    if (!module && win32_try_load_library(wide_filename, &module)) {
        out_library->filename = String::copy(arena, filename);
    }

    if (!module) {
        scratch->restore(scratch_mark);
        *out_library = {};
        return false;
    }

    out_library->arena = arena;
    out_library->name = String::copy(arena, name);
    out_library->internal_data = module;
    out_library->internal_data_size = sizeof(HMODULE);
    out_library->watch_id = 0;
    out_library->functions = ArrayList<DynLibFn>::make(arena);
    scratch->restore(scratch_mark);
    return true;
}

bool platform_dynamic_library_unload(DynLib* library) {
    if (!library || !library->internal_data) {
        return false;
    }

    if (FreeLibrary((HMODULE)library->internal_data) == 0) {
        return false;
    }

    *library = {};
    return true;
}

void* platform_dynamic_library_load_function(String name, DynLib* library) {
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

    Arena* scratch = win32_scratch();
    u64 scratch_mark = scratch->mark();
    const char* symbol_name = name.to_cstr(scratch);
    FARPROC symbol =
        GetProcAddress((HMODULE)library->internal_data, symbol_name);
    if (!symbol) {
        scratch->restore(scratch_mark);
        return nullptr;
    }

    DynLibFn function = {};
    function.name = String::copy(library->arena, name);
    function.pfn = (void*)symbol;
    library->functions.push(function);
    scratch->restore(scratch_mark);
    return function.pfn;
}

String platform_dynamic_library_extension(void) {
    return String::lit(".dll");
}

String platform_dynamic_library_prefix(void) {
    return String::lit("");
}

PlatformErrorCode platform_copy_file(
    String source,
    String dest,
    bool overwrite_if_exists
) {
    Arena* scratch = win32_scratch();
    u64 scratch_mark = scratch->mark();
    wchar_t* wide_source = win32_string_to_wide(scratch, source);
    wchar_t* wide_dest = win32_string_to_wide(scratch, dest);
    if (!wide_source || !wide_dest) {
        scratch->restore(scratch_mark);
        return PlatformErrorCode::PLATFORM_ERROR_UNKNOWN;
    }

    BOOL copied =
        CopyFileW(wide_source, wide_dest, overwrite_if_exists ? FALSE : TRUE);
    DWORD error = copied != 0 ? 0 : GetLastError();
    scratch->restore(scratch_mark);

    if (copied != 0) {
        return PlatformErrorCode::PLATFORM_ERROR_SUCCESS;
    }

    return win32_copy_error(error);
}

#endif
