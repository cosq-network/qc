#include <stdio.h>
#include <stddef.h>

#ifdef STDQC_HOST_TEST
#include <unistd.h>
#else
#ifdef __APPLE__
    #define SYSCALL_WRITE 0x2000004
#else
    #define SYSCALL_WRITE 1
#endif
extern "C" long long __qc_syscall(long long nr, long long arg1, long long arg2, long long arg3);
#endif

extern "C" int putchar(int c) {
#ifdef STDQC_HOST_TEST
    char buf = (char)c;
    (void)::write(1, &buf, 1);
    return c;
#else
    char buf = (char)c;
    __qc_syscall(SYSCALL_WRITE, 1, (long long)&buf, 1);
    return c;
#endif
}

extern "C" int puts(const char* s) {
    if (!s) return -1;
    size_t len = 0;
    while (s[len]) {
        putchar(s[len]);
        len++;
    }
    putchar('\n');
    return (int)len;
}
