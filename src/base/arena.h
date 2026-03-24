#pragma once

#include <cstring>

#include "base/core.h"
#include "base/list.h"
#include "base/memory.h"

// Arena header lives in the first 128 bytes of each reserved block.
// The rest of the block is usable memory.
#define ARENA_HEADER_SIZE 128
#define ARENA_DEFAULT_RESERVE (64 * MB)
#define ARENA_DEFAULT_COMMIT (64 * KB)

struct Arena {
    Arena* prev;    // previous block in chain (null on head block)
    Arena* current; // points to the newest (current) block (only valid on head)
    u64 res_size;   // reserve size for new blocks
    u64 cmt_size;   // commit granule for new blocks
    u64 base_pos;   // byte offset of this block's start in the global chain
    u64 pos;        // current write position within this block
    u64 cmt;        // committed bytes in this block
    u64 res;        // reserved bytes in this block
};
static_assert(
    sizeof(Arena) <= ARENA_HEADER_SIZE,
    "Arena header exceeds ARENA_HEADER_SIZE"
);

struct Temp {
    Arena* arena;
    u64 pos;
};

// --- helpers ---

internal u64 align_up(u64 value, u64 alignment) {
    ASSERT(alignment != 0, "alignment must be non-zero");
    ASSERT(is_pow2(alignment), "alignment must be pow2");
    u64 result = 0;
    bool overflow = align_up_pow2_u64(value, alignment, &result);
    ASSERT(!overflow, "alignment overflow");
    return result;
}

// --- core API ---

internal Arena* arena_alloc(
    u64 res_size = ARENA_DEFAULT_RESERVE,
    u64 cmt_size = ARENA_DEFAULT_COMMIT
) {
    u64 page = get_system_page_size();
    res_size = align_up(res_size, page);
    cmt_size = align_up(cmt_size, page);
    if(cmt_size > res_size)
        cmt_size = res_size;

    void* mem = reserve_system_memory(res_size);
    commit_system_memory(mem, cmt_size);

    Arena* arena = (Arena*)mem;
    arena->prev = nullptr;
    arena->current = arena;
    arena->res_size = res_size;
    arena->cmt_size = cmt_size;
    arena->base_pos = 0;
    arena->pos = ARENA_HEADER_SIZE;
    arena->cmt = cmt_size;
    arena->res = res_size;
    return arena;
}

internal void arena_release(Arena* arena) {
    ASSERT(arena != nullptr, "arena must not be null");
    for(Arena *block = arena->current, *prev = nullptr; block != nullptr;
        block = prev) {
        prev = block->prev;
        release_system_memory(block, block->res);
    }
}

internal void* arena_push(Arena* arena, u64 size, u64 align, bool zero) {
    ASSERT(arena != nullptr, "arena must not be null");
    Arena* current = arena->current;

    u64 pos_pre = align_up(current->pos, align);
    u64 pos_post = pos_pre + size;

    // chain a new block if current is full
    if(pos_post > current->res) {
        u64 new_res = current->res_size;
        u64 new_cmt = current->cmt_size;
        // if the single allocation is larger than the default block, resize
        if(size + ARENA_HEADER_SIZE > new_res) {
            u64 page = get_system_page_size();
            new_res = align_up(size + ARENA_HEADER_SIZE, page);
            new_cmt = new_res;
        }
        Arena* block = arena_alloc(new_res, new_cmt);
        block->base_pos = current->base_pos + current->res;
        block->res_size = current->res_size;
        block->cmt_size = current->cmt_size;
        SLL_STACK_PUSH_N(arena->current, block, prev);
        current = block;
        pos_pre = align_up(current->pos, align);
        pos_post = pos_pre + size;
    }

    // commit more pages if needed
    if(pos_post > current->cmt) {
        u64 new_cmt = align_up(pos_post, current->cmt_size);
        if(new_cmt > current->res)
            new_cmt = current->res;
        commit_system_memory(
            (u8*)current + current->cmt,
            new_cmt - current->cmt
        );
        current->cmt = new_cmt;
    }

    void* result = (u8*)current + pos_pre;
    current->pos = pos_post;
    if(zero)
        memset(result, 0, size);
    return result;
}

internal u64 arena_pos(Arena* arena) {
    ASSERT(arena != nullptr, "arena must not be null");
    return arena->current->base_pos + arena->current->pos;
}

internal void arena_pop_to(Arena* arena, u64 pos) {
    ASSERT(arena != nullptr, "arena must not be null");
    u64 big_pos = pos < ARENA_HEADER_SIZE ? (u64)ARENA_HEADER_SIZE : pos;
    Arena* current = arena->current;
    while(current->base_pos >= big_pos) {
        Arena* prev = current->prev;
        release_system_memory(current, current->res);
        current = prev;
    }
    arena->current = current;
    u64 new_pos = big_pos - current->base_pos;
    ASSERT(new_pos <= current->pos, "arena_pop_to: new_pos out of range");
    current->pos = new_pos;
}

internal void arena_clear(Arena* arena) {
    arena_pop_to(arena, 0);
}

internal Temp temp_begin(Arena* arena) {
    ASSERT(arena != nullptr, "arena must not be null");
    return {arena, arena_pos(arena)};
}

internal void temp_end(Temp t) {
    arena_pop_to(t.arena, t.pos);
}

// --- push helpers ---

#define push_struct(arena, T) (T*)arena_push((arena), sizeof(T), alignof(T), 1)
#define push_array(arena, T, count)                                            \
    (T*)arena_push((arena), sizeof(T) * (count), alignof(T), 1)
#define push_array_no_zero(arena, T, count)                                    \
    (T*)arena_push((arena), sizeof(T) * (count), alignof(T), 0)
#define push_size(arena, size) arena_push((arena), (size), 8, 1)
