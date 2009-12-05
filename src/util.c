// Misc utility functions.
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // call16
#include "bregs.h" // struct bregs
#include "farptr.h" // GET_FLATPTR
#include "biosvar.h" // get_ebda_seg

static inline u32 getesp() {
    u32 esp;
    asm("movl %%esp, %0" : "=rm"(esp));
    return esp;
}


/****************************************************************
 * 16bit calls
 ****************************************************************/

// Call a function with a specified register state.  Note that on
// return, the interrupt enable/disable flag may be altered.
inline void
call16(struct bregs *callregs)
{
    if (!MODE16 && getesp() > BUILD_STACK_ADDR)
        panic("call16 with invalid stack\n");
    asm volatile(
#if MODE16 == 1
        "calll __call16\n"
        "cli\n"
        "cld"
#else
        "calll __call16_from32"
#endif
        : "+a" (callregs), "+m" (*callregs)
        :
        : "ebx", "ecx", "edx", "esi", "edi", "cc", "memory");
}

inline void
call16big(struct bregs *callregs)
{
    ASSERT32();
    if (getesp() > BUILD_STACK_ADDR)
        panic("call16 with invalid stack\n");
    asm volatile(
        "calll __call16big_from32"
        : "+a" (callregs), "+m" (*callregs)
        :
        : "ebx", "ecx", "edx", "esi", "edi", "cc", "memory");
}

inline void
__call16_int(struct bregs *callregs, u16 offset)
{
    if (MODE16)
        callregs->code.seg = GET_SEG(CS);
    else
        callregs->code.seg = SEG_BIOS;
    callregs->code.offset = offset;
    call16(callregs);
}

// Switch to the extra stack in ebda and call a function.
inline u32
stack_hop(u32 eax, u32 edx, u32 ecx, void *func)
{
    ASSERT16();
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
        "movl %4, %%esp"
        : "+a" (eax), "+d" (edx), "+c" (ecx), "=&r" (bkup_ss), "=&r" (bkup_esp)
        : "i" (EBDA_OFFSET_TOP_STACK), "r" (ebda_seg), "m" (*(u8*)func)
        : "cc", "memory");
    return eax;
}

// 16bit trampoline for enabling irqs from 32bit mode.
ASM16(
    "  .global trampoline_checkirqs\n"
    "trampoline_checkirqs:\n"
    "  rep ; nop\n"
    "  lretw"
    );

static void
check_irqs32()
{
    extern void trampoline_checkirqs();
    struct bregs br;
    br.flags = F_IF;
    br.code.seg = SEG_BIOS;
    br.code.offset = (u32)&trampoline_checkirqs;
    call16big(&br);
}

static void
check_irqs16()
{
    asm volatile(
        "sti\n"
        "nop\n"
        "rep ; nop\n"
        "cli\n"
        "cld\n"
        : : :"memory");
}


/****************************************************************
 * Threads
 ****************************************************************/

#define THREADSTACKSIZE 4096

struct thread_info {
    struct thread_info *next;
    void *stackpos;
};

struct thread_info MainThread = {&MainThread, NULL};

struct thread_info *
getCurThread()
{
    u32 esp = getesp();
    if (esp <= BUILD_STACK_ADDR)
        return &MainThread;
    return (void*)ALIGN_DOWN(esp, THREADSTACKSIZE);
}

// Briefly permit irqs to occur.
void
yield()
{
    if (MODE16) {
        // In 16bit mode, just directly check irqs.
        check_irqs16();
        return;
    }
    if (! CONFIG_THREADS) {
        check_irqs32();
        return;
    }
    struct thread_info *cur = getCurThread();
    if (cur == &MainThread)
        // Permit irqs to fire
        check_irqs32();

    // Switch to the next thread
    struct thread_info *next = cur->next;
    asm volatile(
        "  pushl $1f\n"                 // store return pc
        "  pushl %%ebp\n"               // backup %ebp
        "  movl %%esp, 4(%%eax)\n"      // cur->stackpos = %esp
        "  movl 4(%%ecx), %%esp\n"      // %esp = next->stackpos
        "  popl %%ebp\n"                // restore %ebp
        "  retl\n"                      // restore pc
        "1:\n"
        : "+a"(cur), "+c"(next)
        :
        : "ebx", "edx", "esi", "edi", "cc", "memory");
}

// Last thing called from a thread (called on "next" stack).
static void
__end_thread(struct thread_info *old)
{
    struct thread_info *pos = &MainThread;
    while (pos->next != old)
        pos = pos->next;
    pos->next = old->next;
    free(old);
    dprintf(DEBUG_thread, "\\%08x/ End thread\n", (u32)old);
}

void
run_thread(void (*func)(void*), void *data)
{
    ASSERT32();
    if (! CONFIG_THREADS)
        goto fail;
    struct thread_info *thread;
    thread = memalign_tmphigh(THREADSTACKSIZE, THREADSTACKSIZE);
    if (!thread)
        goto fail;

    thread->stackpos = (void*)thread + THREADSTACKSIZE;
    struct thread_info *cur = getCurThread();
    thread->next = cur->next;
    cur->next = thread;

    dprintf(DEBUG_thread, "/%08x\\ Start thread\n", (u32)thread);
    asm volatile(
        // Start thread
        "  pushl $1f\n"                 // store return pc
        "  pushl %%ebp\n"               // backup %ebp
        "  movl %%esp, 4(%%edx)\n"      // cur->stackpos = %esp
        "  movl 4(%%ebx), %%esp\n"      // %esp = thread->stackpos
        "  calll *%%ecx\n"              // Call func

        // End thread
        "  movl (%%ebx), %%ecx\n"       // %ecx = thread->next
        "  movl 4(%%ecx), %%esp\n"      // %esp = next->stackpos
        "  movl %%ebx, %%eax\n"
        "  calll %4\n"                  // call __end_thread(thread)
        "  popl %%ebp\n"                // restore %ebp
        "  retl\n"                      // restore pc
        "1:\n"
        : "+a"(data), "+c"(func), "+b"(thread), "+d"(cur)
        : "m"(*(u8*)__end_thread)
        : "esi", "edi", "cc", "memory");
    return;

fail:
    func(data);
}

void
wait_threads()
{
    ASSERT32();
    if (! CONFIG_THREADS)
        return;
    while (MainThread.next != &MainThread)
        yield();
}


/****************************************************************
 * String ops
 ****************************************************************/

// Sum the bytes in the specified area.
u8
checksum_far(u16 buf_seg, void *buf_far, u32 len)
{
    SET_SEG(ES, buf_seg);
    u32 i;
    u8 sum = 0;
    for (i=0; i<len; i++)
        sum += GET_VAR(ES, ((u8*)buf_far)[i]);
    return sum;
}

u8
checksum(void *buf, u32 len)
{
    return checksum_far(GET_SEG(SS), buf, len);
}

size_t
strlen(const char *s)
{
    if (__builtin_constant_p(s))
        return __builtin_strlen(s);
    const char *p = s;
    while (*p)
        p++;
    return p-s;
}

// Compare two areas of memory.
int
memcmp(const void *s1, const void *s2, size_t n)
{
    while (n) {
        if (*(u8*)s1 != *(u8*)s2)
            return *(u8*)s1 < *(u8*)s2 ? -1 : 1;
        s1++;
        s2++;
        n--;
    }
    return 0;
}

// Compare two strings.
int
strcmp(const char *s1, const char *s2)
{
    for (;;) {
        if (*s1 != *s2)
            return *s1 < *s2 ? -1 : 1;
        if (! *s1)
            return 0;
        s1++;
        s2++;
    }
}

inline void
memset_far(u16 d_seg, void *d_far, u8 c, size_t len)
{
    SET_SEG(ES, d_seg);
    asm volatile(
        "rep stosb %%es:(%%di)"
        : "+c"(len), "+D"(d_far)
        : "a"(c)
        : "cc", "memory");
}

inline void
memset16_far(u16 d_seg, void *d_far, u16 c, size_t len)
{
    len /= 2;
    SET_SEG(ES, d_seg);
    asm volatile(
        "rep stosw %%es:(%%di)"
        : "+c"(len), "+D"(d_far)
        : "a"(c)
        : "cc", "memory");
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
        "movw %w0, %%ds"
        : "=&r"(bkup_ds), "+c"(len), "+S"(s_far), "+D"(d_far)
        : "r"(s_seg)
        : "cc", "memory");
}

void *
#undef memcpy
memcpy(void *d1, const void *s1, size_t len)
#if MODE16 == 0
#define memcpy __builtin_memcpy
#endif
{
    SET_SEG(ES, GET_SEG(SS));
    void *d = d1;
    if (((u32)d1 | (u32)s1 | len) & 3) {
        // non-aligned memcpy
        asm volatile(
            "rep movsb (%%esi),%%es:(%%edi)"
            : "+c"(len), "+S"(s1), "+D"(d)
            : : "cc", "memory");
        return d1;
    }
    // Common case - use 4-byte copy
    len /= 4;
    asm volatile(
        "rep movsl (%%esi),%%es:(%%edi)"
        : "+c"(len), "+S"(s1), "+D"(d)
        : : "cc", "memory");
    return d1;
}

// Copy from memory mapped IO.  IO mem is very slow, so yield
// periodically.  'len' must be 4 byte aligned.
void
iomemcpy(void *d, const void *s, u32 len)
{
    yield();
    while (len) {
        u32 copylen = len;
        if (copylen > 1024)
            copylen = 1024;
        len -= copylen;
        copylen /= 4;
        asm volatile(
            "rep movsl (%%esi),%%es:(%%edi)"
            : "+c"(copylen), "+S"(s), "+D"(d)
            : : "cc", "memory");
        yield();
    }
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


/****************************************************************
 * Keyboard calls
 ****************************************************************/

// Wait for 'usec' microseconds using (with irqs enabled) using int 1586.
void
biosusleep(u32 usec)
{
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.flags = F_IF;
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
    br.flags = F_IF;
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
    br.flags = F_IF;
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
        biosusleep(50*1000);
        msec -= 50;
    }
}
