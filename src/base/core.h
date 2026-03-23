#pragma once

#include <cstdlib>

#include "base/types.h"
#include "base/log.h"

#if defined(__clang__)
#define COMPILER_CLANG 1
#else
#define COMPILER_CLANG 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define COMPILER_GCC 1
#else
#define COMPILER_GCC 0
#endif

#if defined(_MSC_VER)
#define COMPILER_MSVC 1
#include <intrin.h>
#else
#define COMPILER_MSVC 0
#endif

#if defined(_WIN32) || defined(_WIN64)
#define OS_WINDOWS 1
#else
#define OS_WINDOWS 0
#endif

#if defined(__APPLE__) && defined(__MACH__)
#define OS_MAC 1
#else
#define OS_MAC 0
#endif

#if defined(__linux__)
#define OS_LINUX 1
#else
#define OS_LINUX 0
#endif

#if OS_WINDOWS
#define export extern "C" __declspec(dllexport)
#else
#define export extern "C"
#endif

#define BIT(x) (1ULL << (x))
#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define KB 1024ULL
#define MB (KB * KB)
#define GB (MB * KB)
#define TB (GB * KB)
#define clamp(value, min, max)                                                 \
    (((value) < (min)) ? (min) : (((value) > (max)) ? (max) : (value)))

#if COMPILER_MSVC
#define assume(expr) __assume(expr)
#elif COMPILER_CLANG || COMPILER_GCC
#define assume(expr)                                                           \
    do {                                                                       \
        if(!(expr)) {                                                          \
            __builtin_unreachable();                                           \
        }                                                                      \
    } while(0)
#else
#define assume(expr) ((void)0)
#endif

inline bool is_pow2(u64 value) {
    return value != 0 && (value & (value - 1)) == 0;
}

inline bool add_u64_overflow(u64 a, u64 b, u64* out) {
#if COMPILER_CLANG || COMPILER_GCC
    return __builtin_add_overflow(a, b, out);
#else
    u64 max_u64 = ~(u64)0;
    if(a > max_u64 - b) {
        *out = 0;
        return true;
    }
    *out = a + b;
    return false;
#endif
}

inline bool mul_u64_overflow(u64 a, u64 b, u64* out) {
#if COMPILER_CLANG || COMPILER_GCC
    return __builtin_mul_overflow(a, b, out);
#else
    u64 max_u64 = ~(u64)0;
    if(a != 0 && b > max_u64 / a) {
        *out = 0;
        return true;
    }
    *out = a * b;
    return false;
#endif
}

inline bool align_up_pow2_u64(u64 value, u64 alignment, u64* out) {
    if(!is_pow2(alignment)) {
        *out = 0;
        return true;
    }

    u64 sum = 0;
    if(add_u64_overflow(value, alignment - 1, &sum)) {
        *out = 0;
        return true;
    }

    *out = sum & ~(alignment - 1);
    return false;
}

#ifndef NDEBUG
#if COMPILER_MSVC
#define assert(expr, msg)                                                      \
    do {                                                                       \
        if(!(expr)) {                                                          \
            LOG_FATAL("assertion failed: %s - %s", #expr, msg);                \
            __debugbreak();                                                    \
        }                                                                      \
    } while(0)
#elif COMPILER_CLANG
#define assert(expr, msg)                                                      \
    do {                                                                       \
        if(!(expr)) {                                                          \
            LOG_FATAL("assertion failed: %s - %s", #expr, msg);                \
            __builtin_debugtrap();                                             \
        }                                                                      \
    } while(0)
#elif COMPILER_GCC
#define assert(expr, msg)                                                      \
    do {                                                                       \
        if(!(expr)) {                                                          \
            LOG_FATAL("assertion failed: %s - %s", #expr, msg);                \
            __builtin_trap();                                                  \
        }                                                                      \
    } while(0)
#else
#define assert(expr, msg)                                                      \
    do {                                                                       \
        if(!(expr)) {                                                          \
            LOG_FATAL("assertion failed: %s - %s", #expr, msg);                \
            abort();                                                           \
        }                                                                      \
    } while(0)
#endif
#else
#define assert(expr, msg) ((void)sizeof((expr) ? true : false))
#endif

// Linked-list helpers (raddebugger style)
#define CheckNil(nil, p) ((p) == 0 || (p) == (nil))
#define SetNil(nil, p) ((p) = (nil))

// Doubly-linked list
#define DLLInsert_NPZ(nil, f, l, p, n, next, prev)                             \
    (CheckNil(nil, f)                                                          \
         ? ((f) = (l) = (n), SetNil(nil, (n)->next), SetNil(nil, (n)->prev))   \
     : CheckNil(nil, p) ? ((n)->next = (f),                                    \
                           (f)->prev = (n),                                    \
                           (f) = (n),                                          \
                           SetNil(nil, (n)->prev))                             \
     : ((p) == (l))     ? ((l)->next = (n),                                    \
                           (n)->prev = (l),                                    \
                           (l) = (n),                                          \
                           SetNil(nil, (n)->next))                             \
                        : (((!CheckNil(nil, p) && CheckNil(nil, (p)->next))    \
                                ? (0)                                          \
                                : ((p)->next->prev = (n))),                    \
                           ((n)->next = (p)->next),                            \
                           ((p)->next = (n)),                                  \
                           ((n)->prev = (p))))

#define DLLPushBack_NPZ(nil, f, l, n, next, prev)                              \
    DLLInsert_NPZ(nil, f, l, l, n, next, prev)
#define DLLPushFront_NPZ(nil, f, l, n, next, prev)                             \
    DLLInsert_NPZ(nil, l, f, f, n, prev, next)
#define DLLRemove_NPZ(nil, f, l, n, next, prev)                                \
    (((n) == (f) ? (f) = (n)->next : (0)),                                     \
     ((n) == (l) ? (l) = (l)->prev : (0)),                                     \
     (CheckNil(nil, (n)->prev) ? (0) : ((n)->prev->next = (n)->next)),         \
     (CheckNil(nil, (n)->next) ? (0) : ((n)->next->prev = (n)->prev)))

#define DLLPushBack(f, l, n) DLLPushBack_NPZ(0, f, l, n, next, prev)
#define DLLPushFront(f, l, n) DLLPushFront_NPZ(0, f, l, n, next, prev)
#define DLLRemove(f, l, n) DLLRemove_NPZ(0, f, l, n, next, prev)

// Singly-linked queue (doubly-headed)
#define SLLQueuePush_NZ(nil, f, l, n, next)                                    \
    (CheckNil(nil, f) ? ((f) = (l) = (n), SetNil(nil, (n)->next))              \
                      : ((l)->next = (n), (l) = (n), SetNil(nil, (n)->next)))
#define SLLQueuePop_NZ(nil, f, l, next)                                        \
    ((f) == (l) ? (SetNil(nil, f), SetNil(nil, l)) : ((f) = (f)->next))
#define SLLQueuePush_N(f, l, n, next) SLLQueuePush_NZ(0, f, l, n, next)
#define SLLQueuePop_N(f, l, next) SLLQueuePop_NZ(0, f, l, next)
#define SLLQueuePush(f, l, n) SLLQueuePush_NZ(0, f, l, n, next)
#define SLLQueuePop(f, l) SLLQueuePop_NZ(0, f, l, next)

// Singly-linked stack (singly-headed)
#define SLLStackPush_N(f, n, next) ((n)->next = (f), (f) = (n))
#define SLLStackPop_N(f, next) ((f) = (f)->next)
#define SLLStackPush(f, n) SLLStackPush_N(f, n, next)
#define SLLStackPop(f) SLLStackPop_N(f, next)
