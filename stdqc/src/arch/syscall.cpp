extern "C" long long __qc_syscall(long long nr, long long arg1, long long arg2, long long arg3) {
    long long res;
#if defined(__aarch64__)
    register long long x8 __asm__("x8") = nr;
    register long long x0 __asm__("x0") = arg1;
    register long long x1 __asm__("x1") = arg2;
    register long long x2 __asm__("x2") = arg3;
    __asm__ volatile (
        "svc #0"
        : "=r"(x0)
        : "r"(x8), "r"(x0), "r"(x1), "r"(x2)
        : "memory"
    );
    res = x0;
#elif defined(__x86_64__)
    __asm__ volatile (
        "syscall"
        : "=a"(res)
        : "a"(nr), "D"(arg1), "S"(arg2), "d"(arg3)
        : "rcx", "r11", "memory"
    );
#else
    res = -1;
#endif
    return res;
}
