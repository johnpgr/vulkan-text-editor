#pragma once

namespace Platform {

void CreateWindow(String title, int width, int height);

inline void CreateWindow(const char* title, int width, int height) {
    CreateWindow(String::fromCstr(title), width, height);
}

void DestroyWindow(void);
bool ShouldWindowClose(void);
void PollEvents(void);
void SetWindowTitle(String title);

template <u64 N> inline void SetWindowTitle(const char (&title)[N]) {
    SetWindowTitle({(const u8*)title, N - 1});
}

inline void SetWindowTitle(const char* title) {
    SetWindowTitle(String::fromCstr(title));
}

void GetWindowSize(int* width, int* height);
void SetWindowSize(int width, int height);
void SetWindowResizable(bool resizable);
void PresentWindow(void);
void ShowWindow(void);

}
