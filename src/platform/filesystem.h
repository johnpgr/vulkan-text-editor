#pragma once

namespace Platform {

ErrorCode CopyFile(
    String source,
    String dest,
    bool overwrite_if_exists
);

template <u64 SourceN, u64 DestN>
inline ErrorCode CopyFile(
    const char (&source)[SourceN],
    const char (&dest)[DestN],
    bool overwrite_if_exists
) {
    return CopyFile(
        {(const u8*)source, SourceN - 1},
        {(const u8*)dest, DestN - 1},
        overwrite_if_exists
    );
}

template <u64 SourceN>
inline ErrorCode CopyFile(
    const char (&source)[SourceN],
    const char* dest,
    bool overwrite_if_exists
) {
    return CopyFile(
        {(const u8*)source, SourceN - 1},
        String::fromCstr(dest),
        overwrite_if_exists
    );
}

template <u64 DestN>
inline ErrorCode CopyFile(
    const char* source,
    const char (&dest)[DestN],
    bool overwrite_if_exists
) {
    return CopyFile(
        String::fromCstr(source),
        {(const u8*)dest, DestN - 1},
        overwrite_if_exists
    );
}

inline ErrorCode CopyFile(
    const char* source,
    const char* dest,
    bool overwrite_if_exists
) {
    return CopyFile(
        String::fromCstr(source),
        String::fromCstr(dest),
        overwrite_if_exists
    );
}

}
