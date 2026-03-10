#pragma once

// Platform API
// This is the OS-specific API that the rest of the engine will call.

struct DynLibFn {
    String name;
    void* pfn;
};

struct DynLib {
    Arena* arena;
    String name;
    String filename;
    u64 internal_data_size;
    void* internal_data;
    u32 watch_id;

    ArrayList<DynLibFn> functions;
};

enum class PlatformErrorCode : u8 {
    PLATFORM_ERROR_SUCCESS = 0,
    PLATFORM_ERROR_UNKNOWN = 1,
    PLATFORM_ERROR_FILE_NOT_FOUND = 2,
    PLATFORM_ERROR_FILE_LOCKED = 3,
    PLATFORM_ERROR_FILE_EXISTS = 4
};

/**
 * @brief Initialize the window system with a game instance
 * @param game Pointer to the game instance containing display settings
 *
 * Creates the application delegate and sets up the Cocoa application.
 */
void platform_window_init(String title, int width, int height);

inline void platform_window_init(const char* title, int width, int height) {
    platform_window_init(String::from_cstr(title), width, height);
}

/**
 * @brief Clean up window resources
 *
 * Performs cleanup of window system resources. Should be called
 * when shutting down the application.
 */
void platform_window_cleanup(void);

/**
 * @brief Check if the window should close
 * @return true if the window should close, false otherwise
 */
bool platform_window_should_close(void);

/**
 * @brief Process pending window events
 *
 * Polls and processes all pending window system events such as
 * keyboard input, mouse input, and window events.
 */
void platform_window_poll_events(void);

/**
 * @brief Set the window title
 * @param title The new title string
 */
void platform_window_set_title(String title);

template <u64 N> inline void platform_window_set_title(const char (&title)[N]) {
    platform_window_set_title({(const u8*)title, N - 1});
}

inline void platform_window_set_title(const char* title) {
    platform_window_set_title(String::from_cstr(title));
}

/**
 * @brief Get the current window size
 * @param width Pointer to store the width (can be nullptr)
 * @param height Pointer to store the height (can be nullptr)
 */
void platform_window_get_size(int* width, int* height);

/**
 * @brief Set the window size
 * @param width The new width in pixels
 * @param height The new height in pixels
 */
void platform_window_set_size(int width, int height);

/**
 * @brief Set whether the window is resizable
 * @param resizable true to make the window resizable, false otherwise
 */
void platform_window_set_resizable(bool resizable);

/**
 * @brief Present the framebuffer to the screen
 *
 * Forces an immediate redraw of the window content. Normally
 * the window redraws automatically, but this can be used for
 * manual control.
 */
void platform_window_present(void);

/**
 * @brief Show the window
 *
 * Makes the window visible if it was previously hidden or minimized.
 */
void platform_window_show(void);

/**
 * @brief Initializes the audio system.
 * @param game A pointer to the game instance.
 */
void platform_audio_init();

/**
 * @brief Shuts down the audio system and releases resources.
 */
void platform_audio_cleanup(void);

/**
 * @brief Updates the audio buffer with new data from the game.
 * This should be called from the main thread after game_update_and_render.
 * @param game A pointer to the game instance with updated audio data.
 */
void platform_audio_update_buffer();

/**
 * @brief Sets the master volume for audio playback.
 * @param volume Volume level (0.0 to 1.0)
 */
void platform_audio_set_volume(f32 volume);

/**
 * @brief Loads a dynamic library.
 *
 * @param arena Arena used to store metadata and loaded symbol names. Required.
 * @param name The name of the library file, *excluding* the extension.
 * Required.
 * @param out_library A pointer to hold the loaded library. Required.
 * @return True on success; otherwise false.
 */
bool platform_dynamic_library_load(
    Arena* arena,
    String name,
    DynLib* out_library
);

template <u64 N>
inline bool platform_dynamic_library_load(
    Arena* arena,
    const char (&name)[N],
    DynLib* out_library
) {
    return platform_dynamic_library_load(
        arena,
        {(const u8*)name, N - 1},
        out_library
    );
}

inline bool platform_dynamic_library_load(
    Arena* arena,
    const char* name,
    DynLib* out_library
) {
    return platform_dynamic_library_load(
        arena,
        String::from_cstr(name),
        out_library
    );
}

/**
 * @brief Unloads the given dynamic library.
 *
 * @param library A pointer to the loaded library. Required.
 * @return True on success; otherwise false.
 */
bool platform_dynamic_library_unload(DynLib* library);

/**
 * @brief Loads an exported function of the given name from the provided loaded
 * library.
 *
 * @param name The function name to be loaded.
 * @param library A pointer to the library to load the function from.
 * @return A pointer to the loaded function if it exists; otherwise 0/null.
 */
void* platform_dynamic_library_load_function(String name, DynLib* library);

template <u64 N>
inline void* platform_dynamic_library_load_function(
    const char (&name)[N],
    DynLib* library
) {
    return platform_dynamic_library_load_function(
        {(const u8*)name, N - 1},
        library
    );
}

inline void* platform_dynamic_library_load_function(
    const char* name,
    DynLib* library
) {
    return platform_dynamic_library_load_function(
        String::from_cstr(name),
        library
    );
}

/**
 * @brief Returns the file extension for the current platform.
 */
String platform_dynamic_library_extension(void);

/**
 * @brief Returns a file prefix for libraries for the current platform.
 */
String platform_dynamic_library_prefix(void);

/**
 * @brief Copies file at source to destination, optionally overwriting.
 *
 * @param source The source file path.
 * @param dest The destination file path.
 * @param overwrite_if_exists Indicates if the file should be overwritten if it
 * exists.
 * @return An error code indicating success or failure.
 */
PlatformErrorCode platform_copy_file(
    String source,
    String dest,
    bool overwrite_if_exists
);

template <u64 SourceN, u64 DestN>
inline PlatformErrorCode platform_copy_file(
    const char (&source)[SourceN],
    const char (&dest)[DestN],
    bool overwrite_if_exists
) {
    return platform_copy_file(
        {(const u8*)source, SourceN - 1},
        {(const u8*)dest, DestN - 1},
        overwrite_if_exists
    );
}

template <u64 SourceN>
inline PlatformErrorCode platform_copy_file(
    const char (&source)[SourceN],
    const char* dest,
    bool overwrite_if_exists
) {
    return platform_copy_file(
        {(const u8*)source, SourceN - 1},
        String::from_cstr(dest),
        overwrite_if_exists
    );
}

template <u64 DestN>
inline PlatformErrorCode platform_copy_file(
    const char* source,
    const char (&dest)[DestN],
    bool overwrite_if_exists
) {
    return platform_copy_file(
        String::from_cstr(source),
        {(const u8*)dest, DestN - 1},
        overwrite_if_exists
    );
}

inline PlatformErrorCode platform_copy_file(
    const char* source,
    const char* dest,
    bool overwrite_if_exists
) {
    return platform_copy_file(
        String::from_cstr(source),
        String::from_cstr(dest),
        overwrite_if_exists
    );
}
