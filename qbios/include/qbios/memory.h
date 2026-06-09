#pragma once
#include "qbios/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Essential memory operations
void* memcpy(void* dst, const void* src, size_t n);
void* memset(void* dst, int c, size_t n);
void* memmove(void* dst, const void* src, size_t n);
int   memcmp(const void* s1, const void* s2, size_t n);

#ifdef __cplusplus
}
#endif
