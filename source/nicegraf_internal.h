/**
 * Copyright (c) 2019 nicegraf contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy 
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#pragma once

#include "nicegraf.h"

#include <assert.h>
#if defined(_WIN32) || defined(_WIN64)
#define NGF_THREADLOCAL __declspec(thread)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// emulate pthread mutexes and condvars
typedef CRITICAL_SECTION   pthread_mutex_t;
typedef CONDITION_VARIABLE pthread_cond_t;
#define pthread_mutex_lock(m)    (EnterCriticalSection(m),0)
#define pthread_mutex_unlock(m)  (LeaveCriticalSection(m),0)
#define pthread_mutex_init(m, a) (InitializeCriticalSection(m),0)
#define pthread_mutex_destroy(m) (DeleteCriticalSection(m),0)
#define pthread_cond_init(c, a)  (InitializeConditionVariable(c))
#define pthread_cond_wait(c, m)  (SleepConditionVariableCS(c, m, INFINITE))
#define pthread_cond_signal(c)   (WakeConditionVariable(c))
#define _ngf_cur_thread_id()     (GetCurrentThreadId())
#define pthread_cond_destroy(c)
#else
#define NGF_THREADLOCAL __thread
#include <pthread.h>
#if defined(__APPLE__)
#define _ngf_cur_thread_id() pthread_mach_thread_np(pthread_self())
#else
#include <unistd.h>
#include <sys/syscall.h>
#define _ngf_cur_thread_id() syscall(SYS_gettid)
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Custom allocation callbacks.
extern const ngf_allocation_callbacks *NGF_ALLOC_CB;

// Convenience macros for invoking custom memory allocation callbacks.
#define NGF_ALLOC(type)     ((type*) NGF_ALLOC_CB->allocate(sizeof(type), 1))
#define NGF_ALLOCN(type, n) ((type*) NGF_ALLOC_CB->allocate(sizeof(type), n))
#define NGF_FREE(ptr)       (NGF_ALLOC_CB->free((void*)(ptr), sizeof(*ptr), 1))
#define NGF_FREEN(ptr, n)   (NGF_ALLOC_CB->free((void*)(ptr), sizeof(*ptr), n))

// Macro for determining size of arrays.
#if defined(_MSC_VER)
#include <stdlib.h>
#define NGF_ARRAYSIZE(arr) _countof(arr)
#else
#define NGF_ARRAYSIZE(arr) (sizeof(arr)/sizeof(arr[0]))
#endif

// For when you don't feel like comparing structs field-by-field.
#define NGF_STRUCT_EQ(s1, s2) (sizeof(s1) == sizeof(s2) && \
                               memcmp((void*)&s1, (void*)&s2, sizeof(s1)) == 0)

// It is $CURRENT_YEAR and C does not have a standard thing for this.
#define NGF_MAX(a, b) (a > b ? a : b)
#define NGF_MIN(a, b) (a < b ? a : b)

// A fast fixed-size block allocator.
typedef struct _ngf_block_allocator _ngf_block_allocator;

// Creates a new block allocator with a given fixed `block_size` and a given
// initial capacity of `nblocks`.
_ngf_block_allocator* _ngf_blkalloc_create(uint32_t block_size,
                                           uint32_t nblocks);

// Destroys the given block allocator. All unfreed pointers obtained from the
// destroyed allocator become invalid.
void _ngf_blkalloc_destroy(_ngf_block_allocator *alloc);

// Allocates the next free block from the allocator. Returns NULL on error.
void* _ngf_blkalloc_alloc(_ngf_block_allocator *alloc);

typedef enum {
  _NGF_BLK_NO_ERROR,
  _NGF_BLK_DOUBLE_FREE,
  _NGF_BLK_WRONG_ALLOCATOR
} _ngf_blkalloc_error;

// Returns the given block to the allocator.
// Freeing a NULL pointer does nothing.
_ngf_blkalloc_error _ngf_blkalloc_free(_ngf_block_allocator *alloc, void *ptr);

// For fixing unreferenced parameter warnings.
#if defined(__GNUC__) && !defined(__clang__)
static void _NGF_FAKE_USE_HELPER(int _, ...) { _ <<= 0u; }
#define _NGF_FAKE_USE(...) _NGF_FAKE_USE_HELPER(0u, __VA_ARGS__)
#else
#define _NGF_FAKE_USE(...) {sizeof(__VA_ARGS__);}
#endif

// MSVC warnings that are safe to ignore.
#pragma warning(disable:4201)
#pragma warning(disable:4200)
#pragma warning(disable:4204)
#pragma warning(disable:4221)

// Info about a native resource binding.
typedef struct {
  uint32_t  ngf_binding_id;    // Nicegraf binding id.
  uint32_t  native_binding_id; // Actual backend api-specific binding id.
  uint32_t  ncis_bindings;     // Number of associated combined image/sampler
                               // bindings.
  uint32_t *cis_bindings;      // Associated combined image/sampler bindings.
} _ngf_native_binding;

// Mapping from (set, binding) to (native binding).
typedef _ngf_native_binding** _ngf_native_binding_map;

// Generates a (set, binding) to (native binding) map from the given pipeline
// layout and combined image/sampler maps.
ngf_error _ngf_create_native_binding_map(
    const ngf_pipeline_layout_info *layout,
    const ngf_plmd_cis_map *images_to_cis,
    const ngf_plmd_cis_map *samplers_to_cis,
   _ngf_native_binding_map *result);

void _ngf_destroy_binding_map(_ngf_native_binding_map map);
  
const _ngf_native_binding* _ngf_binding_map_lookup(
    const _ngf_native_binding_map map,
    uint32_t set,
    uint32_t binding);

typedef enum {
  _NGF_CMD_BUFFER_READY,
  _NGF_CMD_BUFFER_RECORDING,
  _NGF_CMD_BUFFER_AWAITING_SUBMIT,
  _NGF_CMD_BUFFER_SUBMITTED
} _ngf_cmd_buffer_state;

#define _NGF_CMD_BUF_RECORDABLE(s) (s == _NGF_CMD_BUFFER_READY || \
                                    s == _NGF_CMD_BUFFER_AWAITING_SUBMIT)

// Interlocked ops.
#if defined(_WIN32)
#define ATOMIC_INT ULONG
#define interlocked_inc(v)      ((ULONG)InterlockedIncrement((LONG*)v))
#define interlocked_post_inc(v) ((ULONG)InterlockedExchangeAdd((LONG*)v, 1))
#define interlocked_read(v)     ((ULONG)InterlockedExchangeAdd((LONG*)v, 0))
#elif defined(_WIN64)
#define ATOMIC_INT ULONG64
#define interlocked_inc(v)      ((ULONG64)InterlockedIncrement64((LONG64*)x))
#define interlocked_post_inc(v) ((ULONG64)InterlockedExchangeAdd64((LONG*)v, \
                                                                          1))
#define interlocked_read(v)     ((ULONG64)InterlockedExchangeAdd64( \
                                              (LONG64*)v, 0))
#else
#if defined(__LP64__)
#define ATOMIC_INT uint64_t
#else
#define ATOMIC_INT uint32_t
#endif
#define interlocked_inc(v)      (__sync_add_and_fetch(v, 1))
#define interlocked_post_inc(v) (__sync_fetch_and_add(v, 1))
#define interlocked_read(v)     (__sync_add_and_fetch(v, 0))
#endif

#ifdef __cplusplus
}
#endif
