#ifndef STDQC_STDINT_H
#define STDQC_STDINT_H

#ifdef __INT8_TYPE__
typedef __INT8_TYPE__   int8_t;
typedef __INT16_TYPE__  int16_t;
typedef __INT32_TYPE__  int32_t;
typedef __INT64_TYPE__  int64_t;

typedef __UINT8_TYPE__  uint8_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __UINT64_TYPE__ uint64_t;

typedef __INTPTR_TYPE__  intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;
#else
typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long long          int64_t;

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef long long          intptr_t;
typedef unsigned long long uintptr_t;
#endif

typedef int64_t            intmax_t;
typedef uint64_t           uintmax_t;

#endif // STDQC_STDINT_H
