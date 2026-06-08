#ifndef STDQC_STDLIB_H
#define STDQC_STDLIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* malloc(size_t size);
void free(void* ptr);

void exit(int status);
void abort(void);

#ifdef __cplusplus
}
#endif

#endif // STDQC_STDLIB_H
