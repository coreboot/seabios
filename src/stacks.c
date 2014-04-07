// Code for manipulating stack locations.
//
// Copyright (C) 2009-2010  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "biosvar.h" // GET_GLOBAL
#include "bregs.h" // CR0_PE
#include "hw/rtc.h" // rtc_use
#include "list.h" // hlist_node
#include "malloc.h" // free
#include "output.h" // dprintf
#include "romfile.h" // romfile_loadint
#include "stacks.h" // struct mutex_s
#include "util.h" // useRTC

#define MAIN_STACK_MAX (1024*1024)


/****************************************************************
 * Extra 16bit stack
 ****************************************************************/

// Space for a stack for 16bit code.
u8 ExtraStack[BUILD_EXTRA_STACK_SIZE+1] VARLOW __aligned(8);
u8 *StackPos VARLOW;

// Test if currently on the extra stack
static inline int
on_extra_stack(void)
{
    return MODE16 && GET_SEG(SS) == SEG_LOW && getesp() > (u32)ExtraStack;
}

// Switch to the extra stack and call a function.
u32
stack_hop(u32 eax, u32 edx, void *func)
{
    if (on_extra_stack())
        return ((u32 (*)(u32, u32))func)(eax, edx);
    ASSERT16();
    u16 stack_seg = SEG_LOW;
    u32 bkup_ss, bkup_esp;
    asm volatile(
        // Backup current %ss/%esp values.
        "movw %%ss, %w3\n"
        "movl %%esp, %4\n"
        // Copy stack seg to %ds/%ss and set %esp
        "movw %w6, %%ds\n"
        "movw %w6, %%ss\n"
        "movl %5, %%esp\n"
        "pushl %3\n"
        "pushl %4\n"
        // Call func
        "calll *%2\n"
        "popl %4\n"
        "popl %3\n"
        // Restore segments and stack
        "movw %w3, %%ds\n"
        "movw %w3, %%ss\n"
        "movl %4, %%esp"
        : "+a" (eax), "+d" (edx), "+c" (func), "=&r" (bkup_ss), "=&r" (bkup_esp)
        : "m" (StackPos), "r" (stack_seg)
        : "cc", "memory");
    return eax;
}

// Switch back to original caller's stack and call a function.
u32
stack_hop_back(u32 eax, u32 edx, void *func)
{
    if (!on_extra_stack())
        return ((u32 (*)(u32, u32))func)(eax, edx);
    ASSERT16();
    u16 bkup_ss;
    u32 bkup_stack_pos, temp;
    asm volatile(
        // Backup stack_pos and current %ss/%esp
        "movl %6, %4\n"
        "movw %%ss, %w3\n"
        "movl %%esp, %6\n"
        // Restore original callers' %ss/%esp
        "movl -4(%4), %5\n"
        "movl %5, %%ss\n"
        "movw %%ds:-8(%4), %%sp\n"
        "movl %5, %%ds\n"
        // Call func
        "calll *%2\n"
        // Restore %ss/%esp and stack_pos
        "movw %w3, %%ds\n"
        "movw %w3, %%ss\n"
        "movl %6, %%esp\n"
        "movl %4, %6"
        : "+a" (eax), "+d" (edx), "+c" (func), "=&r" (bkup_ss)
          , "=&r" (bkup_stack_pos), "=&r" (temp), "+m" (StackPos)
        :
        : "cc", "memory");
    return eax;
}


/****************************************************************
 * 16bit / 32bit calling
 ****************************************************************/

u16 StackSeg VARLOW;

// Call a 32bit SeaBIOS function from a 16bit SeaBIOS function.
u32 VISIBLE16
call32(void *func, u32 eax, u32 errret)
{
    ASSERT16();
    u32 cr0 = getcr0();
    if (cr0 & CR0_PE)
        // Called in 16bit protected mode?!
        return errret;

    // Backup cmos index register and disable nmi
    u8 cmosindex = inb(PORT_CMOS_INDEX);
    outb(cmosindex | NMI_DISABLE_BIT, PORT_CMOS_INDEX);
    inb(PORT_CMOS_DATA);

    // Backup fs/gs and gdt
    u16 fs = GET_SEG(FS), gs = GET_SEG(GS);
    struct descloc_s gdt;
    sgdt(&gdt);

    u16 oldstackseg = GET_LOW(StackSeg);
    SET_LOW(StackSeg, GET_SEG(SS));
    u32 bkup_ss, bkup_esp;
    asm volatile(
        // Backup ss/esp / set esp to flat stack location
        "  movl %%ss, %0\n"
        "  movl %%esp, %1\n"
        "  shll $4, %0\n"
        "  addl %0, %%esp\n"
        "  shrl $4, %0\n"

        // Transition to 32bit mode, call func, return to 16bit
        "  movl $(" __stringify(BUILD_BIOS_ADDR) " + 1f), %%edx\n"
        "  jmp transition32\n"
        "  .code32\n"
        "1:calll *%3\n"
        "  movl $2f, %%edx\n"
        "  jmp transition16big\n"

        // Restore ds/ss/esp
        "  .code16gcc\n"
        "2:movl %0, %%ds\n"
        "  movl %0, %%ss\n"
        "  movl %1, %%esp\n"
        : "=&r" (bkup_ss), "=&r" (bkup_esp), "+a" (eax)
        : "r" (func)
        : "ecx", "edx", "cc", "memory");

    SET_LOW(StackSeg, oldstackseg);

    // Restore gdt and fs/gs
    lgdt(&gdt);
    SET_SEG(FS, fs);
    SET_SEG(GS, gs);

    // Restore cmos index register
    outb(cmosindex, PORT_CMOS_INDEX);
    inb(PORT_CMOS_DATA);
    return eax;
}

// Call a 16bit SeaBIOS function from a 32bit SeaBIOS function.
static inline u32
call16(u32 eax, u32 edx, void *func)
{
    ASSERT32FLAT();
    if (getesp() > MAIN_STACK_MAX)
        panic("call16 with invalid stack\n");
    extern u32 __call16(u32 eax, u32 edx, void *func);
    return __call16(eax, edx, func - BUILD_BIOS_ADDR);
}

static inline u32
call16big(u32 eax, u32 edx, void *func)
{
    ASSERT32FLAT();
    if (getesp() > MAIN_STACK_MAX)
        panic("call16big with invalid stack\n");
    extern u32 __call16big(u32 eax, u32 edx, void *func);
    return __call16big(eax, edx, func - BUILD_BIOS_ADDR);
}


/****************************************************************
 * External 16bit interface calling
 ****************************************************************/

// Far call 16bit code with a specified register state.
void VISIBLE16
_farcall16(struct bregs *callregs, u16 callregseg)
{
    ASSERT16();
    if (on_extra_stack()) {
        stack_hop_back((u32)callregs, callregseg, _farcall16);
        return;
    }
    asm volatile(
        "calll __farcall16\n"
        : "+a" (callregs), "+m" (*callregs), "+d" (callregseg)
        :
        : "ebx", "ecx", "esi", "edi", "cc", "memory");
}

inline void
farcall16(struct bregs *callregs)
{
    if (MODE16) {
        _farcall16(callregs, GET_SEG(SS));
        return;
    }
    extern void _cfunc16__farcall16(void);
    call16((u32)callregs - StackSeg * 16, StackSeg, _cfunc16__farcall16);
}

inline void
farcall16big(struct bregs *callregs)
{
    extern void _cfunc16__farcall16(void);
    call16big((u32)callregs - StackSeg * 16, StackSeg, _cfunc16__farcall16);
}

// Invoke a 16bit software interrupt.
inline void
__call16_int(struct bregs *callregs, u16 offset)
{
    if (MODESEGMENT)
        callregs->code.seg = GET_SEG(CS);
    else
        callregs->code.seg = SEG_BIOS;
    callregs->code.offset = offset;
    farcall16(callregs);
}


/****************************************************************
 * Threads
 ****************************************************************/

// Thread info - stored at bottom of each thread stack - don't change
// without also updating the inline assembler below.
struct thread_info {
    void *stackpos;
    struct hlist_node node;
};
struct thread_info MainThread VARFSEG = {
    NULL, { &MainThread.node, &MainThread.node.next }
};
#define THREADSTACKSIZE 4096

// Check if any threads are running.
static int
have_threads(void)
{
    return (CONFIG_THREADS
            && GET_FLATPTR(MainThread.node.next) != &MainThread.node);
}

// Return the 'struct thread_info' for the currently running thread.
struct thread_info *
getCurThread(void)
{
    u32 esp = getesp();
    if (esp <= MAIN_STACK_MAX)
        return &MainThread;
    return (void*)ALIGN_DOWN(esp, THREADSTACKSIZE);
}

static int ThreadControl;

// Initialize the support for internal threads.
void
thread_init(void)
{
    if (! CONFIG_THREADS)
        return;
    ThreadControl = romfile_loadint("etc/threads", 1);
}

// Should hardware initialization threads run during optionrom execution.
int
threads_during_optionroms(void)
{
    return CONFIG_THREADS && ThreadControl == 2;
}

// Switch to next thread stack.
static void
switch_next(struct thread_info *cur)
{
    struct thread_info *next = container_of(
        cur->node.next, struct thread_info, node);
    if (cur == next)
        // Nothing to do.
        return;
    asm volatile(
        "  pushl $1f\n"                 // store return pc
        "  pushl %%ebp\n"               // backup %ebp
        "  movl %%esp, (%%eax)\n"       // cur->stackpos = %esp
        "  movl (%%ecx), %%esp\n"       // %esp = next->stackpos
        "  popl %%ebp\n"                // restore %ebp
        "  retl\n"                      // restore pc
        "1:\n"
        : "+a"(cur), "+c"(next)
        :
        : "ebx", "edx", "esi", "edi", "cc", "memory");
}

// Last thing called from a thread (called on MainThread stack).
static void
__end_thread(struct thread_info *old)
{
    hlist_del(&old->node);
    dprintf(DEBUG_thread, "\\%08x/ End thread\n", (u32)old);
    free(old);
    if (!have_threads())
        dprintf(1, "All threads complete.\n");
}

// Create a new thread and start executing 'func' in it.
void
run_thread(void (*func)(void*), void *data)
{
    ASSERT32FLAT();
    if (! CONFIG_THREADS || ! ThreadControl)
        goto fail;
    struct thread_info *thread;
    thread = memalign_tmphigh(THREADSTACKSIZE, THREADSTACKSIZE);
    if (!thread)
        goto fail;

    dprintf(DEBUG_thread, "/%08x\\ Start thread\n", (u32)thread);
    thread->stackpos = (void*)thread + THREADSTACKSIZE;
    struct thread_info *cur = getCurThread();
    hlist_add_after(&thread->node, &cur->node);
    asm volatile(
        // Start thread
        "  pushl $1f\n"                 // store return pc
        "  pushl %%ebp\n"               // backup %ebp
        "  movl %%esp, (%%edx)\n"       // cur->stackpos = %esp
        "  movl (%%ebx), %%esp\n"       // %esp = thread->stackpos
        "  calll *%%ecx\n"              // Call func

        // End thread
        "  movl %%ebx, %%eax\n"         // %eax = thread
        "  movl 4(%%ebx), %%ebx\n"      // %ebx = thread->node.next
        "  movl (%5), %%esp\n"          // %esp = MainThread.stackpos
        "  calll %4\n"                  // call __end_thread(thread)
        "  movl -4(%%ebx), %%esp\n"     // %esp = next->stackpos
        "  popl %%ebp\n"                // restore %ebp
        "  retl\n"                      // restore pc
        "1:\n"
        : "+a"(data), "+c"(func), "+b"(thread), "+d"(cur)
        : "m"(*(u8*)__end_thread), "m"(MainThread)
        : "esi", "edi", "cc", "memory");
    return;

fail:
    func(data);
}


/****************************************************************
 * Thread helpers
 ****************************************************************/

// Low-level irq enable.
void VISIBLE16
check_irqs(void)
{
    if (on_extra_stack()) {
        stack_hop_back(0, 0, check_irqs);
        return;
    }
    asm volatile("sti ; nop ; rep ; nop ; cli ; cld" : : :"memory");
}

// Briefly permit irqs to occur.
void
yield(void)
{
    if (MODESEGMENT) {
        check_irqs();
        return;
    }
    extern void _cfunc16_check_irqs(void);
    if (!CONFIG_THREADS) {
        call16big(0, 0, _cfunc16_check_irqs);
        return;
    }
    struct thread_info *cur = getCurThread();
    if (cur == &MainThread)
        // Permit irqs to fire
        call16big(0, 0, _cfunc16_check_irqs);

    // Switch to the next thread
    switch_next(cur);
}

void VISIBLE16
wait_irq(void)
{
    if (on_extra_stack()) {
        stack_hop_back(0, 0, wait_irq);
        return;
    }
    asm volatile("sti ; hlt ; cli ; cld": : :"memory");
}

// Wait for next irq to occur.
void
yield_toirq(void)
{
    if (MODESEGMENT) {
        wait_irq();
        return;
    }
    if (have_threads()) {
        // Threads still active - do a yield instead.
        yield();
        return;
    }
    extern void _cfunc16_wait_irq(void);
    call16big(0, 0, _cfunc16_wait_irq);
}

// Wait for all threads (other than the main thread) to complete.
void
wait_threads(void)
{
    ASSERT32FLAT();
    while (have_threads())
        yield();
}

void
mutex_lock(struct mutex_s *mutex)
{
    ASSERT32FLAT();
    if (! CONFIG_THREADS)
        return;
    while (mutex->isLocked)
        yield();
    mutex->isLocked = 1;
}

void
mutex_unlock(struct mutex_s *mutex)
{
    ASSERT32FLAT();
    if (! CONFIG_THREADS)
        return;
    mutex->isLocked = 0;
}


/****************************************************************
 * Thread preemption
 ****************************************************************/

int CanPreempt VARFSEG;
static u32 PreemptCount;

// Turn on RTC irqs and arrange for them to check the 32bit threads.
void
start_preempt(void)
{
    if (! threads_during_optionroms())
        return;
    CanPreempt = 1;
    PreemptCount = 0;
    rtc_use();
}

// Turn off RTC irqs / stop checking for thread execution.
void
finish_preempt(void)
{
    if (! threads_during_optionroms()) {
        yield();
        return;
    }
    CanPreempt = 0;
    rtc_release();
    dprintf(9, "Done preempt - %d checks\n", PreemptCount);
    yield();
}

// Check if preemption is on, and wait for it to complete if so.
int
wait_preempt(void)
{
    if (MODESEGMENT || !CONFIG_THREADS || !CanPreempt
        || getesp() < MAIN_STACK_MAX)
        return 0;
    while (CanPreempt)
        yield();
    return 1;
}

// Try to execute 32bit threads.
void VISIBLE32INIT
yield_preempt(void)
{
    PreemptCount++;
    switch_next(&MainThread);
}

// 16bit code that checks if threads are pending and executes them if so.
void
check_preempt(void)
{
    extern void _cfunc32flat_yield_preempt(void);
    if (CONFIG_THREADS && GET_GLOBAL(CanPreempt) && have_threads())
        call32(_cfunc32flat_yield_preempt, 0, 0);
}


/****************************************************************
 * call32 helper
 ****************************************************************/

struct call32_params_s {
    void *func;
    u32 eax, edx, ecx;
};

u32 VISIBLE32FLAT
call32_params_helper(struct call32_params_s *params)
{
    return ((u32 (*)(u32, u32, u32))params->func)(
        params->eax, params->edx, params->ecx);
}

u32
call32_params(void *func, u32 eax, u32 edx, u32 ecx, u32 errret)
{
    ASSERT16();
    struct call32_params_s params = {func, eax, edx, ecx};
    extern void _cfunc32flat_call32_params_helper(void);
    return call32(_cfunc32flat_call32_params_helper
                  , (u32)MAKE_FLATPTR(GET_SEG(SS), &params), errret);
}
