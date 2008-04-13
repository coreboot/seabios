// Basic type definitions for X86 cpus.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.
#ifndef __TYPES_H
#define __TYPES_H

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;
typedef unsigned long long u64;
typedef signed long long s64;
typedef u32 size_t;

#define __VISIBLE __attribute__((externally_visible))
#ifdef MODE16
// Notes a function as externally visible in the 16bit code chunk.
#define VISIBLE16 __VISIBLE
// Notes a function as externally visible in the 32bit code chunk.
#define VISIBLE32
#else
#define VISIBLE16
#define VISIBLE32 __VISIBLE
#endif

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define FIELD_SIZEOF(t, f) (sizeof(((t*)0)->f))

#define NULL ((void *)0)

#define PACKED __attribute__((packed))

#define barrier() __asm__ __volatile__("": : :"memory")

#define __always_inline inline __attribute__((always_inline))

#define __stringify_1(x)        #x
#define __stringify(x)          __stringify_1(x)

#endif // types.h
