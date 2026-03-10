#if OS_LINUX

#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

static void os_memory_fail(const char* operation) {
    int error = errno;
    log_fatal("%s failed: %s", operation, strerror(error));
    abort();
}

void* platform_memory_reserve(u64 size) {
    void* ptr = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        os_memory_fail("mmap reserve");
    }
    return ptr;
}

u64 platform_memory_page_size(void) {
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        log_fatal("sysconf(_SC_PAGESIZE) failed");
        abort();
    }
    return (u64)page_size;
}

void platform_memory_commit(void* ptr, u64 size) {
    if (size == 0) {
        return;
    }

    if (mprotect(ptr, size, PROT_READ | PROT_WRITE) != 0) {
        os_memory_fail("mprotect commit");
    }
}

void platform_memory_decommit(void* ptr, u64 size) {
    if (size == 0) {
        return;
    }

    if (mprotect(ptr, size, PROT_NONE) != 0) {
        os_memory_fail("mprotect decommit");
    }
    if (madvise(ptr, size, MADV_DONTNEED) != 0) {
        os_memory_fail("madvise decommit");
    }
}

void platform_memory_release(void* ptr, u64 size) {
    if (ptr == nullptr || size == 0) {
        return;
    }
    if (munmap(ptr, size) != 0) {
        os_memory_fail("munmap release");
    }
}

#endif
