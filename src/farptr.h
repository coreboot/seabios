// Code to access multiple segments within gcc.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.
#ifndef __FARPTR_H
#define __FARPTR_H

#include "ioport.h" // insb

#define READ8_SEG(SEG, var) ({                                          \
    u8 __value;                                                         \
    __asm__ __volatile__("movb %%" #SEG ":%1, %b0"                      \
                         : "=Qi"(__value) : "m"(var));                  \
    __value; })
#define READ16_SEG(SEG, var) ({                                         \
    u16 __value;                                                        \
    __asm__ __volatile__("movw %%" #SEG ":%1, %w0"                      \
                         : "=ri"(__value) : "m"(var));                  \
    __value; })
#define READ32_SEG(SEG, var) ({                                         \
    u32 __value;                                                        \
    __asm__ __volatile__("movl %%" #SEG ":%1, %0"                       \
                         : "=ri"(__value) : "m"(var));                  \
    __value; })
#define WRITE8_SEG(SEG, var, value)                     \
    __asm__ __volatile__("movb %b0, %%" #SEG ":%1"      \
                         : : "Q"(value), "m"(var))
#define WRITE16_SEG(SEG, var, value)                    \
    __asm__ __volatile__("movw %w0, %%" #SEG ":%1"      \
                         : : "r"(value), "m"(var))
#define WRITE32_SEG(SEG, var, value)                    \
    __asm__ __volatile__("movl %0, %%" #SEG ":%1"       \
                         : : "r"(value), "m"(var))

extern void __force_link_error__unknown_type();

#define __GET_VAR(seg, var) ({                                  \
    typeof(var) __val;                                          \
    if (__builtin_types_compatible_p(typeof(__val), u8))        \
        __val = READ8_SEG(seg, var);                            \
    else if (__builtin_types_compatible_p(typeof(__val), u16))  \
        __val = READ16_SEG(seg, var);                           \
    else if (__builtin_types_compatible_p(typeof(__val), u32))  \
        __val = READ32_SEG(seg, var);                           \
    else                                                        \
        __force_link_error__unknown_type();                     \
    __val; })

#define __SET_VAR(seg, var, val) do {                             \
        if (__builtin_types_compatible_p(typeof(var), u8))        \
            WRITE8_SEG(seg, var, (val));                          \
        else if (__builtin_types_compatible_p(typeof(var), u16))  \
            WRITE16_SEG(seg, var, (val));                         \
        else if (__builtin_types_compatible_p(typeof(var), u32))  \
            WRITE32_SEG(seg, var, (val));                         \
        else                                                      \
            __force_link_error__unknown_type();                   \
    } while (0)

#define __SET_SEG(SEG, value)                                   \
    __asm__ __volatile__("movw %w0, %%" #SEG : : "r"(value))
#define __GET_SEG(SEG) ({                                       \
    u16 __seg;                                                  \
    __asm__ __volatile__("movw %%" #SEG ", %w0" : "=r"(__seg)); \
    __seg;})

#define GET_FARVAR(seg, var) ({                 \
    SET_SEG(ES, (seg));                         \
    GET_VAR(ES, (var)); })
#define SET_FARVAR(seg, var, val) do {          \
        typeof(var) __sfv_val = (val);          \
        SET_SEG(ES, (seg));                     \
        SET_VAR(ES, (var), __sfv_val);          \
    } while (0)

#define PTR_TO_SEG(p) ((((u32)(p)) >> 4) & 0xf000)
#define PTR_TO_OFFSET(p) (((u32)(p)) & 0xffff)

#define __GET_FARPTR(ptr) ({                                            \
    typeof (&(ptr)) __ptr;                                              \
    GET_FARVAR(PTR_TO_SEG(__ptr), *(typeof __ptr)PTR_TO_OFFSET(__ptr)); })
#define __SET_FARVAR(ptr, val) do {                                     \
        typeof (&(ptr)) __ptr;                                          \
        SET_FARVAR(PTR_TO_SEG(__ptr), *(typeof __ptr)PTR_TO_OFFSET(__ptr) \
                   , (val));                                            \
    } while (0)

#ifdef MODE16
#define GET_VAR(seg, var) __GET_VAR(seg, (var))
#define SET_VAR(seg, var, val) __SET_VAR(seg, (var), (val))
#define SET_SEG(SEG, value) __SET_SEG(SEG, (value))
#define GET_SEG(SEG) __GET_SEG(SEG)
#define GET_FARPTR(ptr) __GET_FARPTR(ptr)
#define SET_FARPTR(ptr, val) __SET_FARPTR((ptr), (val))
#else
// In 32-bit mode there is no need to mess with the segments.
#define GET_VAR(seg, var) (var)
#define SET_VAR(seg, var, val) do { (var) = (val); } while (0)
#define SET_SEG(SEG, value) ((void)(value))
#define GET_SEG(SEG) 0
#define GET_FARPTR(ptr) (ptr)
#define SET_FARPTR(ptr, val) do { (var) = (val); } while (0)
#endif

static inline void insb_seg(u16 port, u16 segment, u16 offset, u16 count) {
    SET_SEG(ES, segment);
    insb(port, (u8*)(offset+0), count);
}
static inline void insw_seg(u16 port, u16 segment, u16 offset, u16 count) {
    SET_SEG(ES, segment);
    insw(port, (u16*)(offset+0), count);
}
static inline void insl_seg(u16 port, u16 segment, u16 offset, u16 count) {
    SET_SEG(ES, segment);
    insl(port, (u32*)(offset+0), count);
}
static inline void outsb_seg(u16 port, u16 segment, u16 offset, u16 count) {
    SET_SEG(ES, segment);
    outsb(port, (u8*)(offset+0), count);
}
static inline void outsw_seg(u16 port, u16 segment, u16 offset, u16 count) {
    SET_SEG(ES, segment);
    outsw(port, (u16*)(offset+0), count);
}
static inline void outsl_seg(u16 port, u16 segment, u16 offset, u16 count) {
    SET_SEG(ES, segment);
    outsl(port, (u32*)(offset+0), count);
}

#endif // farptr.h
