#include <stdlib.h>
#include <stddef.h>

#ifdef STDQC_HOST_TEST
#include <unistd.h>
#else
#ifdef __APPLE__
    #define SYSCALL_EXIT 0x2000001
#else
    #define SYSCALL_EXIT 60
#endif
extern "C" long long __qc_syscall(long long nr, long long arg1, long long arg2, long long arg3);
#endif

// Extremely simple bump allocator for demonstration
static char s_heap[64 * 1024];
static size_t s_heap_ptr = 0;

extern "C" void* malloc(size_t size) {
    if (s_heap_ptr + size > sizeof(s_heap)) return nullptr;
    void* ptr = &s_heap[s_heap_ptr];
    s_heap_ptr += (size + 7) & ~7; // 8-byte align
    return ptr;
}

extern "C" void free(void* ptr) {
    // No-op for bump allocator
    (void)ptr;
}

extern "C" void exit(int status) {
#ifdef STDQC_HOST_TEST
    ::_exit(status);
#else
    __qc_syscall(SYSCALL_EXIT, status, 0, 0);
    for (;;); // Should not reach here
#endif
}

extern "C" void abort(void) {
    exit(127);
}
