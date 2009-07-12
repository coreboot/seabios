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
#define READ64_SEG(SEG, value, var) do {                        \
        union u64_u32_u __value;                                \
        union u64_u32_u *__r64_ptr = (union u64_u32_u *)&(var); \
        READ32_SEG(SEG, __value.hi, __r64_ptr->hi);             \
        READ32_SEG(SEG, __value.lo, __r64_ptr->lo);             \
        *(u64*)&(value) = __value.val;                          \
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
            : "rm"(value))
#define __GET_SEG(SEG) ({                                       \
    u16 __seg;                                                  \
    __asm__("movw %%" #SEG ", %w0" : "=rm"(__seg)               \
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

// Macros for accesssing a 32bit flat mode pointer from 16bit real
// mode.  (They automatically update the %es segment, break the
// pointer into segment/offset, and then make the access.)
#define __GET_FLATPTR(ptr) ({                                   \
    typeof(&(ptr)) __ptr = &(ptr);                              \
    GET_FARVAR(FLATPTR_TO_SEG(__ptr)                            \
               , *(typeof(__ptr))FLATPTR_TO_OFFSET(__ptr)); })
#define __SET_FLATPTR(ptr, val) do {                            \
        typeof (&(ptr)) __ptr = &(ptr);                         \
        SET_FARVAR(FLATPTR_TO_SEG(__ptr)                        \
                   , *(typeof(__ptr))FLATPTR_TO_OFFSET(__ptr)   \
                   , (val));                                    \
    } while (0)

// Macros for converting to/from 32bit flat mode pointers to their
// equivalent 16bit segment/offset values.
#define FLATPTR_TO_SEG(p) (((u32)(p)) >> 4)
#define FLATPTR_TO_OFFSET(p) (((u32)(p)) & 0xf)
#define MAKE_FLATPTR(seg,off) ((void*)(((u32)(seg)<<4)+(u32)(off)))


#if MODE16 == 1

// Definitions when in 16 bit mode.
#define GET_FARVAR(seg, var) __GET_FARVAR((seg), (var))
#define SET_FARVAR(seg, var, val) __SET_FARVAR((seg), (var), (val))
#define GET_VAR(seg, var) __GET_VAR(seg, (var))
#define SET_VAR(seg, var, val) __SET_VAR(seg, (var), (val))
#define SET_SEG(SEG, value) __SET_SEG(SEG, (value))
#define GET_SEG(SEG) __GET_SEG(SEG)
#define GET_FLATPTR(ptr) __GET_FLATPTR(ptr)
#define SET_FLATPTR(ptr, val) __SET_FLATPTR((ptr), (val))

static inline void insb_fl(u16 port, void *ptr_fl, u16 count) {
    SET_SEG(ES, FLATPTR_TO_SEG(ptr_fl));
    insb(port, (u8*)FLATPTR_TO_OFFSET(ptr_fl), count);
}
static inline void insw_fl(u16 port, void *ptr_fl, u16 count) {
    SET_SEG(ES, FLATPTR_TO_SEG(ptr_fl));
    insw(port, (u16*)FLATPTR_TO_OFFSET(ptr_fl), count);
}
static inline void insl_fl(u16 port, void *ptr_fl, u16 count) {
    SET_SEG(ES, FLATPTR_TO_SEG(ptr_fl));
    insl(port, (u32*)FLATPTR_TO_OFFSET(ptr_fl), count);
}
static inline void outsb_fl(u16 port, void *ptr_fl, u16 count) {
    SET_SEG(ES, FLATPTR_TO_SEG(ptr_fl));
    outsb(port, (u8*)FLATPTR_TO_OFFSET(ptr_fl), count);
}
static inline void outsw_fl(u16 port, void *ptr_fl, u16 count) {
    SET_SEG(ES, FLATPTR_TO_SEG(ptr_fl));
    outsw(port, (u16*)FLATPTR_TO_OFFSET(ptr_fl), count);
}
static inline void outsl_fl(u16 port, void *ptr_fl, u16 count) {
    SET_SEG(ES, FLATPTR_TO_SEG(ptr_fl));
    outsl(port, (u32*)FLATPTR_TO_OFFSET(ptr_fl), count);
}

extern void __force_link_error__only_in_32bit() __attribute__ ((noreturn));
#define ASSERT16() do { } while (0)
#define ASSERT32() __force_link_error__only_in_32bit()

#else

// In 32-bit mode there is no need to mess with the segments.
#define GET_FARVAR(seg, var) \
    (*((typeof(&(var)))MAKE_FLATPTR((seg), &(var))))
#define SET_FARVAR(seg, var, val) \
    do { GET_FARVAR((seg), (var)) = (val); } while (0)
#define GET_VAR(seg, var) (var)
#define SET_VAR(seg, var, val) do { (var) = (val); } while (0)
#define SET_SEG(SEG, value) ((void)(value))
#define GET_SEG(SEG) 0
#define GET_FLATPTR(ptr) (ptr)
#define SET_FLATPTR(ptr, val) do { (ptr) = (val); } while (0)

#define insb_fl(port, ptr_fl, count) insb(port, ptr_fl, count)
#define insw_fl(port, ptr_fl, count) insw(port, ptr_fl, count)
#define insl_fl(port, ptr_fl, count) insl(port, ptr_fl, count)
#define outsb_fl(port, ptr_fl, count) outsb(port, ptr_fl, count)
#define outsw_fl(port, ptr_fl, count) outsw(port, ptr_fl, count)
#define outsl_fl(port, ptr_fl, count) outsl(port, ptr_fl, count)

extern void __force_link_error__only_in_16bit() __attribute__ ((noreturn));
#define ASSERT16() __force_link_error__only_in_16bit()
#define ASSERT32() do { } while (0)

#endif

#endif // farptr.h
