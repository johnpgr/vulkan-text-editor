#pragma once

namespace Platform {

struct DynamicLibraryFunction {
    String name;
    void* function;
};

struct DynamicLibrary {
    Arena* arena;
    String name;
    String filename;
    u64 internal_data_size;
    void* internal_data;
    u32 watch_id;

    ArrayList<DynamicLibraryFunction> functions;
};

bool LoadDynamicLibrary(
    Arena* arena,
    String name,
    DynamicLibrary* out_library
);

template <u64 N>
inline bool LoadDynamicLibrary(
    Arena* arena,
    const char (&name)[N],
    DynamicLibrary* out_library
) {
    return LoadDynamicLibrary(arena, {(const u8*)name, N - 1}, out_library);
}

inline bool LoadDynamicLibrary(
    Arena* arena,
    const char* name,
    DynamicLibrary* out_library
) {
    return LoadDynamicLibrary(arena, String::fromCstr(name), out_library);
}

bool UnloadDynamicLibrary(DynamicLibrary* library);
void* LoadDynamicFunction(String name, DynamicLibrary* library);

template <u64 N>
inline void* LoadDynamicFunction(
    const char (&name)[N],
    DynamicLibrary* library
) {
    return LoadDynamicFunction({(const u8*)name, N - 1}, library);
}

inline void* LoadDynamicFunction(const char* name, DynamicLibrary* library) {
    return LoadDynamicFunction(String::fromCstr(name), library);
}

String GetDynamicLibraryExtension(void);
String GetDynamicLibraryPrefix(void);

}
