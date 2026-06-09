#include <string.h>
#include <stddef.h>

extern "C" void* memcpy(void* dest, const void* src, size_t n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dest;
}

extern "C" void* memset(void* s, int c, size_t n) {
    char* p = (char*)s;
    for (size_t i = 0; i < n; ++i) {
        p[i] = (char)c;
    }
    return s;
}

extern "C" void* memmove(void* dest, const void* src, size_t n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    if (d < s) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; --i) d[i-1] = s[i-1];
    }
    return dest;
}

extern "C" int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;
    for (size_t i = 0; i < n; ++i) {
        if (p1[i] < p2[i]) return -1;
        if (p1[i] > p2[i]) return 1;
    }
    return 0;
}

extern "C" size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

extern "C" char* strcpy(char* dest, const char* src) {
    size_t i = 0;
    while (src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = 0;
    return dest;
}

extern "C" int strcmp(const char* s1, const char* s2) {
    size_t i = 0;
    while (s1[i] && s1[i] == s2[i]) {
        i++;
    }
    if ((unsigned char)s1[i] < (unsigned char)s2[i]) return -1;
    if ((unsigned char)s1[i] > (unsigned char)s2[i]) return 1;
    return 0;
}

extern "C" char* strcat(char* dest, const char* src) {
    size_t dlen = strlen(dest);
    strcpy(dest + dlen, src);
    return dest;
}
