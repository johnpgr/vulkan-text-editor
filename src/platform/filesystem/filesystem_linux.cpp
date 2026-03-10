#include "platform/linux/internal.h"

PlatformErrorCode pfsCopyFile(String source, String dest, bool overwrite_if_exists) {
    Arena* scratch = pGetScratchLinux();
    u64 scratch_mark = scratch->mark();
    defer {
        scratch->restore(scratch_mark);
    };

    const char* source_path = source.toCStr(scratch);
    const char* dest_path = dest.toCStr(scratch);

    int source_fd = -1;
    defer {
        if (source_fd >= 0) {
            close(source_fd);
        }
    };

    int dest_fd = -1;
    defer {
        if (dest_fd >= 0) {
            close(dest_fd);
        }
    };

    source_fd = open(source_path, O_RDONLY);
    if (source_fd < 0) {
        return pfsGetCopyErrorLinux(errno);
    }

    struct stat source_stat = {};
    if (fstat(source_fd, &source_stat) != 0) {
        return pfsGetCopyErrorLinux(errno);
    }

    int dest_flags = O_WRONLY | O_CREAT;
    dest_flags |= overwrite_if_exists ? O_TRUNC : O_EXCL;

    dest_fd = open(dest_path, dest_flags, source_stat.st_mode);
    if (dest_fd < 0) {
        return pfsGetCopyErrorLinux(errno);
    }

    PlatformErrorCode result = PlatformErrorCode::PLATFORM_ERROR_SUCCESS;
    char buffer[64 * KB];
    for (;;) {
        ssize_t bytes_read = read(source_fd, buffer, sizeof(buffer));
        if (bytes_read == 0) {
            break;
        }
        if (bytes_read < 0) {
            result = pfsGetCopyErrorLinux(errno);
            break;
        }

        ssize_t total_written = 0;
        while (total_written < bytes_read) {
            ssize_t bytes_written = write(dest_fd, buffer + total_written, (size_t)(bytes_read - total_written));
            if (bytes_written < 0) {
                result = pfsGetCopyErrorLinux(errno);
                break;
            }
            total_written += bytes_written;
        }

        if (result != PlatformErrorCode::PLATFORM_ERROR_SUCCESS) {
            break;
        }
    }
    return result;
}
