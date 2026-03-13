#pragma once

#if OS_WINDOWS
#include <windows.h>
#else
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

internal inline void pmFail(const char* operation) {
#if OS_WINDOWS
    DWORD error = GetLastError();
    LOG_FATAL("%s failed with error %lu", operation, (unsigned long)error);
#else
    int error = errno;
    LOG_FATAL("%s failed: %s", operation, strerror(error));
#endif
    abort();
}

inline void* pmReserveMemory(u64 size) {
#if OS_WINDOWS
    void* ptr = VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
    if (ptr == nullptr) {
        pmFail("VirtualAlloc reserve");
    }
    return ptr;
#else
    int map_anon_flag =
#if OS_MAC
        MAP_ANON;
#else
        MAP_ANONYMOUS;
#endif
    void* ptr =
        mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | map_anon_flag, -1, 0);
    if (ptr == MAP_FAILED) {
        pmFail("mmap reserve");
    }
    return ptr;
#endif
}

inline u64 pmGetPageSize(void) {
#if OS_WINDOWS
    SYSTEM_INFO system_info = {};
    GetSystemInfo(&system_info);
    return (u64)system_info.dwPageSize;
#else
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        LOG_FATAL("sysconf(_SC_PAGESIZE) failed");
        abort();
    }
    return (u64)page_size;
#endif
}

inline void pmCommitMemory(void* ptr, u64 size) {
    if (size == 0) {
        return;
    }

#if OS_WINDOWS
    void* result = VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE);
    if (result == nullptr) {
        pmFail("VirtualAlloc commit");
    }
#else
    if (mprotect(ptr, size, PROT_READ | PROT_WRITE) != 0) {
        pmFail("mprotect commit");
    }
#endif
}

inline void pmDecommitMemory(void* ptr, u64 size) {
    if (size == 0) {
        return;
    }

#if OS_WINDOWS
    if (VirtualFree(ptr, size, MEM_DECOMMIT) == 0) {
        pmFail("VirtualFree decommit");
    }
#else
    if (mprotect(ptr, size, PROT_NONE) != 0) {
        pmFail("mprotect decommit");
    }
    if (madvise(ptr, size, MADV_DONTNEED) != 0) {
        pmFail("madvise decommit");
    }
#endif
}

inline void pmReleaseMemory(void* ptr, u64 size) {
#if OS_WINDOWS
    (void)size;
    if (ptr == nullptr) {
        return;
    }

    if (VirtualFree(ptr, 0, MEM_RELEASE) == 0) {
        pmFail("VirtualFree release");
    }
#else
    if (ptr == nullptr || size == 0) {
        return;
    }

    if (munmap(ptr, size) != 0) {
        pmFail("munmap release");
    }
#endif
}
