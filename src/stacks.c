// Code for manipulating stack locations.
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "biosvar.h" // get_ebda_seg
#include "util.h" // dprintf


/****************************************************************
 * Stack in EBDA
 ****************************************************************/

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


/****************************************************************
 * Threads
 ****************************************************************/

#define THREADSTACKSIZE 4096

struct thread_info {
    struct thread_info *next;
    void *stackpos;
};

struct thread_info MainThread;

void
thread_setup()
{
    MainThread.next = &MainThread;
    MainThread.stackpos = NULL;
}

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
    if (MODE16 || !CONFIG_THREADS) {
        // Just directly check irqs.
        check_irqs();
        return;
    }
    struct thread_info *cur = getCurThread();
    if (cur == &MainThread)
        // Permit irqs to fire
        check_irqs();

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
