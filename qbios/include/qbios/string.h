#pragma once
#include "qbios/types.h"

#ifdef __cplusplus
extern "C" {
#endif

size_t strlen(const char* s);
char*  strcpy(char* dst, const char* src);
int    strcmp(const char* s1, const char* s2);

#ifdef __cplusplus
}
#endif
