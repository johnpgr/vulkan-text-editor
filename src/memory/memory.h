#pragma once

void* platform_memory_reserve(u64 size);
u64 platform_memory_page_size(void);
void platform_memory_commit(void* ptr, u64 size);
void platform_memory_decommit(void* ptr, u64 size);
void platform_memory_release(void* ptr, u64 size);
