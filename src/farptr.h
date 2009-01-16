// Code to access multiple segments within gcc.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.
#ifndef __FARPTR_H
#define __FARPTR_H

#include "ioport.h" // insb

// Dummy definitions used to make sure gcc understands dependencies
// between SET_SEG and GET/READ/WRITE_SEG macros.
extern u16 __segment_ES, __segment_CS, __segment_DS, __segment_SS;
extern u16 __segment_FS, __segment_GS;

// Low level macros for reading/writing memory via a segment selector.
#define READ8_SEG(SEG, value, var)                      \
    __asm__("movb %%" #SEG ":%1, %b0" : "=Qi"(value)    \
            : "m"(var), "m"(__segment_ ## SEG))
#define READ16_SEG(SEG, value, var)                     \
    __asm__("movw %%" #SEG ":%1, %w0" : "=ri"(value)    \
            : "m"(var), "m"(__segment_ ## SEG))
#define READ32_SEG(SEG, value, var)                     \
    __asm__("movl %%" #SEG ":%1, %0" : "=ri"(value)     \
            : "m"(var), "m"(__segment_ ## SEG))
#define READ64_SEG(SEG, value, var) do {                                \
        union u64_u32_u *__w64_ptr = (union u64_u32_u *)&(value);       \
        union u64_u32_u *__r64_ptr = (union u64_u32_u *)&(var);         \
        READ32_SEG(SEG, __w64_ptr->hi, __r64_ptr->hi);                  \
        READ32_SEG(SEG, __w64_ptr->lo, __r64_ptr->lo);                  \
    } while (0)
#define WRITE8_SEG(SEG, var, value)                             \
    __asm__("movb %b1, %%" #SEG ":%0" : "=m"(var)               \
            : "Q"(value), "m"(__segment_ ## SEG))
#define WRITE16_SEG(SEG, var, value)                            \
    __asm__("movw %w1, %%" #SEG ":%0" : "=m"(var)               \
            : "r"(value), "m"(__segment_ ## SEG))
#define WRITE32_SEG(SEG, var, value)                            \
    __asm__("movl %1, %%" #SEG ":%0" : "=m"(var)                \
            : "r"(value), "m"(__segment_ ## SEG))
#define WRITE64_SEG(SEG, var, value) do {                       \
        union u64_u32_u __value;                                \
        union u64_u32_u *__w64_ptr = (union u64_u32_u *)&(var); \
        __value.val = (value);                                  \
        WRITE32_SEG(SEG, __w64_ptr->hi, __value.hi);            \
        WRITE32_SEG(SEG, __w64_ptr->lo, __value.lo);            \
    } while (0)

// Low level macros for getting/setting a segment register.
#define __SET_SEG(SEG, value)                                   \
    __asm__("movw %w1, %%" #SEG : "=m"(__segment_ ## SEG)       \
            : "r"(value))
#define __GET_SEG(SEG) ({                                       \
    u16 __seg;                                                  \
    __asm__("movw %%" #SEG ", %w0" : "=r"(__seg)                \
            : "m"(__segment_ ## SEG));                          \
    __seg;})

// Macros for automatically choosing the appropriate memory size
// access method.
extern void __force_link_error__unknown_type();

#define __GET_VAR(seg, var) ({                  \
    typeof(var) __val;                          \
    if (sizeof(__val) == 1)                     \
        READ8_SEG(seg, __val, var);             \
    else if (sizeof(__val) == 2)                \
        READ16_SEG(seg, __val, var);            \
    else if (sizeof(__val) == 4)                \
        READ32_SEG(seg, __val, var);            \
    else if (sizeof(__val) == 8)                \
        READ64_SEG(seg, __val, var);            \
    else                                        \
        __force_link_error__unknown_type();     \
    __val; })

#define __SET_VAR(seg, var, val) do {           \
        if (sizeof(var) == 1)                   \
            WRITE8_SEG(seg, var, (val));        \
        else if (sizeof(var) == 2)              \
            WRITE16_SEG(seg, var, (val));       \
        else if (sizeof(var) == 4)              \
            WRITE32_SEG(seg, var, (val));       \
        else if (sizeof(var) == 8)              \
            WRITE64_SEG(seg, var, (val));       \
        else                                    \
            __force_link_error__unknown_type(); \
    } while (0)

// Macros for accessing a variable in another segment.  (They
// automatically update the %es segment and then make the appropriate
// access.)
#define __GET_FARVAR(seg, var) ({               \
    SET_SEG(ES, (seg));                         \
    GET_VAR(ES, (var)); })
#define __SET_FARVAR(seg, var, val) do {        \
        typeof(var) __sfv_val = (val);          \
        SET_SEG(ES, (seg));                     \
        SET_VAR(ES, (var), __sfv_val);          \
    } while (0)

// Macros for accesssing a 32bit pointer from 16bit real mode.  (They
// automatically update the %es segment, break the pointer into
// segment/offset, and then make the access.)
#define __GET_FARPTR(ptr) ({                                            \
    typeof(&(ptr)) __ptr = &(ptr);                                      \
    GET_FARVAR(FARPTR_TO_SEG(__ptr)                                     \
               , *(typeof(__ptr))FARPTR_TO_OFFSET(__ptr)); })
#define __SET_FARPTR(ptr, val) do {                                     \
        typeof (&(ptr)) __ptr = &(ptr);                                 \
        SET_FARVAR(FARPTR_TO_SEG(__ptr)                                 \
                   , *(typeof(__ptr))FARPTR_TO_OFFSET(__ptr)            \
                   , (val));                                            \
    } while (0)

// Macros for converting to/from 32bit style pointers to their
// equivalent 16bit segment/offset values.
#define FARPTR_TO_SEG(p) (((u32)(p)) >> 4)
#define FARPTR_TO_OFFSET(p) (((u32)(p)) & 0xf)
#define MAKE_FARPTR(seg,off) ((void*)(((u32)(seg)<<4)+(u32)(off)))


#if MODE16 == 1

// Definitions when in 16 bit mode.
#define GET_FARVAR(seg, var) __GET_FARVAR((seg), (var))
#define SET_FARVAR(seg, var, val) __SET_FARVAR((seg), (var), (val))
#define GET_VAR(seg, var) __GET_VAR(seg, (var))
#define SET_VAR(seg, var, val) __SET_VAR(seg, (var), (val))
#define SET_SEG(SEG, value) __SET_SEG(SEG, (value))
#define GET_SEG(SEG) __GET_SEG(SEG)
#define GET_FARPTR(ptr) __GET_FARPTR(ptr)
#define SET_FARPTR(ptr, val) __SET_FARPTR((ptr), (val))

static inline void insb_far(u16 port, void *farptr, u16 count) {
    SET_SEG(ES, FARPTR_TO_SEG(farptr));
    insb(port, (u8*)FARPTR_TO_OFFSET(farptr), count);
}
static inline void insw_far(u16 port, void *farptr, u16 count) {
    SET_SEG(ES, FARPTR_TO_SEG(farptr));
    insw(port, (u16*)FARPTR_TO_OFFSET(farptr), count);
}
static inline void insl_far(u16 port, void *farptr, u16 count) {
    SET_SEG(ES, FARPTR_TO_SEG(farptr));
    insl(port, (u32*)FARPTR_TO_OFFSET(farptr), count);
}
static inline void outsb_far(u16 port, void *farptr, u16 count) {
    SET_SEG(ES, FARPTR_TO_SEG(farptr));
    outsb(port, (u8*)FARPTR_TO_OFFSET(farptr), count);
}
static inline void outsw_far(u16 port, void *farptr, u16 count) {
    SET_SEG(ES, FARPTR_TO_SEG(farptr));
    outsw(port, (u16*)FARPTR_TO_OFFSET(farptr), count);
}
static inline void outsl_far(u16 port, void *farptr, u16 count) {
    SET_SEG(ES, FARPTR_TO_SEG(farptr));
    outsl(port, (u32*)FARPTR_TO_OFFSET(farptr), count);
}

#else

// In 32-bit mode there is no need to mess with the segments.
#define GET_FARVAR(seg, var) \
    (*((typeof(&(var)))MAKE_FARPTR((seg), (u32)&(var))))
#define SET_FARVAR(seg, var, val) \
    do { GET_FARVAR((seg), (var)) = (val); } while (0)
#define GET_VAR(seg, var) (var)
#define SET_VAR(seg, var, val) do { (var) = (val); } while (0)
#define SET_SEG(SEG, value) ((void)(value))
#define GET_SEG(SEG) 0
#define GET_FARPTR(ptr) (ptr)
#define SET_FARPTR(ptr, val) do { (ptr) = (val); } while (0)

#define insb_far(port, farptr, count) insb(port, farptr, count)
#define insw_far(port, farptr, count) insw(port, farptr, count)
#define insl_far(port, farptr, count) insl(port, farptr, count)
#define outsb_far(port, farptr, count) outsb(port, farptr, count)
#define outsw_far(port, farptr, count) outsw(port, farptr, count)
#define outsl_far(port, farptr, count) outsl(port, farptr, count)

#endif

#endif // farptr.h
