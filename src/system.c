// 16bit system callbacks
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "util.h" // irq_restore
#include "biosvar.h" // CONFIG_BIOS_TABLE
#include "ioport.h" // inb
#include "cmos.h" // inb_cmos

#define E820_RAM          1
#define E820_RESERVED     2
#define E820_ACPI         3
#define E820_NVS          4
#define E820_UNUSABLE     5

// Use PS2 System Control port A to set A20 enable
static inline u8
set_a20(u8 cond)
{
    // get current setting first
    u8 newval, oldval = inb(PORT_A20);
    if (cond)
        newval = oldval | 0x02;
    else
        newval = oldval & ~0x02;
    outb(newval, PORT_A20);

    return (newval & 0x02) != 0;
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
    regs->al = !!(inb(PORT_A20) & 0x20);
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

// Wait for CX:DX microseconds. currently using the
// refresh request port 0x61 bit4, toggling every 15usec
static void
handle_1586(struct bregs *regs)
{
    irq_enable();
    usleep((regs->cx << 16) | regs->dx);
    irq_disable();
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

        // set PE bit in CR0
        "movl %%cr0, %%eax\n"
        "orb $0x01, %%al\n"
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

        // reset PG bit in CR0 ???
        "movl %%cr0, %%eax\n"
        "andb $0xfe, %%al\n"
        "movl %%eax, %%cr0\n"

        // far jump to flush CPU queue after transition to real mode
        "ljmpw $0xf000, $2f\n"
        "2:\n"

        // restore IDT to normal real-mode defaults
        "lidt %%cs:rmode_IDT_info\n"

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
    regs->al = inb_cmos(CMOS_MEM_EXTMEM_LOW);
    regs->ah = inb_cmos(CMOS_MEM_EXTMEM_HIGH);
    // According to Ralf Brown's interrupt the limit should be 15M,
    // but real machines mostly return max. 63M.
    if (regs->ax > 0xffc0)
        regs->ax = 0xffc0;
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
    // set_fail(regs);  -- don't report this failure.
    set_cf(regs, 1);
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

    // Get the amount of extended memory (above 1M)
    regs->cl = inb_cmos(CMOS_MEM_EXTMEM_LOW);
    regs->ch = inb_cmos(CMOS_MEM_EXTMEM_HIGH);

    // limit to 15M
    if (regs->cx > 0x3c00)
        regs->cx = 0x3c00;

    // Get the amount of extended memory above 16M in 64k blocs
    regs->dl = inb_cmos(CMOS_MEM_EXTMEM2_LOW);
    regs->dh = inb_cmos(CMOS_MEM_EXTMEM2_HIGH);

    // Set configured memory equal to extended memory
    regs->ax = regs->cx;
    regs->bx = regs->dx;

    set_success(regs);
}

static void
set_e820_range(struct bregs *regs, u32 start, u32 end, u16 type, int last)
{
    SET_FARVAR(regs->es, *(u16*)(regs->di+0), start);
    SET_FARVAR(regs->es, *(u16*)(regs->di+2), start >> 16);
    SET_FARVAR(regs->es, *(u16*)(regs->di+4), 0x00);
    SET_FARVAR(regs->es, *(u16*)(regs->di+6), 0x00);

    end -= start;
    SET_FARVAR(regs->es, *(u16*)(regs->di+8), end);
    SET_FARVAR(regs->es, *(u16*)(regs->di+10), end >> 16);
    SET_FARVAR(regs->es, *(u16*)(regs->di+12), 0x0000);
    SET_FARVAR(regs->es, *(u16*)(regs->di+14), 0x0000);

    SET_FARVAR(regs->es, *(u16*)(regs->di+16), type);
    SET_FARVAR(regs->es, *(u16*)(regs->di+18), 0x0);

    if (last)
        regs->ebx = 0;
    else
        regs->ebx++;
    regs->eax = 0x534D4150;
    regs->ecx = 0x14;
    set_success(regs);
}

// XXX - should create e820 memory map in post and just copy it here.
static void
handle_15e820(struct bregs *regs)
{
    if (regs->edx != 0x534D4150) {
        set_code_fail(regs, RET_EUNSUPPORTED);
        return;
    }

    u32 extended_memory_size = inb_cmos(CMOS_MEM_EXTMEM2_HIGH);
    extended_memory_size <<= 8;
    extended_memory_size |= inb_cmos(CMOS_MEM_EXTMEM2_LOW);
    extended_memory_size *= 64;
    // greater than EFF00000???
    if (extended_memory_size > 0x3bc000)
        // everything after this is reserved memory until we get to 0x100000000
        extended_memory_size = 0x3bc000;
    extended_memory_size *= 1024;
    extended_memory_size += (16L * 1024 * 1024);

    if (extended_memory_size <= (16L * 1024 * 1024)) {
        extended_memory_size = inb_cmos(CMOS_MEM_EXTMEM_HIGH);
        extended_memory_size <<= 8;
        extended_memory_size |= inb_cmos(CMOS_MEM_EXTMEM_LOW);
        extended_memory_size *= 1024;
        extended_memory_size += 1 * 1024 * 1024;
    }

    switch (regs->bx) {
    case 0:
        set_e820_range(regs, 0x0000000L, 0x0009fc00L, E820_RAM, 0);
        break;
    case 1:
        set_e820_range(regs, 0x0009fc00L, 0x000a0000L, E820_RESERVED, 0);
        break;
    case 2:
        set_e820_range(regs, 0x000e8000L, 0x00100000L, E820_RESERVED, 0);
        break;
    case 3:
        set_e820_range(regs, 0x00100000L
                       , extended_memory_size - CONFIG_ACPI_DATA_SIZE
                       , E820_RAM, 0);
        break;
    case 4:
        set_e820_range(regs,
                       extended_memory_size - CONFIG_ACPI_DATA_SIZE,
                       extended_memory_size, E820_ACPI, 0);
        break;
    case 5:
        /* 256KB BIOS area at the end of 4 GB */
        set_e820_range(regs, 0xfffc0000L, 0x00000000L, E820_RESERVED, 1);
        break;
    default:  /* AX=E820, DX=534D4150, BX unrecognized */
        set_code_fail(regs, RET_EUNSUPPORTED);
    }
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
    //debug_enter(regs);
    switch (regs->ah) {
    case 0x24: handle_1524(regs); break;
    case 0x4f: handle_154f(regs); break;
    case 0x52: handle_1552(regs); break;
    case 0x53: handle_1553(regs); break;
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
    debug_enter(regs);
    regs->ax = GET_BDA(mem_size_kb);
}

// INT 11h Equipment List Service Entry Point
void VISIBLE16
handle_11(struct bregs *regs)
{
    debug_enter(regs);
    regs->ax = GET_BDA(equipment_list_flags);
}

// INT 05h Print Screen Service Entry Point
void VISIBLE16
handle_05(struct bregs *regs)
{
    debug_enter(regs);
}

// INT 10h Video Support Service Entry Point
void VISIBLE16
handle_10(struct bregs *regs)
{
    debug_enter(regs);
    // dont do anything, since the VGA BIOS handles int10h requests
}

void VISIBLE16
handle_nmi()
{
    debug_isr();
    BX_PANIC("NMI Handler called\n");
}

// INT 75 - IRQ13 - MATH COPROCESSOR EXCEPTION
void VISIBLE16
handle_75()
{
    debug_isr();

    // clear irq13
    outb(0, PORT_MATH_CLEAR);
    // clear interrupt
    eoi_both_pics();
    // legacy nmi call
    struct bregs br;
    memset(&br, 0, sizeof(br));
    call16_int(0x02, &br);
}
