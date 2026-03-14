#pragma once

namespace platform {

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

bool load_dynamic_library(
    Arena* arena,
    String name,
    DynamicLibrary* out_library
);

template <u64 N>
inline bool load_dynamic_library(
    Arena* arena,
    const char (&name)[N],
    DynamicLibrary* out_library
) {
    return load_dynamic_library(arena, {(const u8*)name, N - 1}, out_library);
}

inline bool load_dynamic_library(
    Arena* arena,
    const char* name,
    DynamicLibrary* out_library
) {
    return load_dynamic_library(arena, String::from_cstr(name), out_library);
}

bool unload_dynamic_library(DynamicLibrary* library);
void* load_dynamic_function(String name, DynamicLibrary* library);

template <u64 N>
inline void* load_dynamic_function(
    const char (&name)[N],
    DynamicLibrary* library
) {
    return load_dynamic_function({(const u8*)name, N - 1}, library);
}

inline void* load_dynamic_function(const char* name, DynamicLibrary* library) {
    return load_dynamic_function(String::from_cstr(name), library);
}

String get_dynamic_library_extension(void);
String get_dynamic_library_prefix(void);

}
