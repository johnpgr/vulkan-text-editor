#pragma once

namespace platform {

void create_window(String title, int width, int height);

inline void create_window(const char* title, int width, int height) {
    create_window(String::from_cstr(title), width, height);
}

void destroy_window(void);
bool should_window_close(void);
void poll_events(void);
void set_window_title(String title);

template <u64 N> inline void set_window_title(const char (&title)[N]) {
    set_window_title({(const u8*)title, N - 1});
}

inline void set_window_title(const char* title) {
    set_window_title(String::from_cstr(title));
}

void get_window_size(int* width, int* height);
void set_window_size(int width, int height);
void set_window_resizable(bool resizable);
void present_window(void);
void show_window(void);

}
