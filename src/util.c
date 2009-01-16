// Misc utility functions.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // usleep
#include "bregs.h" // struct bregs
#include "config.h" // SEG_BIOS
#include "farptr.h" // GET_FARPTR
#include "biosvar.h" // get_ebda_seg

// Call a function with a specified register state.  Note that on
// return, the interrupt enable/disable flag may be altered.
inline void
call16(struct bregs *callregs)
{
    asm volatile(
#if MODE16 == 1
        "calll __call16\n"
#else
        "calll __call16_from32\n"
#endif
        : "+a" (callregs), "+m" (*callregs)
        :
        : "ebx", "ecx", "edx", "esi", "edi", "ebp", "cc", "memory");
}

inline void
call16big(struct bregs *callregs)
{
    extern void __force_link_error__call16big_only_in_32bit_mode();
    if (MODE16)
        __force_link_error__call16big_only_in_32bit_mode();

    asm volatile(
        "calll __call16big_from32\n"
        : "+a" (callregs), "+m" (*callregs)
        :
        : "ebx", "ecx", "edx", "esi", "edi", "ebp", "cc", "memory");
}

inline void
__call16_int(struct bregs *callregs, u16 offset)
{
    callregs->cs = SEG_BIOS;
    callregs->ip = offset;
    call16(callregs);
}

inline void
call16_simpint(int nr, u32 *eax, u32 *flags)
{
    extern void __force_link_error__call16_simpint_only_in_16bit_mode();
    if (!MODE16)
        __force_link_error__call16_simpint_only_in_16bit_mode();

    asm volatile(
        "stc\n"
        "int %2\n"
        "pushfl\n"
        "popl %1\n"
        "cld\n"
        "cli\n"
        : "+a"(*eax), "=r"(*flags)
        : "i"(nr)
        : "cc", "memory");
}

// Switch to the extra stack in ebda and call a function.
inline u32
stack_hop(u32 eax, u32 edx, u32 ecx, void *func)
{
    extern void __force_link_error__stack_hop_only_in_16bit_mode();
    if (!MODE16)
        __force_link_error__stack_hop_only_in_16bit_mode();

    u32 ebda_seg = get_ebda_seg();
    u32 tmp;
    asm volatile(
        // Backup current %ss value.
        "movl %%ss, %4\n"
        // Copy ebda seg to %ss and %ds
        "movl %3, %%ss\n"
        "movl %3, %%ds\n"
        // Backup %esp and set it to new value
        "movl %%esp, %3\n"
        "movl %5, %%esp\n"
        // Call func
        "calll %6\n"
        // Restore segments and stack
        "movl %3, %%esp\n"
        "movl %4, %%ss\n"
        "movl %4, %%ds\n"
        : "+a" (eax), "+d" (edx), "+c" (ecx), "+r" (ebda_seg), "=r" (tmp)
        : "i" (EBDA_OFFSET_TOP_STACK), "m" (*(u8*)func)
        : "cc", "memory");
    return eax;
}

// Sum the bytes in the specified area.
u8
checksum(u8 *far_data, u32 len)
{
    u32 i;
    u8 sum = 0;
    for (i=0; i<len; i++)
        sum += GET_FARPTR(far_data[i]);
    return sum;
}

void *
memset(void *s, int c, size_t n)
{
    while (n)
        ((char *)s)[--n] = c;
    return s;
}

void *
memcpy_far(void *far_d1, const void *far_s1, size_t len)
{
    u8 *d = far_d1;
    u8 *s = (u8*)far_s1;

    while (len--) {
        SET_FARPTR(*d, GET_FARPTR(*s));
        d++;
        s++;
    }

    return far_d1;
}

void *
memcpy(void *d1, const void *s1, size_t len)
{
    u8 *d = (u8*)d1, *s = (u8*)s1;
    while (len--)
        *d++ = *s++;
    return d1;
}

void *
memmove(void *d, const void *s, size_t len)
{
    if (s >= d)
        return memcpy(d, s, len);

    d += len-1;
    s += len-1;
    while (len--) {
        *(char*)d = *(char*)s;
        d--;
        s--;
    }

    return d;
}
