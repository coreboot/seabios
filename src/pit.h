// Definitions for the Intel 8253 Programmable Interrupt Timer (PIT).
#ifndef __PIT_H
#define __PIT_H

/* PM Timer ticks per second (HZ) */
#define PM_TIMER_FREQUENCY  3579545

#define PIT_TICK_RATE 1193180   // Underlying HZ of PIT
#define PIT_TICK_INTERVAL 65536 // Default interval for 18.2Hz timer
#define TICKS_PER_DAY (u32)((u64)60*60*24*PIT_TICK_RATE / PIT_TICK_INTERVAL)

// Bits for PORT_PIT_MODE
#define PM_SEL_TIMER0   (0<<6)
#define PM_SEL_TIMER1   (1<<6)
#define PM_SEL_TIMER2   (2<<6)
#define PM_SEL_READBACK (3<<6)
#define PM_ACCESS_LATCH  (0<<4)
#define PM_ACCESS_LOBYTE (1<<4)
#define PM_ACCESS_HIBYTE (2<<4)
#define PM_ACCESS_WORD   (3<<4)
#define PM_MODE0 (0<<1)
#define PM_MODE1 (1<<1)
#define PM_MODE2 (2<<1)
#define PM_MODE3 (3<<1)
#define PM_MODE4 (4<<1)
#define PM_MODE5 (5<<1)
#define PM_CNT_BINARY (0<<0)
#define PM_CNT_BCD    (1<<0)
#define PM_READ_COUNTER0 (1<<1)
#define PM_READ_COUNTER1 (1<<2)
#define PM_READ_COUNTER2 (1<<3)
#define PM_READ_STATUSVALUE (0<<4)
#define PM_READ_VALUE       (1<<4)
#define PM_READ_STATUS      (2<<4)

#endif // pit.h
