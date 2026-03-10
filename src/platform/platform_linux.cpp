#if OS_LINUX

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xcb/xcb.h>

struct LinuxPlatformState {
    xcb_connection_t* connection;
    xcb_screen_t* screen;
    xcb_window_t window;
    xcb_atom_t wm_protocols;
    xcb_atom_t wm_delete_window;
    xcb_atom_t net_wm_name;
    xcb_atom_t utf8_string;
    int width;
    int height;
    bool initialized;
    bool should_close;
    bool visible;
    bool resizable;
};

internal LinuxPlatformState linux_state = {};
internal Arena linux_scratch_arena = {};
internal bool linux_scratch_initialized = false;

internal xcb_screen_t* linux_default_screen(xcb_connection_t* connection) {
    const xcb_setup_t* setup = xcb_get_setup(connection);
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
    if (it.rem == 0 || !it.data) {
        return nullptr;
    }
    return it.data;
}

internal xcb_atom_t linux_intern_atom(const char* name) {
    xcb_intern_atom_cookie_t cookie =
        xcb_intern_atom(linux_state.connection, 0, (u16)strlen(name), name);
    xcb_intern_atom_reply_t* reply =
        xcb_intern_atom_reply(linux_state.connection, cookie, nullptr);
    if (!reply) {
        return XCB_ATOM_NONE;
    }

    xcb_atom_t atom = reply->atom;
    free(reply);
    return atom;
}

internal void linux_apply_window_title(String title) {
    if (!linux_state.initialized || linux_state.window == XCB_WINDOW_NONE) {
        return;
    }

    String effective_title = title.size > 0 ? title : String::lit("cpp-gaming");

    xcb_change_property(
        linux_state.connection,
        XCB_PROP_MODE_REPLACE,
        linux_state.window,
        XCB_ATOM_WM_NAME,
        XCB_ATOM_STRING,
        8,
        (u32)effective_title.size,
        effective_title.str
    );

    if (linux_state.net_wm_name != XCB_ATOM_NONE &&
        linux_state.utf8_string != XCB_ATOM_NONE) {
        xcb_change_property(
            linux_state.connection,
            XCB_PROP_MODE_REPLACE,
            linux_state.window,
            linux_state.net_wm_name,
            linux_state.utf8_string,
            8,
            (u32)effective_title.size,
            effective_title.str
        );
    }
}

internal void linux_destroy_window(void) {
    if (linux_state.connection != nullptr &&
        linux_state.window != XCB_WINDOW_NONE) {
        xcb_destroy_window(linux_state.connection, linux_state.window);
        xcb_flush(linux_state.connection);
        linux_state.window = XCB_WINDOW_NONE;
    }
}

internal Arena* linux_scratch(void) {
    if (!linux_scratch_initialized) {
        linux_scratch_arena = Arena::make();
        linux_scratch_initialized = true;
    }
    return &linux_scratch_arena;
}

internal bool linux_executable_dir(char* buffer, usize buffer_size) {
    assert_msg(buffer != nullptr, "Buffer must not be null!");
    assert_msg(buffer_size > 0, "Buffer size must be non-zero!");

    ssize_t size = readlink("/proc/self/exe", buffer, buffer_size - 1);
    if (size <= 0) {
        return false;
    }

    buffer[size] = 0;
    char* slash = strrchr(buffer, '/');
    if (!slash) {
        return false;
    }
    *slash = 0;
    return true;
}

internal bool linux_try_dlopen(const char* path, void** out_handle) {
    assert_msg(path != nullptr, "Library path must not be null!");
    assert_msg(out_handle != nullptr, "Output handle must not be null!");

    dlerror();
    void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        return false;
    }

    *out_handle = handle;
    return true;
}

internal PlatformErrorCode linux_copy_errno(int error) {
    switch (error) {
        case 0:
            return PlatformErrorCode::PLATFORM_ERROR_SUCCESS;
        case ENOENT:
            return PlatformErrorCode::PLATFORM_ERROR_FILE_NOT_FOUND;
        case EEXIST:
            return PlatformErrorCode::PLATFORM_ERROR_FILE_EXISTS;
        case EBUSY:
        case ETXTBSY:
            return PlatformErrorCode::PLATFORM_ERROR_FILE_LOCKED;
        default:
            return PlatformErrorCode::PLATFORM_ERROR_UNKNOWN;
    }
}

void platform_window_init(String title, int width, int height) {
    if (linux_state.initialized) {
        platform_window_cleanup();
    }

    linux_state.connection = xcb_connect(nullptr, nullptr);
    if (!linux_state.connection ||
        xcb_connection_has_error(linux_state.connection) != 0) {
        log_fatal("Failed to connect to the X server.");
        abort();
    }

    linux_state.screen = linux_default_screen(linux_state.connection);
    if (!linux_state.screen) {
        xcb_disconnect(linux_state.connection);
        linux_state = {};
        log_fatal("Failed to acquire the default X screen.");
        abort();
    }

    linux_state.window = xcb_generate_id(linux_state.connection);
    linux_state.width = width;
    linux_state.height = height;
    linux_state.resizable = true;

    u32 event_mask = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    u32 value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    u32 value_list[] = {linux_state.screen->black_pixel, event_mask};

    xcb_void_cookie_t create_cookie = xcb_create_window_checked(
        linux_state.connection,
        XCB_COPY_FROM_PARENT,
        linux_state.window,
        linux_state.screen->root,
        0,
        0,
        (u16)width,
        (u16)height,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        linux_state.screen->root_visual,
        value_mask,
        value_list
    );

    xcb_generic_error_t* create_error =
        xcb_request_check(linux_state.connection, create_cookie);
    if (create_error) {
        free(create_error);
        xcb_disconnect(linux_state.connection);
        linux_state = {};
        log_fatal("Failed to create the XCB window.");
        abort();
    }

    linux_state.wm_protocols = linux_intern_atom("WM_PROTOCOLS");
    linux_state.wm_delete_window = linux_intern_atom("WM_DELETE_WINDOW");
    linux_state.net_wm_name = linux_intern_atom("_NET_WM_NAME");
    linux_state.utf8_string = linux_intern_atom("UTF8_STRING");

    if (linux_state.wm_protocols != XCB_ATOM_NONE &&
        linux_state.wm_delete_window != XCB_ATOM_NONE) {
        xcb_change_property(
            linux_state.connection,
            XCB_PROP_MODE_REPLACE,
            linux_state.window,
            linux_state.wm_protocols,
            XCB_ATOM_ATOM,
            32,
            1,
            &linux_state.wm_delete_window
        );
    }

    linux_apply_window_title(title);
    xcb_flush(linux_state.connection);

    linux_state.should_close = false;
    linux_state.visible = false;
    linux_state.initialized = true;
}

void platform_window_cleanup(void) {
    if (!linux_state.initialized) {
        return;
    }

    linux_destroy_window();
    if (linux_state.connection != nullptr) {
        xcb_disconnect(linux_state.connection);
    }
    linux_state = {};
}

bool platform_window_should_close(void) {
    return linux_state.should_close;
}

void platform_window_poll_events(void) {
    if (!linux_state.initialized || !linux_state.connection) {
        return;
    }

    xcb_generic_event_t* event = nullptr;
    while ((event = xcb_poll_for_event(linux_state.connection)) != nullptr) {
        u8 response_type = (u8)(event->response_type & 0x7f);
        switch (response_type) {
            case XCB_CLIENT_MESSAGE: {
                xcb_client_message_event_t* client_message =
                    (xcb_client_message_event_t*)event;
                if (client_message->type == linux_state.wm_protocols &&
                    client_message->data.data32[0] ==
                        linux_state.wm_delete_window) {
                    linux_state.should_close = true;
                }
            } break;
            case XCB_CONFIGURE_NOTIFY: {
                xcb_configure_notify_event_t* configure =
                    (xcb_configure_notify_event_t*)event;
                linux_state.width = configure->width;
                linux_state.height = configure->height;
            } break;
            case XCB_DESTROY_NOTIFY:
                linux_state.should_close = true;
                break;
            default:
                break;
        }

        free(event);
    }
}

void platform_window_set_title(String title) {
    linux_apply_window_title(title);
    if (linux_state.connection != nullptr) {
        xcb_flush(linux_state.connection);
    }
}

void platform_window_get_size(int* width, int* height) {
    if (width) {
        *width = linux_state.width;
    }
    if (height) {
        *height = linux_state.height;
    }
}

void platform_window_set_size(int width, int height) {
    if (!linux_state.initialized || linux_state.window == XCB_WINDOW_NONE) {
        return;
    }

    u32 values[] = {(u32)width, (u32)height};
    xcb_configure_window(
        linux_state.connection,
        linux_state.window,
        XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
        values
    );
    xcb_flush(linux_state.connection);

    linux_state.width = width;
    linux_state.height = height;
}

void platform_window_set_resizable(bool resizable) {
    linux_state.resizable = resizable;
}

void platform_window_present(void) {
    if (linux_state.connection != nullptr) {
        xcb_flush(linux_state.connection);
    }
}

void platform_window_show(void) {
    if (!linux_state.initialized || linux_state.visible ||
        linux_state.window == XCB_WINDOW_NONE) {
        return;
    }

    xcb_map_window(linux_state.connection, linux_state.window);
    xcb_flush(linux_state.connection);
    linux_state.visible = true;
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

    String filename = str_concat(
        arena,
        str_concat(arena, platform_dynamic_library_prefix(), name),
        platform_dynamic_library_extension()
    );

    const char* filename_cstr = (const char*)filename.str;
    Arena* scratch = linux_scratch();
    u64 scratch_mark = scratch->mark();
    char executable_dir[PATH_MAX] = {};
    bool have_executable_dir =
        linux_executable_dir(executable_dir, sizeof(executable_dir));
    char cwd[PATH_MAX] = {};
    bool have_cwd = getcwd(cwd, sizeof(cwd)) != nullptr;
    void* handle = nullptr;

    if (have_executable_dir) {
        String resolved_path =
            String::fmt(scratch, "%s/%s", executable_dir, filename_cstr);
        if (linux_try_dlopen((const char*)resolved_path.str, &handle)) {
            out_library->filename = String::copy(arena, resolved_path);
        }
    }

    if (!handle && have_cwd) {
        String resolved_path =
            String::fmt(scratch, "%s/%s", cwd, filename_cstr);
        if (linux_try_dlopen((const char*)resolved_path.str, &handle)) {
            out_library->filename = String::copy(arena, resolved_path);
        }
    }

    if (!handle && linux_try_dlopen(filename_cstr, &handle)) {
        out_library->filename = String::copy(arena, filename);
    }

    if (!handle) {
        scratch->restore(scratch_mark);
        *out_library = {};
        return false;
    }

    out_library->arena = arena;
    out_library->name = String::copy(arena, name);
    out_library->internal_data = handle;
    out_library->internal_data_size = sizeof(void*);
    out_library->watch_id = 0;
    out_library->functions = ArrayList<DynLibFn>::make(arena);
    scratch->restore(scratch_mark);
    return true;
}

bool platform_dynamic_library_unload(DynLib* library) {
    if (!library || !library->internal_data) {
        return false;
    }

    if (dlclose(library->internal_data) != 0) {
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

    Arena* scratch = linux_scratch();
    u64 scratch_mark = scratch->mark();
    const char* symbol_name = name.to_cstr(scratch);
    dlerror();
    void* symbol = dlsym(library->internal_data, symbol_name);
    const char* error = dlerror();

    if (error != nullptr || !symbol) {
        log_debug(
            "Failed to load symbol '%s' from '%s': %s",
            symbol_name,
            library->filename.str != nullptr
                ? (const char*)library->filename.str
                : "<unknown>",
            error != nullptr ? error : "symbol not found"
        );
        scratch->restore(scratch_mark);
        return nullptr;
    }

    DynLibFn function = {};
    function.name = String::copy(library->arena, name);
    function.pfn = symbol;
    library->functions.push(function);
    scratch->restore(scratch_mark);
    return symbol;
}

String platform_dynamic_library_extension(void) {
    return String::lit(".so");
}

String platform_dynamic_library_prefix(void) {
    return String::lit("lib");
}

PlatformErrorCode platform_copy_file(
    String source,
    String dest,
    bool overwrite_if_exists
) {
    Arena* scratch = linux_scratch();
    u64 scratch_mark = scratch->mark();
    const char* source_path = source.to_cstr(scratch);
    const char* dest_path = dest.to_cstr(scratch);

    int source_fd = open(source_path, O_RDONLY);
    if (source_fd < 0) {
        PlatformErrorCode result = linux_copy_errno(errno);
        scratch->restore(scratch_mark);
        return result;
    }

    struct stat source_stat = {};
    if (fstat(source_fd, &source_stat) != 0) {
        PlatformErrorCode result = linux_copy_errno(errno);
        close(source_fd);
        scratch->restore(scratch_mark);
        return result;
    }

    int dest_flags = O_WRONLY | O_CREAT;
    dest_flags |= overwrite_if_exists ? O_TRUNC : O_EXCL;

    int dest_fd = open(dest_path, dest_flags, source_stat.st_mode);
    if (dest_fd < 0) {
        PlatformErrorCode result = linux_copy_errno(errno);
        close(source_fd);
        scratch->restore(scratch_mark);
        return result;
    }

    PlatformErrorCode result = PlatformErrorCode::PLATFORM_ERROR_SUCCESS;
    char buffer[64 * KB];
    for (;;) {
        ssize_t bytes_read = read(source_fd, buffer, sizeof(buffer));
        if (bytes_read == 0) {
            break;
        }
        if (bytes_read < 0) {
            result = linux_copy_errno(errno);
            break;
        }

        ssize_t total_written = 0;
        while (total_written < bytes_read) {
            ssize_t bytes_written = write(
                dest_fd,
                buffer + total_written,
                (size_t)(bytes_read - total_written)
            );
            if (bytes_written < 0) {
                result = linux_copy_errno(errno);
                break;
            }
            total_written += bytes_written;
        }

        if (result != PlatformErrorCode::PLATFORM_ERROR_SUCCESS) {
            break;
        }
    }

    close(dest_fd);
    close(source_fd);
    scratch->restore(scratch_mark);
    return result;
}

#endif
