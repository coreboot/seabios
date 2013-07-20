// Internal timer support.
//
// Copyright (C) 2008-2013  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // dprintf
#include "pit.h" // PM_SEL_TIMER0
#include "ioport.h" // PORT_PIT_MODE
#include "config.h" // CONFIG_*
#include "biosvar.h" // GET_LOW

// Bits for PORT_PS2_CTRLB
#define PPCB_T2GATE (1<<0)
#define PPCB_SPKR   (1<<1)
#define PPCB_T2OUT  (1<<5)


/****************************************************************
 * TSC timer
 ****************************************************************/

#define CALIBRATE_COUNT 0x800   // Approx 1.7ms

u32 cpu_khz VARFSEG;
u8 no_tsc VARFSEG;

u16 pmtimer_ioport VARFSEG;
u32 pmtimer_wraps VARLOW;
u32 pmtimer_last VARLOW;

void
timer_setup(void)
{
    u32 eax, ebx, ecx, edx, cpuid_features = 0;

    if (CONFIG_PMTIMER && GET_GLOBAL(pmtimer_ioport)) {
        dprintf(3, "pmtimer already configured; will not calibrate TSC\n");
        return;
    }

    cpuid(0, &eax, &ebx, &ecx, &edx);
    if (eax > 0)
        cpuid(1, &eax, &ebx, &ecx, &cpuid_features);

    if (!(cpuid_features & CPUID_TSC)) {
        SET_GLOBAL(no_tsc, 1);
        SET_GLOBAL(cpu_khz, PIT_TICK_RATE / 1000);
        dprintf(3, "386/486 class CPU. Using TSC emulation\n");
        return;
    }

    // Setup "timer2"
    u8 orig = inb(PORT_PS2_CTRLB);
    outb((orig & ~PPCB_SPKR) | PPCB_T2GATE, PORT_PS2_CTRLB);
    /* binary, mode 0, LSB/MSB, Ch 2 */
    outb(PM_SEL_TIMER2|PM_ACCESS_WORD|PM_MODE0|PM_CNT_BINARY, PORT_PIT_MODE);
    /* LSB of ticks */
    outb(CALIBRATE_COUNT & 0xFF, PORT_PIT_COUNTER2);
    /* MSB of ticks */
    outb(CALIBRATE_COUNT >> 8, PORT_PIT_COUNTER2);

    u64 start = rdtscll();
    while ((inb(PORT_PS2_CTRLB) & PPCB_T2OUT) == 0)
        ;
    u64 end = rdtscll();

    // Restore PORT_PS2_CTRLB
    outb(orig, PORT_PS2_CTRLB);

    // Store calibrated cpu khz.
    u64 diff = end - start;
    dprintf(6, "tsc calibrate start=%u end=%u diff=%u\n"
            , (u32)start, (u32)end, (u32)diff);
    u32 hz = diff * PIT_TICK_RATE / CALIBRATE_COUNT;
    SET_GLOBAL(cpu_khz, hz / 1000);

    dprintf(1, "CPU Mhz=%u\n", hz / 1000000);
}

/* TSC emulation timekeepers */
u64 TSC_8254 VARLOW;
int Last_TSC_8254 VARLOW;

static u64
emulate_tsc(void)
{
    /* read timer 0 current count */
    u64 ret = GET_LOW(TSC_8254);
    /* readback mode has slightly shifted registers, works on all
     * 8254, readback PIT0 latch */
    outb(PM_SEL_READBACK | PM_READ_VALUE | PM_READ_COUNTER0, PORT_PIT_MODE);
    int cnt = (inb(PORT_PIT_COUNTER0) | (inb(PORT_PIT_COUNTER0) << 8));
    int d = GET_LOW(Last_TSC_8254) - cnt;
    /* Determine the ticks count from last invocation of this function */
    ret += (d > 0) ? d : (PIT_TICK_INTERVAL + d);
    SET_LOW(Last_TSC_8254, cnt);
    SET_LOW(TSC_8254, ret);
    return ret;
}

void pmtimer_setup(u16 ioport, u32 khz)
{
    if (!CONFIG_PMTIMER)
        return;
    dprintf(1, "Using pmtimer, ioport 0x%x, freq %d kHz\n", ioport, khz);
    SET_GLOBAL(pmtimer_ioport, ioport);
    SET_GLOBAL(cpu_khz, khz);
}

static u64 pmtimer_get(void)
{
    u16 ioport = GET_GLOBAL(pmtimer_ioport);
    u32 wraps = GET_LOW(pmtimer_wraps);
    u32 pmtimer = inl(ioport) & 0xffffff;

    if (pmtimer < GET_LOW(pmtimer_last)) {
        wraps++;
        SET_LOW(pmtimer_wraps, wraps);
    }
    SET_LOW(pmtimer_last, pmtimer);

    dprintf(9, "pmtimer: %u:%u\n", wraps, pmtimer);
    return (u64)wraps << 24 | pmtimer;
}

static u64
get_tsc(void)
{
    if (unlikely(GET_GLOBAL(no_tsc)))
        return emulate_tsc();
    if (CONFIG_PMTIMER && GET_GLOBAL(pmtimer_ioport))
        return pmtimer_get();
    return rdtscll();
}

int
check_tsc(u64 end)
{
    return (s64)(get_tsc() - end) > 0;
}

static void
tscdelay(u64 diff)
{
    u64 start = get_tsc();
    u64 end = start + diff;
    while (!check_tsc(end))
        cpu_relax();
}

static void
tscsleep(u64 diff)
{
    u64 start = get_tsc();
    u64 end = start + diff;
    while (!check_tsc(end))
        yield();
}

void ndelay(u32 count) {
    tscdelay(count * GET_GLOBAL(cpu_khz) / 1000000);
}
void udelay(u32 count) {
    tscdelay(count * GET_GLOBAL(cpu_khz) / 1000);
}
void mdelay(u32 count) {
    tscdelay(count * GET_GLOBAL(cpu_khz));
}

void nsleep(u32 count) {
    tscsleep(count * GET_GLOBAL(cpu_khz) / 1000000);
}
void usleep(u32 count) {
    tscsleep(count * GET_GLOBAL(cpu_khz) / 1000);
}
void msleep(u32 count) {
    tscsleep(count * GET_GLOBAL(cpu_khz));
}

// Return the TSC value that is 'msecs' time in the future.
u64
calc_future_tsc(u32 msecs)
{
    u32 khz = GET_GLOBAL(cpu_khz);
    return get_tsc() + ((u64)khz * msecs);
}
u64
calc_future_tsc_usec(u32 usecs)
{
    u32 khz = GET_GLOBAL(cpu_khz);
    return get_tsc() + ((u64)(khz/1000) * usecs);
}


/****************************************************************
 * IRQ based timer
 ****************************************************************/

// Calculate the timer value at 'count' number of full timer ticks in
// the future.
u32
calc_future_timer_ticks(u32 count)
{
    return (GET_BDA(timer_counter) + count + 1) % TICKS_PER_DAY;
}

// Return the timer value that is 'msecs' time in the future.
u32
calc_future_timer(u32 msecs)
{
    if (!msecs)
        return GET_BDA(timer_counter);
    u32 kticks = DIV_ROUND_UP((u64)msecs * PIT_TICK_RATE, PIT_TICK_INTERVAL);
    u32 ticks = DIV_ROUND_UP(kticks, 1000);
    return calc_future_timer_ticks(ticks);
}

// Check if the given timer value has passed.
int
check_timer(u32 end)
{
    return (((GET_BDA(timer_counter) + TICKS_PER_DAY - end) % TICKS_PER_DAY)
            < (TICKS_PER_DAY/2));
}
