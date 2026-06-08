#ifndef STDQC_STDIO_H
#define STDQC_STDIO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int putchar(int c);
int puts(const char* s);
int printf(const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif // STDQC_STDIO_H
