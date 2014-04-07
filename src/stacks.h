// Misc function and variable declarations.
#ifndef __STACKS_H
#define __STACKS_H

#include "types.h" // u32

// stacks.c
extern u8 ExtraStack[], *StackPos;
u32 stack_hop(u32 eax, u32 edx, void *func);
u32 stack_hop_back(u32 eax, u32 edx, void *func);
u32 call32(void *func, u32 eax, u32 errret);
struct bregs;
inline void farcall16(struct bregs *callregs);
inline void farcall16big(struct bregs *callregs);
inline void __call16_int(struct bregs *callregs, u16 offset);
#define call16_int(nr, callregs) do {                           \
        extern void irq_trampoline_ ##nr ();                    \
        __call16_int((callregs), (u32)&irq_trampoline_ ##nr );  \
    } while (0)
extern struct thread_info MainThread;
struct thread_info *getCurThread(void);
void yield(void);
void yield_toirq(void);
void thread_init(void);
int threads_during_optionroms(void);
void run_thread(void (*func)(void*), void *data);
void wait_threads(void);
struct mutex_s { u32 isLocked; };
void mutex_lock(struct mutex_s *mutex);
void mutex_unlock(struct mutex_s *mutex);
void start_preempt(void);
void finish_preempt(void);
int wait_preempt(void);
void check_preempt(void);
u32 call32_params(void *func, u32 eax, u32 edx, u32 ecx, u32 errret);

#endif // stacks.h
