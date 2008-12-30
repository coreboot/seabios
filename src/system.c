// 16bit system callbacks
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "util.h" // irq_restore
#include "biosvar.h" // BIOS_CONFIG_TABLE
#include "ioport.h" // inb
#include "memmap.h" // E820_RAM
#include "pic.h" // eoi_pic2
#include "bregs.h" // struct bregs

// Use PS2 System Control port A to set A20 enable
static inline u8
set_a20(u8 cond)
{
    // get current setting first
    u8 newval, oldval = inb(PORT_A20);
    if (cond)
        newval = oldval | A20_ENABLE_BIT;
    else
        newval = oldval & ~A20_ENABLE_BIT;
    outb(newval, PORT_A20);

    return (oldval & A20_ENABLE_BIT) != 0;
}

static void
handle_152400(struct bregs *regs)
{
    set_a20(0);
    set_code_success(regs);
}

static void
handle_152401(struct bregs *regs)
{
    set_a20(1);
    set_code_success(regs);
}

static void
handle_152402(struct bregs *regs)
{
    regs->al = (inb(PORT_A20) & A20_ENABLE_BIT) != 0;
    set_code_success(regs);
}

static void
handle_152403(struct bregs *regs)
{
    regs->bx = 3;
    set_code_success(regs);
}

static void
handle_1524XX(struct bregs *regs)
{
    set_code_fail(regs, RET_EUNSUPPORTED);
}

static void
handle_1524(struct bregs *regs)
{
    switch (regs->al) {
    case 0x00: handle_152400(regs); break;
    case 0x01: handle_152401(regs); break;
    case 0x02: handle_152402(regs); break;
    case 0x03: handle_152403(regs); break;
    default:   handle_1524XX(regs); break;
    }
}

// removable media eject
static void
handle_1552(struct bregs *regs)
{
    set_code_success(regs);
}

static void
handle_1587(struct bregs *regs)
{
    // +++ should probably have descriptor checks
    // +++ should have exception handlers

    u8 prev_a20_enable = set_a20(1); // enable A20 line

    // 128K max of transfer on 386+ ???
    // source == destination ???

    // ES:SI points to descriptor table
    // offset   use     initially  comments
    // ==============================================
    // 00..07   Unused  zeros      Null descriptor
    // 08..0f   GDT     zeros      filled in by BIOS
    // 10..17   source  ssssssss   source of data
    // 18..1f   dest    dddddddd   destination of data
    // 20..27   CS      zeros      filled in by BIOS
    // 28..2f   SS      zeros      filled in by BIOS

    //es:si
    //eeee0
    //0ssss
    //-----

// check for access rights of source & dest here

    // Initialize GDT descriptor
    SET_SEG(ES, regs->es);
    u16 si = regs->si;
    u16 base15_00 = (regs->es << 4) + si;
    u16 base23_16 = regs->es >> 12;
    if (base15_00 < (u16)(regs->es<<4))
        base23_16++;
    SET_VAR(ES, *(u16*)(si+0x08+0), 47);       // limit 15:00 = 6 * 8bytes/descriptor
    SET_VAR(ES, *(u16*)(si+0x08+2), base15_00);// base 15:00
    SET_VAR(ES, *(u8 *)(si+0x08+4), base23_16);// base 23:16
    SET_VAR(ES, *(u8 *)(si+0x08+5), 0x93);     // access
    SET_VAR(ES, *(u16*)(si+0x08+6), 0x0000);   // base 31:24/reserved/limit 19:16

    // Initialize CS descriptor
    SET_VAR(ES, *(u16*)(si+0x20+0), 0xffff);// limit 15:00 = normal 64K limit
    SET_VAR(ES, *(u16*)(si+0x20+2), 0x0000);// base 15:00
    SET_VAR(ES, *(u8 *)(si+0x20+4), 0x000f);// base 23:16
    SET_VAR(ES, *(u8 *)(si+0x20+5), 0x9b);  // access
    SET_VAR(ES, *(u16*)(si+0x20+6), 0x0000);// base 31:24/reserved/limit 19:16

    // Initialize SS descriptor
    u16 ss = GET_SEG(SS);
    base15_00 = ss << 4;
    base23_16 = ss >> 12;
    SET_VAR(ES, *(u16*)(si+0x28+0), 0xffff);   // limit 15:00 = normal 64K limit
    SET_VAR(ES, *(u16*)(si+0x28+2), base15_00);// base 15:00
    SET_VAR(ES, *(u8 *)(si+0x28+4), base23_16);// base 23:16
    SET_VAR(ES, *(u8 *)(si+0x28+5), 0x93);     // access
    SET_VAR(ES, *(u16*)(si+0x28+6), 0x0000);   // base 31:24/reserved/limit 19:16

    u16 count = regs->cx;
    asm volatile(
        // Load new descriptor tables
        "lgdtw %%es:0x8(%%si)\n"
        "lidtw %%cs:pmode_IDT_info\n"

        // Enable protected mode
        "movl %%cr0, %%eax\n"
        "orl $" __stringify(CR0_PE) ", %%eax\n"
        "movl %%eax, %%cr0\n"

        // far jump to flush CPU queue after transition to protected mode
        "ljmpw $0x0020, $1f\n"
        "1:\n"

        // GDT points to valid descriptor table, now load DS, ES
        "movw $0x10, %%ax\n" // 010 000 = 2nd descriptor in table, TI=GDT, RPL=00
        "movw %%ax, %%ds\n"
        "movw $0x18, %%ax\n" // 011 000 = 3rd descriptor in table, TI=GDT, RPL=00
        "movw %%ax, %%es\n"

        // move CX words from DS:SI to ES:DI
        "xorw %%si, %%si\n"
        "xorw %%di, %%di\n"
        "rep movsw\n"

        // Disable protected mode
        "movl %%cr0, %%eax\n"
        "andl $~" __stringify(CR0_PE) ", %%eax\n"
        "movl %%eax, %%cr0\n"

        // far jump to flush CPU queue after transition to real mode
        "ljmpw $" __stringify(SEG_BIOS) ", $2f\n"
        "2:\n"

        // restore IDT to normal real-mode defaults
        "lidtw %%cs:rmode_IDT_info\n"

        // Restore %ds (from %ss)
        "movw %%ss, %%ax\n"
        "movw %%ax, %%ds\n"
        : "+c"(count), "+S"(si)
        : : "eax", "di", "cc"); // XXX - also clobbers %es

    set_a20(prev_a20_enable);

    set_code_success(regs);
}

// Get the amount of extended memory (above 1M)
static void
handle_1588(struct bregs *regs)
{
    u32 rs = GET_GLOBAL(RamSize);

    // According to Ralf Brown's interrupt the limit should be 15M,
    // but real machines mostly return max. 63M.
    if (rs > 64*1024*1024)
        regs->ax = 63 * 1024;
    else
        regs->ax = (rs - 1*1024*1024) / 1024;
    set_success(regs);
}

// Device busy interrupt.  Called by Int 16h when no key available
static void
handle_1590(struct bregs *regs)
{
}

// Interrupt complete.  Called by Int 16h when key becomes available
static void
handle_1591(struct bregs *regs)
{
}

// keyboard intercept
static void
handle_154f(struct bregs *regs)
{
    set_fail_silent(regs);
}

static void
handle_15c0(struct bregs *regs)
{
    regs->es = SEG_BIOS;
    regs->bx = (u32)&BIOS_CONFIG_TABLE;
    set_code_success(regs);
}

static void
handle_15c1(struct bregs *regs)
{
    regs->es = GET_BDA(ebda_seg);
    set_success(regs);
}

static void
handle_15e801(struct bregs *regs)
{
    // my real system sets ax and bx to 0
    // this is confirmed by Ralph Brown list
    // but syslinux v1.48 is known to behave
    // strangely if ax is set to 0
    // regs.u.r16.ax = 0;
    // regs.u.r16.bx = 0;

    u32 rs = GET_GLOBAL(RamSize);

    // Get the amount of extended memory (above 1M)
    if (rs > 16*1024*1024) {
        // limit to 15M
        regs->cx = 15*1024;
        // Get the amount of extended memory above 16M in 64k blocks
        regs->dx = (rs - 16*1024*1024) / (64*1024);
    } else {
        regs->cx = (rs - 1*1024*1024) / 1024;
        regs->dx = 0;
    }

    // Set configured memory equal to extended memory
    regs->ax = regs->cx;
    regs->bx = regs->dx;

    set_success(regs);
}

// Info on e820 map location and size.
struct e820entry *e820_list VAR16_32;
int e820_count VAR16_32;
// Amount of continuous ram under 4Gig
u32 RamSize VAR16_32;
// Amount of continuous ram >4Gig
u64 RamSizeOver4G;

static void
handle_15e820(struct bregs *regs)
{
    int count = GET_GLOBAL(e820_count);
    if (regs->edx != 0x534D4150 || regs->bx >= count) {
        set_code_fail(regs, RET_EUNSUPPORTED);
        return;
    }

    struct e820entry *l = GET_GLOBAL(e820_list);
    memcpy_far(MAKE_FARPTR(regs->es, regs->di), &l[regs->bx], sizeof(l[0]));
    if (regs->bx == count-1)
        regs->ebx = 0;
    else
        regs->ebx++;
    regs->eax = 0x534D4150;
    regs->ecx = sizeof(l[0]);
    set_success(regs);
}

static void
handle_15e8XX(struct bregs *regs)
{
    set_code_fail(regs, RET_EUNSUPPORTED);
}

static void
handle_15e8(struct bregs *regs)
{
    switch (regs->al) {
    case 0x01: handle_15e801(regs); break;
    case 0x20: handle_15e820(regs); break;
    default:   handle_15e8XX(regs); break;
    }
}

static void
handle_15XX(struct bregs *regs)
{
    set_code_fail(regs, RET_EUNSUPPORTED);
}

// INT 15h System Services Entry Point
void VISIBLE16
handle_15(struct bregs *regs)
{
    debug_enter(regs, DEBUG_HDL_15);
    switch (regs->ah) {
    case 0x24: handle_1524(regs); break;
    case 0x4f: handle_154f(regs); break;
    case 0x52: handle_1552(regs); break;
    case 0x53: handle_1553(regs); break;
    case 0x5f: handle_155f(regs); break;
    case 0x83: handle_1583(regs); break;
    case 0x86: handle_1586(regs); break;
    case 0x87: handle_1587(regs); break;
    case 0x88: handle_1588(regs); break;
    case 0x90: handle_1590(regs); break;
    case 0x91: handle_1591(regs); break;
    case 0xc0: handle_15c0(regs); break;
    case 0xc1: handle_15c1(regs); break;
    case 0xc2: handle_15c2(regs); break;
    case 0xe8: handle_15e8(regs); break;
    default:   handle_15XX(regs); break;
    }
}

// INT 12h Memory Size Service Entry Point
void VISIBLE16
handle_12(struct bregs *regs)
{
    debug_enter(regs, DEBUG_HDL_12);
    regs->ax = GET_BDA(mem_size_kb);
}

// INT 11h Equipment List Service Entry Point
void VISIBLE16
handle_11(struct bregs *regs)
{
    debug_enter(regs, DEBUG_HDL_11);
    regs->ax = GET_BDA(equipment_list_flags);
}

// INT 05h Print Screen Service Entry Point
void VISIBLE16
handle_05(struct bregs *regs)
{
    debug_enter(regs, DEBUG_HDL_05);
}

// INT 10h Video Support Service Entry Point
void VISIBLE16
handle_10(struct bregs *regs)
{
    debug_enter(regs, DEBUG_HDL_10);
    // dont do anything, since the VGA BIOS handles int10h requests
}

void VISIBLE16
handle_nmi()
{
    debug_isr(DEBUG_ISR_nmi);
    BX_PANIC("NMI Handler called\n");
}

void
mathcp_setup()
{
    dprintf(3, "math cp init\n");
    // 80x87 coprocessor installed
    SETBITS_BDA(equipment_list_flags, 0x02);
    enable_hwirq(13, entry_75);
}

// INT 75 - IRQ13 - MATH COPROCESSOR EXCEPTION
void VISIBLE16
handle_75()
{
    debug_isr(DEBUG_ISR_75);

    // clear irq13
    outb(0, PORT_MATH_CLEAR);
    // clear interrupt
    eoi_pic2();
    // legacy nmi call
    struct bregs br;
    memset(&br, 0, sizeof(br));
    call16_int(0x02, &br);
}
