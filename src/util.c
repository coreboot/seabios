// Misc utility functions.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // call16
#include "bregs.h" // struct bregs
#include "farptr.h" // GET_FLATPTR
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
    if (MODE16)
        callregs->cs = GET_SEG(CS);
    else
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

    u16 ebda_seg = get_ebda_seg(), bkup_ss;
    u32 bkup_esp;
    asm volatile(
        // Backup current %ss/%esp values.
        "movw %%ss, %w3\n"
        "movl %%esp, %4\n"
        // Copy ebda seg to %ds/%ss and set %esp
        "movw %w6, %%ds\n"
        "movw %w6, %%ss\n"
        "movl %5, %%esp\n"
        // Call func
        "calll %7\n"
        // Restore segments and stack
        "movw %w3, %%ds\n"
        "movw %w3, %%ss\n"
        "movl %4, %%esp\n"
        : "+a" (eax), "+d" (edx), "+c" (ecx), "=&r" (bkup_ss), "=&r" (bkup_esp)
        : "i" (EBDA_OFFSET_TOP_STACK), "r" (ebda_seg), "m" (*(u8*)func)
        : "cc", "memory");
    return eax;
}

// Sum the bytes in the specified area.
u8
checksum_far(u16 buf_seg, u8 *buf_far, u32 len)
{
    SET_SEG(ES, buf_seg);
    u32 i;
    u8 sum = 0;
    for (i=0; i<len; i++)
        sum += GET_VAR(ES, buf_far[i]);
    return sum;
}

u8
checksum(u8 *buf, u32 len)
{
    return checksum_far(GET_SEG(SS), buf, len);
}

void *
memset(void *s, int c, size_t n)
{
    while (n)
        ((char *)s)[--n] = c;
    return s;
}

inline void
memcpy_far(u16 d_seg, void *d_far, u16 s_seg, const void *s_far, size_t len)
{
    SET_SEG(ES, d_seg);
    u16 bkup_ds;
    asm volatile(
        "movw %%ds, %w0\n"
        "movw %w4, %%ds\n"
        "rep movsb (%%si),%%es:(%%di)\n"
        "movw %w0, %%ds\n"
        : "=&r"(bkup_ds), "+c"(len), "+S"(s_far), "+D"(d_far)
        : "r"(s_seg)
        : "cc", "memory");
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

// Copy a string - truncating it if necessary.
char *
strtcpy(char *dest, const char *src, size_t len)
{
    char *d = dest;
    while (len-- && *src != '\0')
        *d++ = *src++;
    *d = '\0';
    return dest;
}

// Wait for 'usec' microseconds with irqs enabled.
void
usleep(u32 usec)
{
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.ah = 0x86;
    br.cx = usec >> 16;
    br.dx = usec;
    call16_int(0x15, &br);
}

// See if a keystroke is pending in the keyboard buffer.
static int
check_for_keystroke()
{
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.ah = 1;
    call16_int(0x16, &br);
    return !(br.flags & F_ZF);
}

// Return a keystroke - waiting forever if necessary.
static int
get_raw_keystroke()
{
    struct bregs br;
    memset(&br, 0, sizeof(br));
    call16_int(0x16, &br);
    return br.ah;
}

// Read a keystroke - waiting up to 'msec' milliseconds.
int
get_keystroke(int msec)
{
    for (;;) {
        if (check_for_keystroke())
            return get_raw_keystroke();
        if (msec <= 0)
            return -1;
        usleep(50*1000);
        msec -= 50;
    }
}
