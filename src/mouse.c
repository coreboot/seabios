// 16bit code to handle mouse events.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "biosvar.h" // struct bregs
#include "util.h" // debug_enter

#define DEBUGF1(fmt, args...) bprintf(0, fmt , ##args)
#define DEBUGF(fmt, args...)

static char panic_msg_keyb_buffer_full[] = "%s: keyboard input buffer full\n";

static void
set_kbd_command_byte(u8 command_byte)
{
    if (inb(PORT_PS2_STATUS) & 0x02)
        BX_PANIC(panic_msg_keyb_buffer_full, "setkbdcomm");
    outb(0xD4, PORT_PS2_STATUS);

    outb(0x60, PORT_PS2_STATUS); // write command byte
    outb(command_byte, PORT_PS2_DATA);
}

static u8
inhibit_mouse_int_and_events()
{
    // Turn off IRQ generation and aux data line
    if (inb(PORT_PS2_STATUS) & 0x02)
        BX_PANIC(panic_msg_keyb_buffer_full,"inhibmouse");
    outb(0x20, PORT_PS2_STATUS); // get command byte
    while ((inb(PORT_PS2_STATUS) & 0x01) != 0x01)
        ;
    u8 prev_command_byte = inb(PORT_PS2_DATA);
    u8 command_byte = prev_command_byte;
    //while ( (inb(PORT_PS2_STATUS) & 0x02) );
    if ( inb(PORT_PS2_STATUS) & 0x02 )
        BX_PANIC(panic_msg_keyb_buffer_full,"inhibmouse");
    command_byte &= 0xfd; // turn off IRQ 12 generation
    command_byte |= 0x20; // disable mouse serial clock line
    outb(0x60, PORT_PS2_STATUS); // write command byte
    outb(command_byte, PORT_PS2_DATA);
    return prev_command_byte;
}

static void
enable_mouse_int_and_events()
{
    // Turn on IRQ generation and aux data line
    if ( inb(PORT_PS2_STATUS) & 0x02 )
        BX_PANIC(panic_msg_keyb_buffer_full,"enabmouse");
    outb(0x20, PORT_PS2_STATUS); // get command byte
    while ((inb(PORT_PS2_STATUS) & 0x01) != 0x01)
        ;
    u8 command_byte = inb(PORT_PS2_DATA);
    //while ( (inb(PORT_PS2_STATUS) & 0x02) );
    if (inb(PORT_PS2_STATUS) & 0x02)
        BX_PANIC(panic_msg_keyb_buffer_full,"enabmouse");
    command_byte |= 0x02; // turn on IRQ 12 generation
    command_byte &= 0xdf; // enable mouse serial clock line
    outb(0x60, PORT_PS2_STATUS); // write command byte
    outb(command_byte, PORT_PS2_DATA);
}

static void
send_to_mouse_ctrl(u8 sendbyte)
{
    // wait for chance to write to ctrl
    if (inb(PORT_PS2_STATUS) & 0x02)
        BX_PANIC(panic_msg_keyb_buffer_full,"sendmouse");
    outb(0xD4, PORT_PS2_STATUS);
    outb(sendbyte, PORT_PS2_DATA);
}

static void
get_mouse_data(u8 *data)
{
    while ((inb(PORT_PS2_STATUS) & 0x21) != 0x21)
        ;
    *data = inb(PORT_PS2_DATA);
}

#define RET_SUCCESS      0x00
#define RET_EINVFUNCTION 0x01
#define RET_EINVINPUT    0x02
#define RET_EINTERFACE   0x03
#define RET_ENEEDRESEND  0x04
#define RET_ENOHANDLER   0x05

// Disable Mouse
static void
mouse_15c20000(struct bregs *regs)
{
    inhibit_mouse_int_and_events(); // disable IRQ12 and packets
    send_to_mouse_ctrl(0xF5); // disable mouse command
    u8 mouse_data1;
    get_mouse_data(&mouse_data1);
    set_code_success(regs);
}

#define BX_DEBUG_INT15(args...)

// Enable Mouse
static void
mouse_15c20001(struct bregs *regs)
{
    u8 mouse_flags_2 = GET_EBDA(mouse_flag2);
    if ((mouse_flags_2 & 0x80) == 0) {
        BX_DEBUG_INT15("INT 15h C2 Enable Mouse, no far call handler\n");
        set_code_fail(regs, RET_ENOHANDLER);
        return;
    }
    inhibit_mouse_int_and_events(); // disable IRQ12 and packets
    send_to_mouse_ctrl(0xF4); // enable mouse command
    u8 mouse_data1;
    get_mouse_data(&mouse_data1);
    if (mouse_data1 == 0xFA) {
        enable_mouse_int_and_events(); // turn IRQ12 and packet generation on
        set_code_success(regs);
        return;
    }
    set_code_fail(regs, RET_ENEEDRESEND);
}

static void
mouse_15c200XX(struct bregs *regs)
{
    set_code_fail(regs, RET_EINVFUNCTION);
}

// Disable/Enable Mouse
static void
mouse_15c200(struct bregs *regs)
{
    switch (regs->bh) {
    case 0x00: mouse_15c20000(regs); break;
    case 0x01: mouse_15c20001(regs); break;
    default:   mouse_15c200XX(regs); break;
    }
}

// Reset Mouse
static void
mouse_15c201(struct bregs *regs)
{
    inhibit_mouse_int_and_events(); // disable IRQ12 and packets
    send_to_mouse_ctrl(0xFF); // reset mouse command
    u8 mouse_data1, mouse_data2, mouse_data3;
    get_mouse_data(&mouse_data3);
    // if no mouse attached, it will return RESEND
    if (mouse_data3 == 0xfe) {
        set_code_fail(regs, RET_ENEEDRESEND);
        return;
    }
    if (mouse_data3 != 0xfa)
        BX_PANIC("Mouse reset returned %02x (should be ack)\n"
                 , (unsigned)mouse_data3);
    get_mouse_data(&mouse_data1);
    get_mouse_data(&mouse_data2);
    // turn IRQ12 and packet generation on
    enable_mouse_int_and_events();
    regs->bl = mouse_data1;
    regs->bh = mouse_data2;
    set_code_success(regs);
}

// Set Sample Rate
static void
mouse_15c202(struct bregs *regs)
{
    if (regs->bh >= 7) {
        set_code_fail(regs, RET_EUNSUPPORTED);
        return;
    }
    u8 mouse_data1 = regs->bh * 20;
    if (!mouse_data1)
        mouse_data1 = 10;
    send_to_mouse_ctrl(0xF3); // set sample rate command
    u8 mouse_data2;
    get_mouse_data(&mouse_data2);
    send_to_mouse_ctrl(mouse_data1);
    get_mouse_data(&mouse_data2);
    set_code_success(regs);
}

// Set Resolution
static void
mouse_15c203(struct bregs *regs)
{
    // BH:
    //      0 =  25 dpi, 1 count  per millimeter
    //      1 =  50 dpi, 2 counts per millimeter
    //      2 = 100 dpi, 4 counts per millimeter
    //      3 = 200 dpi, 8 counts per millimeter
    u8 comm_byte = inhibit_mouse_int_and_events(); // disable IRQ12 and packets
    if (regs->bh >= 4) {
        set_code_fail(regs, RET_EUNSUPPORTED);
        goto done;
    }
    send_to_mouse_ctrl(0xE8); // set resolution command
    u8 mouse_data1;
    get_mouse_data(&mouse_data1);
    if (mouse_data1 != 0xfa)
        BX_PANIC("Mouse status returned %02x (should be ack)\n"
                 , (unsigned)mouse_data1);
    send_to_mouse_ctrl(regs->bh);
    get_mouse_data(&mouse_data1);
    if (mouse_data1 != 0xfa)
        BX_PANIC("Mouse status returned %02x (should be ack)\n"
                 , (unsigned)mouse_data1);
    set_code_success(regs);

done:
    set_kbd_command_byte(comm_byte); // restore IRQ12 and serial enable
}

// Get Device ID
static void
mouse_15c204(struct bregs *regs)
{
    inhibit_mouse_int_and_events(); // disable IRQ12 and packets
    send_to_mouse_ctrl(0xF2); // get mouse ID command
    u8 mouse_data1, mouse_data2;
    get_mouse_data(&mouse_data1);
    get_mouse_data(&mouse_data2);
    regs->bh = mouse_data2;
    set_code_success(regs);
}

// Initialize Mouse
static void
mouse_15c205(struct bregs *regs)
{
    if (regs->bh != 3) {
        set_code_fail(regs, RET_EINTERFACE);
        return;
    }
    SET_EBDA(mouse_flag1, 0x00);
    SET_EBDA(mouse_flag2, regs->bh);

    // Reset Mouse
    mouse_15c201(regs);
}

// Return Status
static void
mouse_15c20600(struct bregs *regs)
{
    u8 comm_byte = inhibit_mouse_int_and_events(); // disable IRQ12 and packets
    send_to_mouse_ctrl(0xE9); // get mouse info command
    u8 mouse_data1, mouse_data2, mouse_data3;
    get_mouse_data(&mouse_data1);
    if (mouse_data1 != 0xfa)
        BX_PANIC("Mouse status returned %02x (should be ack)\n"
                 , (unsigned)mouse_data1);
    get_mouse_data(&mouse_data1);
    get_mouse_data(&mouse_data2);
    get_mouse_data(&mouse_data3);
    regs->bl = mouse_data1;
    regs->cl = mouse_data2;
    regs->dl = mouse_data3;
    set_code_success(regs);
    set_kbd_command_byte(comm_byte); // restore IRQ12 and serial enable
}

static void
set_scaling(struct bregs *regs, u8 cmd)
{
    u8 comm_byte = inhibit_mouse_int_and_events(); // disable IRQ12 and packets
    send_to_mouse_ctrl(0xE6);
    u8 mouse_data1;
    get_mouse_data(&mouse_data1);
    if (mouse_data1 != 0xFA)
        set_code_fail(regs, RET_EUNSUPPORTED);
    else
        set_code_success(regs);
    set_kbd_command_byte(comm_byte); // restore IRQ12 and serial enable
}

// Set Scaling Factor to 1:1
static void
mouse_15c20601(struct bregs *regs)
{
    set_scaling(regs, 0xE6);
}

// Set Scaling Factor to 2:1
static void
mouse_15c20602(struct bregs *regs)
{
    set_scaling(regs, 0xE7);
}

static void
mouse_15c206XX(struct bregs *regs)
{
    BX_PANIC("INT 15h C2 AL=6, BH=%02x\n", regs->bh);
}

// Return Status & Set Scaling Factor...
static void
mouse_15c206(struct bregs *regs)
{
    switch (regs->bh) {
    case 0x00: mouse_15c20600(regs); break;
    case 0x01: mouse_15c20601(regs); break;
    case 0x02: mouse_15c20602(regs); break;
    default:   mouse_15c206XX(regs); break;
    }
}

// Set Mouse Handler Address
static void
mouse_15c207(struct bregs *regs)
{
    u32 farptr = (regs->es << 16) | regs->bx;
    SET_EBDA(far_call_pointer, farptr);
    u8 mouse_flags_2 = GET_EBDA(mouse_flag2);
    if (! farptr) {
        /* remove handler */
        if ((mouse_flags_2 & 0x80) != 0) {
            mouse_flags_2 &= ~0x80;
            inhibit_mouse_int_and_events(); // disable IRQ12 and packets
        }
    } else {
        /* install handler */
        mouse_flags_2 |= 0x80;
    }
    SET_EBDA(mouse_flag2, mouse_flags_2);
    set_code_success(regs);
}

static void
mouse_15c2XX(struct bregs *regs)
{
    set_code_fail(regs, RET_EINVFUNCTION);
}

void
handle_15c2(struct bregs *regs)
{
    //debug_stub(regs);

    if (! CONFIG_PS2_MOUSE) {
        set_code_fail(regs, RET_EUNSUPPORTED);
        return;
    }

    switch (regs->al) {
    case 0x00: mouse_15c200(regs); break;
    case 0x01: mouse_15c201(regs); break;
    case 0x02: mouse_15c202(regs); break;
    case 0x03: mouse_15c203(regs); break;
    case 0x04: mouse_15c204(regs); break;
    case 0x05: mouse_15c205(regs); break;
    case 0x06: mouse_15c206(regs); break;
    case 0x07: mouse_15c207(regs); break;
    default:   mouse_15c2XX(regs); break;
    }
}

static void
int74_function()
{
    u8 v = inb(PORT_PS2_STATUS);
    if ((v & 0x21) != 0x21)
        return;

    v = inb(PORT_PS2_DATA);

    u8 mouse_flags_1 = GET_EBDA(mouse_flag1);
    u8 mouse_flags_2 = GET_EBDA(mouse_flag2);

    if ((mouse_flags_2 & 0x80) != 0x80)
        return;

    u8 package_count = mouse_flags_2 & 0x07;
    u8 index = mouse_flags_1 & 0x07;
    SET_EBDA(mouse_data[index], v);

    if ((index+1) < package_count) {
        mouse_flags_1++;
        SET_EBDA(mouse_flag1, mouse_flags_1);
        return;
    }

    //BX_DEBUG_INT74("int74_function: make_farcall=1\n");
    u16 status = GET_EBDA(mouse_data[0]);
    u16 X      = GET_EBDA(mouse_data[1]);
    u16 Y      = GET_EBDA(mouse_data[2]);
    SET_EBDA(mouse_flag1, 0);
    // check if far call handler installed
    if (! (mouse_flags_2 & 0x80))
        return;

    u32 func = GET_EBDA(far_call_pointer);
    asm volatile(
        "pushl %0\n"
        "pushw %w1\n"  // status
        "pushw %w2\n"  // X
        "pushw %w3\n"  // Y
        "pushw $0\n"   // Z
        "lcallw *8(%%esp)\n"
        "addl $12, %%esp\n"
        "cld\n"
        : "+a" (func), "+b" (status), "+c" (X), "+d" (Y)
        :
        : "esi", "edi", "ebp", "cc"
        );
}

// INT74h : PS/2 mouse hardware interrupt
void VISIBLE16
handle_74()
{
    //debug_isr();

    irq_enable();
    int74_function();
    irq_disable();

    eoi_both_pics();
}
