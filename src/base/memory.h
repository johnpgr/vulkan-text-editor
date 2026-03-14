#pragma once

#if OS_WINDOWS
#include <windows.h>
#else
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "platform/error.h"

namespace platform {

inline void* reserve_memory(u64 size) {
#if OS_WINDOWS
    void* ptr = VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
    if (ptr == nullptr) {
        fail("VirtualAlloc reserve");
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
        fail("mmap reserve");
    }
    return ptr;
#endif
}

inline u64 get_page_size(void) {
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

inline void commit_memory(void* ptr, u64 size) {
    if (size == 0) {
        return;
    }

#if OS_WINDOWS
    void* result = VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE);
    if (result == nullptr) {
        fail("VirtualAlloc commit");
    }
#else
    if (mprotect(ptr, size, PROT_READ | PROT_WRITE) != 0) {
        fail("mprotect commit");
    }
#endif
}

inline void decommit_memory(void* ptr, u64 size) {
    if (size == 0) {
        return;
    }

#if OS_WINDOWS
    if (VirtualFree(ptr, size, MEM_DECOMMIT) == 0) {
        fail("VirtualFree decommit");
    }
#else
    if (mprotect(ptr, size, PROT_NONE) != 0) {
        fail("mprotect decommit");
    }
    if (madvise(ptr, size, MADV_DONTNEED) != 0) {
        fail("madvise decommit");
    }
#endif
}

inline void release_memory(void* ptr, u64 size) {
#if OS_WINDOWS
    (void)size;
    if (ptr == nullptr) {
        return;
    }

    if (VirtualFree(ptr, 0, MEM_RELEASE) == 0) {
        fail("VirtualFree release");
    }
#else
    if (ptr == nullptr || size == 0) {
        return;
    }

    if (munmap(ptr, size) != 0) {
        fail("munmap release");
    }
#endif
}

}
