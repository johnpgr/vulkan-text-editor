#pragma once

namespace platform {

ErrorCode copy_file(
    String source,
    String dest,
    bool overwrite_if_exists
);

template <u64 SourceN, u64 DestN>
inline ErrorCode copy_file(
    const char (&source)[SourceN],
    const char (&dest)[DestN],
    bool overwrite_if_exists
) {
    return copy_file(
        {(const u8*)source, SourceN - 1},
        {(const u8*)dest, DestN - 1},
        overwrite_if_exists
    );
}

template <u64 SourceN>
inline ErrorCode copy_file(
    const char (&source)[SourceN],
    const char* dest,
    bool overwrite_if_exists
) {
    return copy_file(
        {(const u8*)source, SourceN - 1},
        String::from_cstr(dest),
        overwrite_if_exists
    );
}

template <u64 DestN>
inline ErrorCode copy_file(
    const char* source,
    const char (&dest)[DestN],
    bool overwrite_if_exists
) {
    return copy_file(
        String::from_cstr(source),
        {(const u8*)dest, DestN - 1},
        overwrite_if_exists
    );
}

inline ErrorCode copy_file(
    const char* source,
    const char* dest,
    bool overwrite_if_exists
) {
    return copy_file(
        String::from_cstr(source),
        String::from_cstr(dest),
        overwrite_if_exists
    );
}

}
