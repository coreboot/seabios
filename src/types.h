// Basic type definitions for X86 cpus.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.
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

union u64_u32_u {
    struct { u32 hi, lo; };
    u64 val;
};

#define __VISIBLE __attribute__((externally_visible))

#define UNIQSEC __FILE__ "." __stringify(__LINE__)

#define __ASM(code) asm(".section .text.asm." UNIQSEC "\n\t" code)

#if MODE16 == 1
// Notes a function as externally visible in the 16bit code chunk.
# define VISIBLE16 __VISIBLE
// Notes a function as externally visible in the 32bit code chunk.
# define VISIBLE32
// Designate a variable as (only) visible to 16bit code.
# define VAR16 __section(".data16." UNIQSEC)
// Designate a variable as visible to both 32bit and 16bit code.
# define VAR16_32 VAR16 __VISIBLE
// Designate a variable visible externally.
# define VAR16EXPORT __section(".data16.export." UNIQSEC) __VISIBLE
// Designate a variable at a specific 16bit address
# define VAR16FIXED(addr) __aligned(1) __VISIBLE __section(".fixedaddr." __stringify(addr))
// Designate top-level assembler as 16bit only.
# define ASM16(code) __ASM(code)
# define ASM32(code)
#else
# define VISIBLE16
# define VISIBLE32 __VISIBLE
# define VAR16 __section(".discard.var16." UNIQSEC)
# define VAR16_32 VAR16 __VISIBLE __weak
# define VAR16EXPORT VAR16_32
# define VAR16FIXED(addr) VAR16_32
# define ASM16(code)
# define ASM32(code) __ASM(code)
#endif

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define FIELD_SIZEOF(t, f) (sizeof(((t*)0)->f))
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#define ALIGN(x,a)              __ALIGN_MASK(x,(typeof(x))(a)-1)
#define __ALIGN_MASK(x,mask)    (((x)+(mask))&~(mask))

#define NULL ((void *)0)

#define __weak __attribute__((weak))
#define __section(S) __attribute__((section(S)))

#define PACKED __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))

#define barrier() __asm__ __volatile__("": : :"memory")

#define noinline __attribute__((noinline))
#define __always_inline inline __attribute__((always_inline))

#define __stringify_1(x)        #x
#define __stringify(x)          __stringify_1(x)

#endif // types.h
