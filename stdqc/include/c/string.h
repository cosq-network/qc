#ifndef STDQC_STRING_H
#define STDQC_STRING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void*  memcpy(void* dest, const void* src, size_t n);
void*  memset(void* s, int c, size_t n);
void*  memmove(void* dest, const void* src, size_t n);
int    memcmp(const void* s1, const void* s2, size_t n);

size_t strlen(const char* s);
char*  strcpy(char* dest, const char* src);
int    strcmp(const char* s1, const char* s2);
char*  strcat(char* dest, const char* src);

#ifdef __cplusplus
}
#endif

#endif // STDQC_STRING_H
