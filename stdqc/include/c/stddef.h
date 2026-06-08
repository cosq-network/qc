#ifndef STDQC_STDDEF_H
#define STDQC_STDDEF_H

#ifdef __SIZE_TYPE__
typedef __SIZE_TYPE__ size_t;
#else
typedef unsigned long long size_t;
#endif

#ifdef __PTRDIFF_TYPE__
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#else
typedef long long ptrdiff_t;
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

#define offsetof(type, member) ((size_t)&(((type*)0)->member))

#endif // STDQC_STDDEF_H
