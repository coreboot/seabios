// 16bit code to handle keyboard requests.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "biosvar.h" // struct bregs
#include "util.h" // debug_enter
#include "config.h" // CONFIG_*

//--------------------------------------------------------------------------
// keyboard_panic
//--------------------------------------------------------------------------
static void
keyboard_panic(u16 status)
{
    // If you're getting a 993 keyboard panic here,
    // please see the comment in keyboard_init

    BX_PANIC("Keyboard error:%u\n",status);
}

static void
kbd_flush(u8 code)
{
    u16 max = 0xffff;
    while ((inb(PORT_PS2_STATUS) & 0x02) && (--max > 0))
        outb(code, PORT_DIAG);
    if (!max && code != 0xff)
        keyboard_panic(code);
}

static void
kbd_waitdata(u8 code)
{
    u16 max = 0xffff;
    while ( ((inb(PORT_PS2_STATUS) & 0x01) == 0) && (--max>0) )
        outb(code, PORT_DIAG);
    if (!max)
        keyboard_panic(code);
}

//--------------------------------------------------------------------------
// keyboard_init
//--------------------------------------------------------------------------
// this file is based on LinuxBIOS implementation of keyboard.c
static void
keyboard_init()
{
    /* ------------------- Flush buffers ------------------------*/
    /* Wait until buffer is empty */
    kbd_flush(0xff);

    /* flush incoming keys */
    u16 max=0x2000;
    while (--max > 0) {
        outb(0x00, PORT_DIAG);
        if (inb(PORT_PS2_STATUS) & 0x01) {
            inb(PORT_PS2_DATA);
            max = 0x2000;
        }
    }

    // Due to timer issues, and if the IPS setting is > 15000000,
    // the incoming keys might not be flushed here. That will
    // cause a panic a few lines below.  See sourceforge bug report :
    // [ 642031 ] FATAL: Keyboard RESET error:993

    /* ------------------- controller side ----------------------*/
    /* send cmd = 0xAA, self test 8042 */
    outb(0xaa, PORT_PS2_STATUS);

    kbd_flush(0x00);
    kbd_waitdata(0x01);

    /* read self-test result, 0x55 should be returned from 0x60 */
    if (inb(PORT_PS2_DATA) != 0x55)
        keyboard_panic(991);

    /* send cmd = 0xAB, keyboard interface test */
    outb(0xab, PORT_PS2_STATUS);

    kbd_flush(0x10);
    kbd_waitdata(0x11);

    /* read keyboard interface test result, */
    /* 0x00 should be returned form 0x60 */
    if (inb(PORT_PS2_DATA) != 0x00)
        keyboard_panic(992);

    /* Enable Keyboard clock */
    outb(0xae, PORT_PS2_STATUS);
    outb(0xa8, PORT_PS2_STATUS);

    /* ------------------- keyboard side ------------------------*/
    /* reset kerboard and self test  (keyboard side) */
    outb(0xff, PORT_PS2_DATA);

    kbd_flush(0x20);
    kbd_waitdata(0x21);

    /* keyboard should return ACK */
    if (inb(PORT_PS2_DATA) != 0xfa)
        keyboard_panic(993);

    kbd_waitdata(0x31);

    if (inb(PORT_PS2_DATA) != 0xaa)
        keyboard_panic(994);

    /* Disable keyboard */
    outb(0xf5, PORT_PS2_DATA);

    kbd_flush(0x40);
    kbd_waitdata(0x41);

    /* keyboard should return ACK */
    if (inb(PORT_PS2_DATA) != 0xfa)
        keyboard_panic(995);

    /* Write Keyboard Mode */
    outb(0x60, PORT_PS2_STATUS);

    kbd_flush(0x50);

    /* send cmd: scan code convert, disable mouse, enable IRQ 1 */
    outb(0x61, PORT_PS2_DATA);

    kbd_flush(0x60);

    /* Enable keyboard */
    outb(0xf4, PORT_PS2_DATA);

    kbd_flush(0x70);
    kbd_waitdata(0x71);

    /* keyboard should return ACK */
    if (inb(PORT_PS2_DATA) != 0xfa)
        keyboard_panic(996);

    outb(0x77, PORT_DIAG);
}

void
kbd_setup()
{
    u16 x = offsetof(struct bios_data_area_s, kbd_buf) - 0x400;
    SET_BDA(kbd_mode, 0x10);
    SET_BDA(kbd_buf_head, x);
    SET_BDA(kbd_buf_tail, x);
    SET_BDA(kbd_buf_start_offset, x);

    SET_BDA(kbd_buf_end_offset
            , x + FIELD_SIZEOF(struct bios_data_area_s, kbd_buf));

    keyboard_init();
}

static u8
enqueue_key(u8 scan_code, u8 ascii_code)
{
    u16 buffer_start = GET_BDA(kbd_buf_start_offset);
    u16 buffer_end   = GET_BDA(kbd_buf_end_offset);

    u16 buffer_head = GET_BDA(kbd_buf_head);
    u16 buffer_tail = GET_BDA(kbd_buf_tail);

    u16 temp_tail = buffer_tail;
    buffer_tail += 2;
    if (buffer_tail >= buffer_end)
        buffer_tail = buffer_start;

    if (buffer_tail == buffer_head)
        return 0;

    SET_FARVAR(SEG_BDA, *(u8*)(temp_tail+0x400+0), ascii_code);
    SET_FARVAR(SEG_BDA, *(u8*)(temp_tail+0x400+1), scan_code);
    SET_BDA(kbd_buf_tail, buffer_tail);
    return 1;
}

static u8
dequeue_key(u8 *scan_code, u8 *ascii_code, u8 incr)
{
    u16 buffer_head;
    u16 buffer_tail;
    for (;;) {
        buffer_head = GET_BDA(kbd_buf_head);
        buffer_tail = GET_BDA(kbd_buf_tail);

        if (buffer_head != buffer_tail)
            break;
        if (!incr)
            return 0;
        cpu_relax();
    }

    *ascii_code = GET_FARVAR(SEG_BDA, *(u8*)(buffer_head+0x400+0));
    *scan_code  = GET_FARVAR(SEG_BDA, *(u8*)(buffer_head+0x400+1));

    if (incr) {
        u16 buffer_start = GET_BDA(kbd_buf_start_offset);
        u16 buffer_end   = GET_BDA(kbd_buf_end_offset);

        buffer_head += 2;
        if (buffer_head >= buffer_end)
            buffer_head = buffer_start;
        SET_BDA(kbd_buf_head, buffer_head);
    }
    return 1;
}

// read keyboard input
static void
handle_1600(struct bregs *regs)
{
    u8 scan_code, ascii_code;
    dequeue_key(&scan_code, &ascii_code, 1);
    if (scan_code != 0 && ascii_code == 0xF0)
        ascii_code = 0;
    else if (ascii_code == 0xE0)
        ascii_code = 0;
    regs->ax = (scan_code << 8) | ascii_code;
}

// check keyboard status
static void
handle_1601(struct bregs *regs)
{
    u8 scan_code, ascii_code;
    if (!dequeue_key(&scan_code, &ascii_code, 0)) {
        regs->flags |= F_ZF;
        return;
    }
    if (scan_code != 0 && ascii_code == 0xF0)
        ascii_code = 0;
    else if (ascii_code == 0xE0)
        ascii_code = 0;
    regs->ax = (scan_code << 8) | ascii_code;
    regs->flags &= ~F_ZF;
}

// get shift flag status
static void
handle_1602(struct bregs *regs)
{
    regs->al = GET_BDA(kbd_flag0);
}

// store key-stroke into buffer
static void
handle_1605(struct bregs *regs)
{
    regs->al = !enqueue_key(regs->ch, regs->cl);
}

// GET KEYBOARD FUNCTIONALITY
static void
handle_1609(struct bregs *regs)
{
    // bit Bochs Description
    //  7    0   reserved
    //  6    0   INT 16/AH=20h-22h supported (122-key keyboard support)
    //  5    1   INT 16/AH=10h-12h supported (enhanced keyboard support)
    //  4    1   INT 16/AH=0Ah supported
    //  3    0   INT 16/AX=0306h supported
    //  2    0   INT 16/AX=0305h supported
    //  1    0   INT 16/AX=0304h supported
    //  0    0   INT 16/AX=0300h supported
    //
    regs->al = 0x30;
}

// GET KEYBOARD ID
static void
handle_160a(struct bregs *regs)
{
    outb(0xf2, PORT_PS2_DATA);
    /* Wait for data */
    u16 max=0xffff;
    while ( ((inb(PORT_PS2_STATUS) & 0x01) == 0) && (--max>0) )
        outb(0x00, PORT_DIAG);
    if (!max)
        return;
    if (inb(PORT_PS2_DATA) != 0xfa) {
        regs->bx = 0;
        return;
    }
    u16 kbd_code = 0;
    u8 count = 2;
    do {
        max=0xffff;
        while ( ((inb(PORT_PS2_STATUS) & 0x01) == 0) && (--max>0) )
            outb(0x00, PORT_DIAG);
        if (max>0x0) {
            kbd_code >>= 8;
            kbd_code |= (inb(PORT_PS2_DATA) << 8);
        }
    } while (--count>0);
    regs->bx = kbd_code;
}

// read MF-II keyboard input
static void
handle_1610(struct bregs *regs)
{
    u8 scan_code, ascii_code;
    dequeue_key(&scan_code, &ascii_code, 1);
    if (scan_code != 0 && ascii_code == 0xF0)
        ascii_code = 0;
    regs->ax = (scan_code << 8) | ascii_code;
}

// check MF-II keyboard status
static void
handle_1611(struct bregs *regs)
{
    u8 scan_code, ascii_code;
    if (!dequeue_key(&scan_code, &ascii_code, 0)) {
        regs->flags |= F_ZF;
        return;
    }
    if (scan_code != 0 && ascii_code == 0xF0)
        ascii_code = 0;
    regs->ax = (scan_code << 8) | ascii_code;
    regs->flags &= ~F_ZF;
}

// get extended keyboard status
static void
handle_1612(struct bregs *regs)
{
    regs->al = GET_BDA(kbd_flag0);
    regs->ah = (GET_BDA(kbd_flag1) & 0x73) | (GET_BDA(kbd_mode) & 0x0c);
    //BX_DEBUG_INT16("int16: func 12 sending %04x\n",AX);
}

static void
handle_166f(struct bregs *regs)
{
    if (regs->al == 0x08)
        // unsupported, aka normal keyboard
        regs->ah = 2;
}

// keyboard capability check called by DOS 5.0+ keyb
static void
handle_1692(struct bregs *regs)
{
    // function int16 ah=0x10-0x12 supported
    regs->ah = 0x80;
}

// 122 keys capability check called by DOS 5.0+ keyb
static void
handle_16a2(struct bregs *regs)
{
    // don't change AH : function int16 ah=0x20-0x22 NOT supported
}

static void
set_leds()
{
    u8 shift_flags = GET_BDA(kbd_flag0);
    u8 led_flags = GET_BDA(kbd_led);
    if (((shift_flags >> 4) & 0x07) ^ ((led_flags & 0x07) == 0))
        return;

    outb(0xed, PORT_PS2_DATA);
    while ((inb(PORT_PS2_STATUS) & 0x01) == 0)
        outb(0x21, PORT_DIAG);
    if (inb(PORT_PS2_DATA) == 0xfa) {
        led_flags &= 0xf8;
        led_flags |= (shift_flags >> 4) & 0x07;
        outb(led_flags & 0x07, PORT_PS2_DATA);
        while ((inb(PORT_PS2_STATUS) & 0x01) == 0)
            outb(0x21, PORT_DIAG);
        inb(PORT_PS2_DATA);
        SET_BDA(kbd_led, led_flags);
    }
}

// INT 16h Keyboard Service Entry Point
void VISIBLE16
handle_16(struct bregs *regs)
{
    //debug_enter(regs);

    set_leds();

    irq_enable();

    switch (regs->ah) {
    case 0x00: handle_1600(regs); break;
    case 0x01: handle_1601(regs); break;
    case 0x02: handle_1602(regs); break;
    case 0x05: handle_1605(regs); break;
    case 0x09: handle_1609(regs); break;
    case 0x0a: handle_160a(regs); break;
    case 0x10: handle_1610(regs); break;
    case 0x11: handle_1611(regs); break;
    case 0x12: handle_1612(regs); break;
    case 0x92: handle_1692(regs); break;
    case 0xa2: handle_16a2(regs); break;
    case 0x6f: handle_166f(regs); break;
    }
}

#define none 0
#define MAX_SCAN_CODE 0x58

static struct scaninfo {
    u16 normal;
    u16 shift;
    u16 control;
    u16 alt;
    u8 lock_flags;
} scan_to_scanascii[MAX_SCAN_CODE + 1] = {
    {   none,   none,   none,   none, none },
    { 0x011b, 0x011b, 0x011b, 0x0100, none }, /* escape */
    { 0x0231, 0x0221,   none, 0x7800, none }, /* 1! */
    { 0x0332, 0x0340, 0x0300, 0x7900, none }, /* 2@ */
    { 0x0433, 0x0423,   none, 0x7a00, none }, /* 3# */
    { 0x0534, 0x0524,   none, 0x7b00, none }, /* 4$ */
    { 0x0635, 0x0625,   none, 0x7c00, none }, /* 5% */
    { 0x0736, 0x075e, 0x071e, 0x7d00, none }, /* 6^ */
    { 0x0837, 0x0826,   none, 0x7e00, none }, /* 7& */
    { 0x0938, 0x092a,   none, 0x7f00, none }, /* 8* */
    { 0x0a39, 0x0a28,   none, 0x8000, none }, /* 9( */
    { 0x0b30, 0x0b29,   none, 0x8100, none }, /* 0) */
    { 0x0c2d, 0x0c5f, 0x0c1f, 0x8200, none }, /* -_ */
    { 0x0d3d, 0x0d2b,   none, 0x8300, none }, /* =+ */
    { 0x0e08, 0x0e08, 0x0e7f,   none, none }, /* backspace */
    { 0x0f09, 0x0f00,   none,   none, none }, /* tab */
    { 0x1071, 0x1051, 0x1011, 0x1000, 0x40 }, /* Q */
    { 0x1177, 0x1157, 0x1117, 0x1100, 0x40 }, /* W */
    { 0x1265, 0x1245, 0x1205, 0x1200, 0x40 }, /* E */
    { 0x1372, 0x1352, 0x1312, 0x1300, 0x40 }, /* R */
    { 0x1474, 0x1454, 0x1414, 0x1400, 0x40 }, /* T */
    { 0x1579, 0x1559, 0x1519, 0x1500, 0x40 }, /* Y */
    { 0x1675, 0x1655, 0x1615, 0x1600, 0x40 }, /* U */
    { 0x1769, 0x1749, 0x1709, 0x1700, 0x40 }, /* I */
    { 0x186f, 0x184f, 0x180f, 0x1800, 0x40 }, /* O */
    { 0x1970, 0x1950, 0x1910, 0x1900, 0x40 }, /* P */
    { 0x1a5b, 0x1a7b, 0x1a1b,   none, none }, /* [{ */
    { 0x1b5d, 0x1b7d, 0x1b1d,   none, none }, /* ]} */
    { 0x1c0d, 0x1c0d, 0x1c0a,   none, none }, /* Enter */
    {   none,   none,   none,   none, none }, /* L Ctrl */
    { 0x1e61, 0x1e41, 0x1e01, 0x1e00, 0x40 }, /* A */
    { 0x1f73, 0x1f53, 0x1f13, 0x1f00, 0x40 }, /* S */
    { 0x2064, 0x2044, 0x2004, 0x2000, 0x40 }, /* D */
    { 0x2166, 0x2146, 0x2106, 0x2100, 0x40 }, /* F */
    { 0x2267, 0x2247, 0x2207, 0x2200, 0x40 }, /* G */
    { 0x2368, 0x2348, 0x2308, 0x2300, 0x40 }, /* H */
    { 0x246a, 0x244a, 0x240a, 0x2400, 0x40 }, /* J */
    { 0x256b, 0x254b, 0x250b, 0x2500, 0x40 }, /* K */
    { 0x266c, 0x264c, 0x260c, 0x2600, 0x40 }, /* L */
    { 0x273b, 0x273a,   none,   none, none }, /* ;: */
    { 0x2827, 0x2822,   none,   none, none }, /* '" */
    { 0x2960, 0x297e,   none,   none, none }, /* `~ */
    {   none,   none,   none,   none, none }, /* L shift */
    { 0x2b5c, 0x2b7c, 0x2b1c,   none, none }, /* |\ */
    { 0x2c7a, 0x2c5a, 0x2c1a, 0x2c00, 0x40 }, /* Z */
    { 0x2d78, 0x2d58, 0x2d18, 0x2d00, 0x40 }, /* X */
    { 0x2e63, 0x2e43, 0x2e03, 0x2e00, 0x40 }, /* C */
    { 0x2f76, 0x2f56, 0x2f16, 0x2f00, 0x40 }, /* V */
    { 0x3062, 0x3042, 0x3002, 0x3000, 0x40 }, /* B */
    { 0x316e, 0x314e, 0x310e, 0x3100, 0x40 }, /* N */
    { 0x326d, 0x324d, 0x320d, 0x3200, 0x40 }, /* M */
    { 0x332c, 0x333c,   none,   none, none }, /* ,< */
    { 0x342e, 0x343e,   none,   none, none }, /* .> */
    { 0x352f, 0x353f,   none,   none, none }, /* /? */
    {   none,   none,   none,   none, none }, /* R Shift */
    { 0x372a, 0x372a,   none,   none, none }, /* * */
    {   none,   none,   none,   none, none }, /* L Alt */
    { 0x3920, 0x3920, 0x3920, 0x3920, none }, /* space */
    {   none,   none,   none,   none, none }, /* caps lock */
    { 0x3b00, 0x5400, 0x5e00, 0x6800, none }, /* F1 */
    { 0x3c00, 0x5500, 0x5f00, 0x6900, none }, /* F2 */
    { 0x3d00, 0x5600, 0x6000, 0x6a00, none }, /* F3 */
    { 0x3e00, 0x5700, 0x6100, 0x6b00, none }, /* F4 */
    { 0x3f00, 0x5800, 0x6200, 0x6c00, none }, /* F5 */
    { 0x4000, 0x5900, 0x6300, 0x6d00, none }, /* F6 */
    { 0x4100, 0x5a00, 0x6400, 0x6e00, none }, /* F7 */
    { 0x4200, 0x5b00, 0x6500, 0x6f00, none }, /* F8 */
    { 0x4300, 0x5c00, 0x6600, 0x7000, none }, /* F9 */
    { 0x4400, 0x5d00, 0x6700, 0x7100, none }, /* F10 */
    {   none,   none,   none,   none, none }, /* Num Lock */
    {   none,   none,   none,   none, none }, /* Scroll Lock */
    { 0x4700, 0x4737, 0x7700,   none, 0x20 }, /* 7 Home */
    { 0x4800, 0x4838,   none,   none, 0x20 }, /* 8 UP */
    { 0x4900, 0x4939, 0x8400,   none, 0x20 }, /* 9 PgUp */
    { 0x4a2d, 0x4a2d,   none,   none, none }, /* - */
    { 0x4b00, 0x4b34, 0x7300,   none, 0x20 }, /* 4 Left */
    { 0x4c00, 0x4c35,   none,   none, 0x20 }, /* 5 */
    { 0x4d00, 0x4d36, 0x7400,   none, 0x20 }, /* 6 Right */
    { 0x4e2b, 0x4e2b,   none,   none, none }, /* + */
    { 0x4f00, 0x4f31, 0x7500,   none, 0x20 }, /* 1 End */
    { 0x5000, 0x5032,   none,   none, 0x20 }, /* 2 Down */
    { 0x5100, 0x5133, 0x7600,   none, 0x20 }, /* 3 PgDn */
    { 0x5200, 0x5230,   none,   none, 0x20 }, /* 0 Ins */
    { 0x5300, 0x532e,   none,   none, 0x20 }, /* Del */
    {   none,   none,   none,   none, none },
    {   none,   none,   none,   none, none },
    { 0x565c, 0x567c,   none,   none, none }, /* \| */
    { 0x5700, 0x5700,   none,   none, none }, /* F11 */
    { 0x5800, 0x5800,   none,   none, none }  /* F12 */
};

static void
process_key(u8 scancode)
{
    u8 shift_flags = GET_BDA(kbd_flag0);
    u8 mf2_flags = GET_BDA(kbd_flag1);
    u8 mf2_state = GET_BDA(kbd_mode);

    switch (scancode) {
    case 0x00:
        BX_INFO("KBD: int09 handler: AL=0\n");
        return;

    case 0x3a: /* Caps Lock press */
        shift_flags ^= 0x40;
        SET_BDA(kbd_flag0, shift_flags);
        mf2_flags |= 0x40;
        SET_BDA(kbd_flag1, mf2_flags);
        break;
    case 0xba: /* Caps Lock release */
        mf2_flags &= ~0x40;
        SET_BDA(kbd_flag1, mf2_flags);
        break;

    case 0x2a: /* L Shift press */
        shift_flags |= 0x02;
        SET_BDA(kbd_flag0, shift_flags);
        break;
    case 0xaa: /* L Shift release */
        shift_flags &= ~0x02;
        SET_BDA(kbd_flag0, shift_flags);
        break;

    case 0x36: /* R Shift press */
        shift_flags |= 0x01;
        SET_BDA(kbd_flag0, shift_flags);
        break;
    case 0xb6: /* R Shift release */
        shift_flags &= ~0x01;
        SET_BDA(kbd_flag0, shift_flags);
        break;

    case 0x1d: /* Ctrl press */
        if ((mf2_state & 0x01) == 0) {
            shift_flags |= 0x04;
            SET_BDA(kbd_flag0, shift_flags);
            if (mf2_state & 0x02) {
                mf2_state |= 0x04;
                SET_BDA(kbd_mode, mf2_state);
            } else {
                mf2_flags |= 0x01;
                SET_BDA(kbd_flag1, mf2_flags);
            }
        }
        break;
    case 0x9d: /* Ctrl release */
        if ((mf2_state & 0x01) == 0) {
            shift_flags &= ~0x04;
            SET_BDA(kbd_flag0, shift_flags);
            if (mf2_state & 0x02) {
                mf2_state &= ~0x04;
                SET_BDA(kbd_mode, mf2_state);
            } else {
                mf2_flags &= ~0x01;
                SET_BDA(kbd_flag1, mf2_flags);
            }
        }
        break;

    case 0x38: /* Alt press */
        shift_flags |= 0x08;
        SET_BDA(kbd_flag0, shift_flags);
        if (mf2_state & 0x02) {
            mf2_state |= 0x08;
            SET_BDA(kbd_mode, mf2_state);
        } else {
            mf2_flags |= 0x02;
            SET_BDA(kbd_flag1, mf2_flags);
        }
        break;
    case 0xb8: /* Alt release */
        shift_flags &= ~0x08;
        SET_BDA(kbd_flag0, shift_flags);
        if (mf2_state & 0x02) {
            mf2_state &= ~0x08;
            SET_BDA(kbd_mode, mf2_state);
        } else {
            mf2_flags &= ~0x02;
            SET_BDA(kbd_flag1, mf2_flags);
        }
        break;

    case 0x45: /* Num Lock press */
        if ((mf2_state & 0x03) == 0) {
            mf2_flags |= 0x20;
            SET_BDA(kbd_flag1, mf2_flags);
            shift_flags ^= 0x20;
            SET_BDA(kbd_flag0, shift_flags);
        }
        break;
    case 0xc5: /* Num Lock release */
        if ((mf2_state & 0x03) == 0) {
            mf2_flags &= ~0x20;
            SET_BDA(kbd_flag1, mf2_flags);
        }
        break;

    case 0x46: /* Scroll Lock press */
        mf2_flags |= 0x10;
        SET_BDA(kbd_flag1, mf2_flags);
        shift_flags ^= 0x10;
        SET_BDA(kbd_flag0, shift_flags);
        break;
    case 0xc6: /* Scroll Lock release */
        mf2_flags &= ~0x10;
        SET_BDA(kbd_flag1, mf2_flags);
        break;

    case 0xe0:
        // Extended key
        SETBITS_BDA(kbd_mode, 0x02);
        return;
    case 0xe1:
        // Pause key
        SETBITS_BDA(kbd_mode, 0x01);
        return;

    default:
        if (scancode & 0x80) {
            break; /* toss key releases ... */
        }
        if (scancode > MAX_SCAN_CODE) {
            BX_INFO("KBD: int09h_handler(): unknown scancode read: 0x%02x!\n"
                    , scancode);
            return;
        }
        u8 asciicode;
        struct scaninfo *info = &scan_to_scanascii[scancode];
        if (shift_flags & 0x08) { /* ALT */
            asciicode = GET_VAR(CS, info->alt);
            scancode = GET_VAR(CS, info->alt) >> 8;
        } else if (shift_flags & 0x04) { /* CONTROL */
            asciicode = GET_VAR(CS, info->control);
            scancode = GET_VAR(CS, info->control) >> 8;
        } else if ((mf2_state & 0x02) > 0
                   && scancode >= 0x47 && scancode <= 0x53) {
            /* extended keys handling */
            asciicode = 0xe0;
            scancode = GET_VAR(CS, info->normal) >> 8;
        } else if (shift_flags & 0x03) { /* LSHIFT + RSHIFT */
            /* check if lock state should be ignored
             * because a SHIFT key are pressed */

            if (shift_flags & GET_VAR(CS, info->lock_flags)) {
                asciicode = GET_VAR(CS, info->normal);
                scancode = GET_VAR(CS, info->normal) >> 8;
            } else {
                asciicode = GET_VAR(CS, info->shift);
                scancode = GET_VAR(CS, info->shift) >> 8;
            }
        } else {
            /* check if lock is on */
            if (shift_flags & GET_VAR(CS, info->lock_flags)) {
                asciicode = GET_VAR(CS, info->shift);
                scancode = GET_VAR(CS, info->shift) >> 8;
            } else {
                asciicode = GET_VAR(CS, info->normal);
                scancode = GET_VAR(CS, info->normal) >> 8;
            }
        }
        if (scancode==0 && asciicode==0) {
            BX_INFO("KBD: int09h_handler(): scancode & asciicode are zero?\n");
        }
        enqueue_key(scancode, asciicode);
        break;
    }
    if ((scancode & 0x7f) != 0x1d) {
        mf2_state &= ~0x01;
    }
    mf2_state &= ~0x02;
    SET_BDA(kbd_mode, mf2_state);
}

// INT09h : Keyboard Hardware Service Entry Point
void VISIBLE16
handle_09()
{
    //debug_isr();

    // disable keyboard
    outb(0xad, PORT_PS2_STATUS);

    outb(0x0b, PORT_PIC1);
    if ((inb(PORT_PIC1) & 0x02) == 0)
        goto done;

    // read key from keyboard controller
    u8 key = inb(PORT_PS2_DATA);
    irq_enable();
    if (CONFIG_KBD_CALL_INT15_4F) {
        // allow for keyboard intercept
        struct bregs tr;
        memset(&tr, 0, sizeof(tr));
        tr.al = key;
        tr.ah = 0x4f;
        tr.flags = F_CF;
        call16_int(0x15, &tr);
        if (!(tr.flags & F_CF))
            goto done;
        key = tr.al;
    }
    process_key(key);

    irq_disable();
    eoi_master_pic();

done:
    // enable keyboard
    outb(0xae, PORT_PS2_STATUS);
}
