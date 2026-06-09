#pragma once

// Essential fixed-width types for bare-metal development
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

// Pointer-sized integer types (assuming 64-bit target)
typedef unsigned long long uintptr_t;
typedef signed long long   intptr_t;
typedef unsigned long long size_t;
typedef signed long long   ssize_t;

#ifndef NULL
#define NULL ((void*)0)
#endif
