// Code to access multiple segments within gcc.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

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

#define GET_VAR(seg, var) ({                                    \
    typeof(var) __val;                                          \
    if (__builtin_types_compatible_p(typeof(__val), u8))        \
        __val = READ8_SEG(seg, var);                            \
    else if (__builtin_types_compatible_p(typeof(__val), u16))  \
        __val = READ16_SEG(seg, var);                           \
    else if (__builtin_types_compatible_p(typeof(__val), u32))  \
        __val = READ32_SEG(seg, var);                           \
    __val; })

#define SET_VAR(seg, var, val) do {                               \
        if (__builtin_types_compatible_p(typeof(var), u8))        \
            WRITE8_SEG(seg, var, (val));                          \
        else if (__builtin_types_compatible_p(typeof(var), u16))  \
            WRITE16_SEG(seg, var, (val));                         \
        else if (__builtin_types_compatible_p(typeof(var), u32))  \
            WRITE32_SEG(seg, var, (val));                         \
    } while (0)

#define SET_SEG(SEG, value)                                     \
    __asm__ __volatile__("movw %w0, %%" #SEG : : "r"(value))
#define GET_SEG(SEG) ({                                         \
    u16 __seg;                                                  \
    __asm__ __volatile__("movw %%" #SEG ", %w0" : "=r"(__seg)); \
    __seg;})

