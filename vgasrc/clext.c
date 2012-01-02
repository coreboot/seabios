//  QEMU Cirrus CLGD 54xx VGABIOS Extension.
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
//  Copyright (c) 2004 Makoto Suzuki (suzu)
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "clext.h" // clext_init
#include "vgabios.h" // VBE_VENDOR_STRING
#include "biosvar.h" // GET_GLOBAL
#include "util.h" // dprintf
#include "bregs.h" // struct bregs
#include "vbe.h" // struct vbe_info
#include "stdvga.h" // VGAREG_SEQU_ADDRESS


/****************************************************************
 * tables
 ****************************************************************/

struct cirrus_mode_s {
    /* + 0 */
    u16 mode;
    u8 memmodel;
    u16 width;
    u16 height;
    u16 depth;
    /* + 8 */
    u16 hidden_dac; /* 0x3c6 */
    u16 *seq; /* 0x3c4 */
    u16 *graph; /* 0x3ce */
    u16 *crtc; /* 0x3d4 */
    /* +16 */
    u8 bitsperpixel;
    u8 vesaredmask;
    u8 vesaredpos;
    u8 vesagreenmask;
    u8 vesagreenpos;
    u8 vesabluemask;
    u8 vesabluepos;
    /* +24 */
    u8 vesareservedmask;
    u8 vesareservedpos;
};

/* VGA */
static u16 cseq_vga[] VAR16 = {0x0007,0xffff};
static u16 cgraph_vga[] VAR16 = {0x0009,0x000a,0x000b,0xffff};
static u16 ccrtc_vga[] VAR16 = {0x001a,0x001b,0x001d,0xffff};

/* extensions */
static u16 cgraph_svgacolor[] VAR16 = {
    0x0000,0x0001,0x0002,0x0003,0x0004,0x4005,0x0506,0x0f07,0xff08,
    0x0009,0x000a,0x000b,
    0xffff
};
/* 640x480x8 */
static u16 cseq_640x480x8[] VAR16 = {
    0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1107,
    0x580b,0x580c,0x580d,0x580e,
    0x0412,0x0013,0x2017,
    0x331b,0x331c,0x331d,0x331e,
    0xffff
};
static u16 ccrtc_640x480x8[] VAR16 = {
    0x2c11,
    0x5f00,0x4f01,0x4f02,0x8003,0x5204,0x1e05,0x0b06,0x3e07,
    0x4009,0x000c,0x000d,
    0xea10,0xdf12,0x5013,0x4014,0xdf15,0x0b16,0xc317,0xff18,
    0x001a,0x221b,0x001d,
    0xffff
};
/* 640x480x16 */
static u16 cseq_640x480x16[] VAR16 = {
    0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1707,
    0x580b,0x580c,0x580d,0x580e,
    0x0412,0x0013,0x2017,
    0x331b,0x331c,0x331d,0x331e,
    0xffff
};
static u16 ccrtc_640x480x16[] VAR16 = {
    0x2c11,
    0x5f00,0x4f01,0x4f02,0x8003,0x5204,0x1e05,0x0b06,0x3e07,
    0x4009,0x000c,0x000d,
    0xea10,0xdf12,0xa013,0x4014,0xdf15,0x0b16,0xc317,0xff18,
    0x001a,0x221b,0x001d,
    0xffff
};
/* 640x480x24 */
static u16 cseq_640x480x24[] VAR16 = {
    0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1507,
    0x580b,0x580c,0x580d,0x580e,
    0x0412,0x0013,0x2017,
    0x331b,0x331c,0x331d,0x331e,
    0xffff
};
static u16 ccrtc_640x480x24[] VAR16 = {
    0x2c11,
    0x5f00,0x4f01,0x4f02,0x8003,0x5204,0x1e05,0x0b06,0x3e07,
    0x4009,0x000c,0x000d,
    0xea10,0xdf12,0x0013,0x4014,0xdf15,0x0b16,0xc317,0xff18,
    0x001a,0x321b,0x001d,
    0xffff
};
/* 800x600x8 */
static u16 cseq_800x600x8[] VAR16 = {
    0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1107,
    0x230b,0x230c,0x230d,0x230e,
    0x0412,0x0013,0x2017,
    0x141b,0x141c,0x141d,0x141e,
    0xffff
};
static u16 ccrtc_800x600x8[] VAR16 = {
    0x2311,0x7d00,0x6301,0x6302,0x8003,0x6b04,0x1a05,0x9806,0xf007,
    0x6009,0x000c,0x000d,
    0x7d10,0x5712,0x6413,0x4014,0x5715,0x9816,0xc317,0xff18,
    0x001a,0x221b,0x001d,
    0xffff
};
/* 800x600x16 */
static u16 cseq_800x600x16[] VAR16 = {
    0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1707,
    0x230b,0x230c,0x230d,0x230e,
    0x0412,0x0013,0x2017,
    0x141b,0x141c,0x141d,0x141e,
    0xffff
};
static u16 ccrtc_800x600x16[] VAR16 = {
    0x2311,0x7d00,0x6301,0x6302,0x8003,0x6b04,0x1a05,0x9806,0xf007,
    0x6009,0x000c,0x000d,
    0x7d10,0x5712,0xc813,0x4014,0x5715,0x9816,0xc317,0xff18,
    0x001a,0x221b,0x001d,
    0xffff
};
/* 800x600x24 */
static u16 cseq_800x600x24[] VAR16 = {
    0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1507,
    0x230b,0x230c,0x230d,0x230e,
    0x0412,0x0013,0x2017,
    0x141b,0x141c,0x141d,0x141e,
    0xffff
};
static u16 ccrtc_800x600x24[] VAR16 = {
    0x2311,0x7d00,0x6301,0x6302,0x8003,0x6b04,0x1a05,0x9806,0xf007,
    0x6009,0x000c,0x000d,
    0x7d10,0x5712,0x2c13,0x4014,0x5715,0x9816,0xc317,0xff18,
    0x001a,0x321b,0x001d,
    0xffff
};
/* 1024x768x8 */
static u16 cseq_1024x768x8[] VAR16 = {
    0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1107,
    0x760b,0x760c,0x760d,0x760e,
    0x0412,0x0013,0x2017,
    0x341b,0x341c,0x341d,0x341e,
    0xffff
};
static u16 ccrtc_1024x768x8[] VAR16 = {
    0x2911,0xa300,0x7f01,0x7f02,0x8603,0x8304,0x9405,0x2406,0xf507,
    0x6009,0x000c,0x000d,
    0x0310,0xff12,0x8013,0x4014,0xff15,0x2416,0xc317,0xff18,
    0x001a,0x221b,0x001d,
    0xffff
};
/* 1024x768x16 */
static u16 cseq_1024x768x16[] VAR16 = {
    0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1707,
    0x760b,0x760c,0x760d,0x760e,
    0x0412,0x0013,0x2017,
    0x341b,0x341c,0x341d,0x341e,
    0xffff
};
static u16 ccrtc_1024x768x16[] VAR16 = {
    0x2911,0xa300,0x7f01,0x7f02,0x8603,0x8304,0x9405,0x2406,0xf507,
    0x6009,0x000c,0x000d,
    0x0310,0xff12,0x0013,0x4014,0xff15,0x2416,0xc317,0xff18,
    0x001a,0x321b,0x001d,
    0xffff
};
/* 1024x768x24 */
static u16 cseq_1024x768x24[] VAR16 = {
    0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1507,
    0x760b,0x760c,0x760d,0x760e,
    0x0412,0x0013,0x2017,
    0x341b,0x341c,0x341d,0x341e,
    0xffff
};
static u16 ccrtc_1024x768x24[] VAR16 = {
    0x2911,0xa300,0x7f01,0x7f02,0x8603,0x8304,0x9405,0x2406,0xf507,
    0x6009,0x000c,0x000d,
    0x0310,0xff12,0x8013,0x4014,0xff15,0x2416,0xc317,0xff18,
    0x001a,0x321b,0x001d,
    0xffff
};
/* 1280x1024x8 */
static u16 cseq_1280x1024x8[] VAR16 = {
    0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1107,
    0x760b,0x760c,0x760d,0x760e,
    0x0412,0x0013,0x2017,
    0x341b,0x341c,0x341d,0x341e,
    0xffff
};
static u16 ccrtc_1280x1024x8[] VAR16 = {
    0x2911,0xc300,0x9f01,0x9f02,0x8603,0x8304,0x9405,0x2406,0xf707,
    0x6009,0x000c,0x000d,
    0x0310,0xff12,0xa013,0x4014,0xff15,0x2416,0xc317,0xff18,
    0x001a,0x221b,0x001d,
    0xffff
};
/* 1280x1024x16 */
static u16 cseq_1280x1024x16[] VAR16 = {
    0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1707,
    0x760b,0x760c,0x760d,0x760e,
    0x0412,0x0013,0x2017,
    0x341b,0x341c,0x341d,0x341e,
    0xffff
};
static u16 ccrtc_1280x1024x16[] VAR16 = {
    0x2911,0xc300,0x9f01,0x9f02,0x8603,0x8304,0x9405,0x2406,0xf707,
    0x6009,0x000c,0x000d,
    0x0310,0xff12,0x4013,0x4014,0xff15,0x2416,0xc317,0xff18,
    0x001a,0x321b,0x001d,
    0xffff
};

/* 1600x1200x8 */
static u16 cseq_1600x1200x8[] VAR16 = {
    0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1107,
    0x760b,0x760c,0x760d,0x760e,
    0x0412,0x0013,0x2017,
    0x341b,0x341c,0x341d,0x341e,
    0xffff
};
static u16 ccrtc_1600x1200x8[] VAR16 = {
    0x2911,0xc300,0x9f01,0x9f02,0x8603,0x8304,0x9405,0x2406,0xf707,
    0x6009,0x000c,0x000d,
    0x0310,0xff12,0xa013,0x4014,0xff15,0x2416,0xc317,0xff18,
    0x001a,0x221b,0x001d,
    0xffff
};

static struct cirrus_mode_s cirrus_modes[] VAR16 = {
    {0x5f,MM_PACKED,640,480,8,0x00,
     cseq_640x480x8,cgraph_svgacolor,ccrtc_640x480x8,8,
     0,0,0,0,0,0,0,0},
    {0x64,MM_DIRECT,640,480,16,0xe1,
     cseq_640x480x16,cgraph_svgacolor,ccrtc_640x480x16,16,
     5,11,6,5,5,0,0,0},
    {0x66,MM_DIRECT,640,480,15,0xf0,
     cseq_640x480x16,cgraph_svgacolor,ccrtc_640x480x16,16,
     5,10,5,5,5,0,1,15},
    {0x71,MM_DIRECT,640,480,24,0xe5,
     cseq_640x480x24,cgraph_svgacolor,ccrtc_640x480x24,24,
     8,16,8,8,8,0,0,0},

    {0x5c,MM_PACKED,800,600,8,0x00,
     cseq_800x600x8,cgraph_svgacolor,ccrtc_800x600x8,8,
     0,0,0,0,0,0,0,0},
    {0x65,MM_DIRECT,800,600,16,0xe1,
     cseq_800x600x16,cgraph_svgacolor,ccrtc_800x600x16,16,
     5,11,6,5,5,0,0,0},
    {0x67,MM_DIRECT,800,600,15,0xf0,
     cseq_800x600x16,cgraph_svgacolor,ccrtc_800x600x16,16,
     5,10,5,5,5,0,1,15},

    {0x60,MM_PACKED,1024,768,8,0x00,
     cseq_1024x768x8,cgraph_svgacolor,ccrtc_1024x768x8,8,
     0,0,0,0,0,0,0,0},
    {0x74,MM_DIRECT,1024,768,16,0xe1,
     cseq_1024x768x16,cgraph_svgacolor,ccrtc_1024x768x16,16,
     5,11,6,5,5,0,0,0},
    {0x68,MM_DIRECT,1024,768,15,0xf0,
     cseq_1024x768x16,cgraph_svgacolor,ccrtc_1024x768x16,16,
     5,10,5,5,5,0,1,15},

    {0x78,MM_DIRECT,800,600,24,0xe5,
     cseq_800x600x24,cgraph_svgacolor,ccrtc_800x600x24,24,
     8,16,8,8,8,0,0,0},
    {0x79,MM_DIRECT,1024,768,24,0xe5,
     cseq_1024x768x24,cgraph_svgacolor,ccrtc_1024x768x24,24,
     8,16,8,8,8,0,0,0},

    {0x6d,MM_PACKED,1280,1024,8,0x00,
     cseq_1280x1024x8,cgraph_svgacolor,ccrtc_1280x1024x8,8,
     0,0,0,0,0,0,0,0},
    {0x69,MM_DIRECT,1280,1024,15,0xf0,
     cseq_1280x1024x16,cgraph_svgacolor,ccrtc_1280x1024x16,16,
     5,10,5,5,5,0,1,15},
    {0x75,MM_DIRECT,1280,1024,16,0xe1,
     cseq_1280x1024x16,cgraph_svgacolor,ccrtc_1280x1024x16,16,
     5,11,6,5,5,0,0,0},

    {0x7b,MM_PACKED,1600,1200,8,0x00,
     cseq_1600x1200x8,cgraph_svgacolor,ccrtc_1600x1200x8,8,
     0,0,0,0,0,0,0,0},

    {0xfe,0xff,0,0,0,0,cseq_vga,cgraph_vga,ccrtc_vga,0,
     0,0,0,0,0,0,0,0},
};


/****************************************************************
 * helper functions
 ****************************************************************/

static struct cirrus_mode_s *
cirrus_get_modeentry(u8 mode)
{
    struct cirrus_mode_s *table_g = cirrus_modes;
    while (table_g < &cirrus_modes[ARRAY_SIZE(cirrus_modes)]) {
        u16 tmode = GET_GLOBAL(table_g->mode);
        if (tmode == mode)
            return table_g;
        table_g++;
    }
    return NULL;
}

static void
cirrus_switch_mode_setregs(u16 *data, u16 port)
{
    for (;;) {
        u16 val = GET_GLOBAL(*data);
        if (val == 0xffff)
            return;
        outw(val, port);
        data++;
    }
}

static void
cirrus_switch_mode(struct cirrus_mode_s *table)
{
    // Unlock cirrus special
    outw(0x1206, VGAREG_SEQU_ADDRESS);
    cirrus_switch_mode_setregs(GET_GLOBAL(table->seq), VGAREG_SEQU_ADDRESS);
    cirrus_switch_mode_setregs(GET_GLOBAL(table->graph), VGAREG_GRDC_ADDRESS);
    cirrus_switch_mode_setregs(GET_GLOBAL(table->crtc), stdvga_get_crtc());

    outb(0x00, VGAREG_PEL_MASK);
    inb(VGAREG_PEL_MASK);
    inb(VGAREG_PEL_MASK);
    inb(VGAREG_PEL_MASK);
    inb(VGAREG_PEL_MASK);
    outb(GET_GLOBAL(table->hidden_dac), VGAREG_PEL_MASK);
    outb(0xff, VGAREG_PEL_MASK);

    u8 memmodel = GET_GLOBAL(table->memmodel);
    u8 v = stdvga_get_single_palette_reg(0x10) & 0xfe;
    if (memmodel == MM_PLANAR)
        v |= 0x41;
    else if (memmodel != MM_TEXT)
        v |= 0x01;
    stdvga_set_single_palette_reg(0x10, v);
}

static u8
cirrus_get_memsize(void)
{
    // get DRAM band width
    outb(0x0f, VGAREG_SEQU_ADDRESS);
    u8 v = inb(VGAREG_SEQU_DATA);
    u8 x = (v >> 3) & 0x03;
    if (x == 0x03) {
        if (v & 0x80)
            // 4MB
            return 0x40;
        // 2MB
        return 0x20;
    }
    return 0x04 << x;
}

static void
cirrus_enable_16k_granularity(void)
{
    outb(0x0b, VGAREG_GRDC_ADDRESS);
    u8 v = inb(VGAREG_GRDC_DATA);
    outb(v | 0x20, VGAREG_GRDC_DATA);
}

static void
cirrus_clear_vram(u16 param)
{
    cirrus_enable_16k_granularity();
    u8 count = cirrus_get_memsize() * 4;
    u8 i;
    for (i=0; i<count; i++) {
        outw((i<<8) | 0x09, VGAREG_GRDC_ADDRESS);
        memset16_far(SEG_GRAPH, 0, param, 16 * 1024);
    }
    outw(0x0009, VGAREG_GRDC_ADDRESS);
}

int
clext_set_mode(int mode, int flags)
{
    dprintf(1, "cirrus mode %d\n", mode);
    SET_BDA(vbe_mode, 0);
    struct cirrus_mode_s *table_g = cirrus_get_modeentry(mode);
    if (table_g) {
        cirrus_switch_mode(table_g);
        if (!(flags & MF_NOCLEARMEM))
            cirrus_clear_vram(0xffff);
        SET_BDA(video_mode, mode);
        return 0;
    }
    table_g = cirrus_get_modeentry(0xfe);
    cirrus_switch_mode(table_g);
    dprintf(1, "cirrus mode switch regular\n");
    return stdvga_set_mode(mode, flags);
}

static int
cirrus_check(void)
{
    outw(0x9206, VGAREG_SEQU_ADDRESS);
    return inb(VGAREG_SEQU_DATA) == 0x12;
}


/****************************************************************
 * extbios
 ****************************************************************/

static void
cirrus_extbios_80h(struct bregs *regs)
{
    u16 crtc_addr = stdvga_get_crtc();
    outb(0x27, crtc_addr);
    u8 v = inb(crtc_addr + 1);
    if (v == 0xa0)
        // 5430
        regs->ax = 0x0032;
    else if (v == 0xb8)
        // 5446
        regs->ax = 0x0039;
    else
        regs->ax = 0x00ff;
    regs->bx = 0x00;
    return;
}

static void
cirrus_extbios_81h(struct bregs *regs)
{
    // XXX
    regs->ax = 0x0100;
}

static void
cirrus_extbios_82h(struct bregs *regs)
{
    u16 crtc_addr = stdvga_get_crtc();
    outb(0x27, crtc_addr);
    regs->al = inb(crtc_addr + 1) & 0x03;
    regs->ah = 0xAF;
}

static void
cirrus_extbios_85h(struct bregs *regs)
{
    regs->al = cirrus_get_memsize();
}

static void
cirrus_extbios_9Ah(struct bregs *regs)
{
    regs->ax = 0x4060;
    regs->cx = 0x1132;
}

extern void a0h_callback(void);
ASM16(
    // fatal: not implemented yet
    "a0h_callback:"
    "cli\n"
    "hlt\n"
    "retf");

static void
cirrus_extbios_A0h(struct bregs *regs)
{
    struct cirrus_mode_s *table_g = cirrus_get_modeentry(regs->al & 0x7f);
    regs->ah = (table_g ? 1 : 0);
    regs->si = 0xffff;
    regs->di = regs->ds = regs->es = regs->bx = (u32)a0h_callback;
}

static void
cirrus_extbios_A1h(struct bregs *regs)
{
    regs->bx = 0x0e00; // IBM 8512/8513, color
}

static void
cirrus_extbios_A2h(struct bregs *regs)
{
    regs->al = 0x07; // HSync 31.5 - 64.0 kHz
}

static void
cirrus_extbios_AEh(struct bregs *regs)
{
    regs->al = 0x01; // High Refresh 75Hz
}

void
cirrus_extbios(struct bregs *regs)
{
    // XXX - regs->bl < 0x80 or > 0xaf call regular handlers.
    switch (regs->bl) {
    case 0x80: cirrus_extbios_80h(regs); break;
    case 0x81: cirrus_extbios_81h(regs); break;
    case 0x82: cirrus_extbios_82h(regs); break;
    case 0x85: cirrus_extbios_85h(regs); break;
    case 0x9a: cirrus_extbios_9Ah(regs); break;
    case 0xa0: cirrus_extbios_A0h(regs); break;
    case 0xa1: cirrus_extbios_A1h(regs); break;
    case 0xa2: cirrus_extbios_A2h(regs); break;
    case 0xae: cirrus_extbios_AEh(regs); break;
    default: break;
    }
}


/****************************************************************
 * vesa calls
 ****************************************************************/

static struct {
    u16 vesamode, mode;
} cirrus_vesa_modelist[] VAR16 = {
    // 640x480x8
    { 0x101, 0x5f },
    // 640x480x15
    { 0x110, 0x66 },
    // 640x480x16
    { 0x111, 0x64 },
    // 640x480x24
    { 0x112, 0x71 },
    // 800x600x8
    { 0x103, 0x5c },
    // 800x600x15
    { 0x113, 0x67 },
    // 800x600x16
    { 0x114, 0x65 },
    // 800x600x24
    { 0x115, 0x78 },
    // 1024x768x8
    { 0x105, 0x60 },
    // 1024x768x15
    { 0x116, 0x68 },
    // 1024x768x16
    { 0x117, 0x74 },
    // 1024x768x24
    { 0x118, 0x79 },
    // 1280x1024x8
    { 0x107, 0x6d },
    // 1280x1024x15
    { 0x119, 0x69 },
    // 1280x1024x16
    { 0x11a, 0x75 },
};

static u16
cirrus_vesamode_to_mode(u16 vesamode)
{
    int i;
    for (i=0; i<ARRAY_SIZE(cirrus_vesa_modelist); i++)
        if (GET_GLOBAL(cirrus_vesa_modelist[i].vesamode) == vesamode)
            return GET_GLOBAL(cirrus_vesa_modelist[i].mode);
    return 0;
}

static u8
cirrus_get_bpp_bytes(void)
{
    outb(0x07, VGAREG_SEQU_ADDRESS);
    u8 v = inb(VGAREG_SEQU_DATA) & 0x0e;
    if (v == 0x06)
        v &= 0x02;
    v >>= 1;
    if (v != 0x04)
        v++;
    return v;
}

static void
cirrus_set_line_offset(u16 new_line_offset)
{
    u16 crtc_addr = stdvga_get_crtc();
    outb(0x13, crtc_addr);
    outb(new_line_offset / 8, crtc_addr + 1);

    outb(0x1b, crtc_addr);
    u8 v = inb(crtc_addr + 1);
    outb((((new_line_offset / 8) & 0x100) >> 4) | (v & 0xef), crtc_addr + 1);
}

static u16
cirrus_get_line_offset(void)
{
    u16 crtc_addr = stdvga_get_crtc();
    outb(0x13, crtc_addr);
    u8 reg13 = inb(crtc_addr + 1);
    outb(0x1b, crtc_addr);
    u8 reg1b = inb(crtc_addr + 1);

    return (((reg1b << 4) & 0x100) + reg13) * 8;
}

static u16
cirrus_get_line_offset_entry(struct cirrus_mode_s *table_g)
{
    u16 *crtc = GET_GLOBAL(table_g->crtc);

    u16 *c = crtc;
    u16 reg13;
    for (;;) {
        reg13 = GET_GLOBAL(*c);
        if ((reg13 & 0xff) == 0x13)
            break;
        c++;
    }
    reg13 >>= 8;

    c = crtc;
    u16 reg1b;
    for (;;) {
        reg1b = GET_GLOBAL(*c);
        if ((reg1b & 0xff) == 0x1b)
            break;
        c++;
    }
    reg1b >>= 8;

    return (((reg1b << 4) & 0x100) + reg13) * 8;
}

static void
cirrus_set_start_addr(u32 addr)
{
    u16 crtc_addr = stdvga_get_crtc();
    outb(0x0d, crtc_addr);
    outb(addr, crtc_addr + 1);

    outb(0x0c, crtc_addr);
    outb(addr>>8, crtc_addr + 1);

    outb(0x1d, crtc_addr);
    u8 v = inb(crtc_addr + 1);
    outb(((addr & 0x0800) >> 4) | (v & 0x7f), crtc_addr + 1);

    outb(0x1b, crtc_addr);
    v = inb(crtc_addr + 1);
    outb(((addr & 0x0100) >> 8) | ((addr & 0x0600) >> 7) | (v & 0xf2)
         , crtc_addr + 1);
}

static u32
cirrus_get_start_addr(void)
{
    u16 crtc_addr = stdvga_get_crtc();
    outb(0x0c, crtc_addr);
    u8 b2 = inb(crtc_addr + 1);

    outb(0x0d, crtc_addr);
    u8 b1 = inb(crtc_addr + 1);

    outb(0x1b, crtc_addr);
    u8 b3 = inb(crtc_addr + 1);

    outb(0x1d, crtc_addr);
    u8 b4 = inb(crtc_addr + 1);

    return (b1 | (b2<<8) | ((b3 & 0x01) << 16) | ((b3 & 0x0c) << 15)
            | ((b4 & 0x80) << 12));
}

static void
cirrus_vesa_00h(struct bregs *regs)
{
    u16 seg = regs->es;
    struct vbe_info *info = (void*)(regs->di+0);

    if (GET_FARVAR(seg, info->signature) == VBE2_SIGNATURE) {
        SET_FARVAR(seg, info->oem_revision, 0x0100);
        SET_FARVAR(seg, info->oem_vendor_string,
                   SEGOFF(get_global_seg(), (u32)VBE_VENDOR_STRING));
        SET_FARVAR(seg, info->oem_product_string,
                   SEGOFF(get_global_seg(), (u32)VBE_PRODUCT_STRING));
        SET_FARVAR(seg, info->oem_revision_string,
                   SEGOFF(get_global_seg(), (u32)VBE_REVISION_STRING));
    }
    SET_FARVAR(seg, info->signature, VESA_SIGNATURE);

    SET_FARVAR(seg, info->version, 0x0200);

    SET_FARVAR(seg, info->oem_string
               , SEGOFF(get_global_seg(), (u32)VBE_OEM_STRING));
    SET_FARVAR(seg, info->capabilities, 0);
    SET_FARVAR(seg, info->total_memory, cirrus_get_memsize());

    u16 *destmode = (void*)info->reserved;
    SET_FARVAR(seg, info->video_mode, SEGOFF(seg, (u32)destmode));
    int i;
    for (i=0; i<ARRAY_SIZE(cirrus_vesa_modelist); i++)
        SET_FARVAR(seg, destmode[i]
                   , GET_GLOBAL(cirrus_vesa_modelist[i].vesamode));
    SET_FARVAR(seg, destmode[i], 0xffff);

    regs->ax = 0x004f;
}

static u32 cirrus_lfb_addr VAR16;

static void
cirrus_vesa_01h(struct bregs *regs)
{
    u16 mode = cirrus_vesamode_to_mode(regs->cx & 0x3fff);
    if (!mode) {
        regs->ax = 0x014f;
        return;
    }
    struct cirrus_mode_s *table_g = cirrus_get_modeentry(mode);
    u32 lfb = GET_GLOBAL(cirrus_lfb_addr); // XXX
    if ((regs->cx & 0x4000) && !lfb) {
        regs->ax = 0x014f;
        return;
    }

    u16 seg = regs->es;
    struct vbe_mode_info *info = (void*)(regs->di+0);
    memset_far(seg, info, 0, sizeof(*info));

    SET_FARVAR(seg, info->mode_attributes, lfb ? 0xbb : 0x3b);
    SET_FARVAR(seg, info->winA_attributes, 0x07);
    SET_FARVAR(seg, info->winB_attributes, 0);
    SET_FARVAR(seg, info->win_granularity, 16);
    SET_FARVAR(seg, info->win_size, 64);
    SET_FARVAR(seg, info->winA_seg, SEG_GRAPH);
    SET_FARVAR(seg, info->winB_seg, 0x0);
    SET_FARVAR(seg, info->win_func_ptr.segoff, 0x0); // XXX
    u16 linesize = cirrus_get_line_offset_entry(table_g);
    SET_FARVAR(seg, info->bytes_per_scanline, linesize);
    SET_FARVAR(seg, info->xres, GET_GLOBAL(table_g->width));
    u16 height = GET_GLOBAL(table_g->height);
    SET_FARVAR(seg, info->yres, height);
    SET_FARVAR(seg, info->xcharsize, 8);
    SET_FARVAR(seg, info->ycharsize, 16);
    SET_FARVAR(seg, info->planes, 1);
    SET_FARVAR(seg, info->bits_per_pixel, GET_GLOBAL(table_g->depth));
    SET_FARVAR(seg, info->banks, 1);
    SET_FARVAR(seg, info->mem_model, GET_GLOBAL(table_g->memmodel));
    SET_FARVAR(seg, info->bank_size, 0);

    int pages = (cirrus_get_memsize() * 64 * 1024) / (height * linesize);
    SET_FARVAR(seg, info->pages, pages - 1);
    SET_FARVAR(seg, info->reserved0, 0);

    SET_FARVAR(seg, info->red_size, GET_GLOBAL(table_g->vesaredmask));
    SET_FARVAR(seg, info->red_pos, GET_GLOBAL(table_g->vesaredpos));
    SET_FARVAR(seg, info->green_size, GET_GLOBAL(table_g->vesagreenmask));
    SET_FARVAR(seg, info->green_pos, GET_GLOBAL(table_g->vesagreenpos));
    SET_FARVAR(seg, info->blue_size, GET_GLOBAL(table_g->vesabluemask));
    SET_FARVAR(seg, info->blue_pos, GET_GLOBAL(table_g->vesabluepos));
    SET_FARVAR(seg, info->alpha_size, GET_GLOBAL(table_g->vesareservedmask));
    SET_FARVAR(seg, info->alpha_pos, GET_GLOBAL(table_g->vesareservedpos));
    u8 directcolor_info = GET_GLOBAL(table_g->bitsperpixel) <= 8;
    SET_FARVAR(seg, info->directcolor_info, directcolor_info);

    SET_FARVAR(seg, info->phys_base, lfb);

    regs->ax = 0x004f;
}

static void
cirrus_vesa_02h(struct bregs *regs)
{
    if (regs->bx & 0x3e00) {
        regs->ax = 0x014f;
        return;
    }
    if ((regs->bx & 0x1ff) < 0x100) {
        // XXX - call legacy mode switch
        regs->ax = 0x004f;
        return;
    }

    u16 mode = cirrus_vesamode_to_mode(regs->cx & 0x3fff);
    if (!mode) {
        regs->ax = 0x014f;
        return;
    }
    struct cirrus_mode_s *table_g = cirrus_get_modeentry(mode);
    cirrus_switch_mode(table_g);

    if (!(regs->bx & 0x4000))
        cirrus_enable_16k_granularity();
    if (!(regs->bx & 0x8000))
        cirrus_clear_vram(0);
    SET_BDA(video_mode, mode);
    SET_BDA(vbe_mode, regs->bx);

    regs->ax = 0x004f;
}

static void
cirrus_vesa_03h(struct bregs *regs)
{
    u16 mode = GET_BDA(vbe_mode);
    if (!mode)
        mode = GET_BDA(video_mode);
    regs->bx = mode;

    regs->ax = 0x004f;
}

// XXX - add cirrus_vesa_05h_farentry to vgaentry.S

static void
cirrus_vesa_05h(struct bregs *regs)
{
    if (regs->bl > 1)
        goto fail;
    if (regs->bh == 0) {
        // set mempage
        if (regs->dx >= 0x100)
            goto fail;
        outw((regs->dx << 8) | (regs->bl + 9), VGAREG_GRDC_ADDRESS);
    } else if (regs->bh == 1) {
        // get mempage
        outb(regs->bl + 9, VGAREG_GRDC_ADDRESS);
        regs->dx = inb(VGAREG_GRDC_DATA);
    } else
        goto fail;

    regs->ax = 0x004f;
    return;
fail:
    regs->ax = 0x014f;
}

static void
cirrus_vesa_06h(struct bregs *regs)
{
    if (regs->bl > 2) {
        regs->ax = 0x0100;
        return;
    }

    if (regs->bl == 0x00) {
        cirrus_set_line_offset(cirrus_get_bpp_bytes() * regs->cx);
    } else if (regs->bl == 0x02) {
        cirrus_set_line_offset(regs->cx);
    }

    u32 v = cirrus_get_line_offset();
    regs->cx = v / cirrus_get_bpp_bytes();
    regs->bx = v;
    regs->dx = (cirrus_get_memsize() * 64 * 1024) / v;
    regs->ax = 0x004f;
}

static void
cirrus_vesa_07h(struct bregs *regs)
{
    if (regs->bl == 0x80 || regs->bl == 0x00) {
        u32 addr = (cirrus_get_bpp_bytes() * regs->cx
                    + cirrus_get_line_offset() * regs->dx);
        cirrus_set_start_addr(addr / 4);
    } else if (regs->bl == 0x01) {
        u32 addr = cirrus_get_start_addr() * 4;
        u32 linelength = cirrus_get_line_offset();
        regs->dx = addr / linelength;
        regs->cx = (addr % linelength) / cirrus_get_bpp_bytes();
    } else {
        regs->ax = 0x0100;
        return;
    }

    regs->ax = 0x004f;
}

static void
cirrus_vesa_10h(struct bregs *regs)
{
    if (regs->bl == 0x00) {
        regs->bx = 0x0f30;
        regs->ax = 0x004f;
        return;
    }
    if (regs->bl == 0x01) {
        SET_BDA(vbe_flag, regs->bh);
        regs->ax = 0x004f;
        return;
    }
    if (regs->bl == 0x02) {
        regs->bh = GET_BDA(vbe_flag);
        regs->ax = 0x004f;
        return;
    }
    regs->ax = 0x014f;
}

static void
cirrus_vesa_not_handled(struct bregs *regs)
{
    debug_stub(regs);
    regs->ax = 0x014f;
}

void
cirrus_vesa(struct bregs *regs)
{
    switch (regs->al) {
    case 0x00: cirrus_vesa_00h(regs); break;
    case 0x01: cirrus_vesa_01h(regs); break;
    case 0x02: cirrus_vesa_02h(regs); break;
    case 0x03: cirrus_vesa_03h(regs); break;
    case 0x05: cirrus_vesa_05h(regs); break;
    case 0x06: cirrus_vesa_06h(regs); break;
    case 0x07: cirrus_vesa_07h(regs); break;
    case 0x10: cirrus_vesa_10h(regs); break;
    default:   cirrus_vesa_not_handled(regs); break;
    }
}


/****************************************************************
 * init
 ****************************************************************/

int
clext_init(void)
{
    int ret = stdvga_init();
    if (ret)
        return ret;

    dprintf(1, "cirrus init\n");
    if (! cirrus_check())
        return -1;
    dprintf(1, "cirrus init 2\n");

    // memory setup
    outb(0x0f, VGAREG_SEQU_ADDRESS);
    u8 v = inb(VGAREG_SEQU_DATA);
    outb(((v & 0x18) << 8) | 0x0a, VGAREG_SEQU_ADDRESS);
    // set vga mode
    outw(0x0007, VGAREG_SEQU_ADDRESS);
    // reset bitblt
    outw(0x0431, VGAREG_GRDC_ADDRESS);
    outw(0x0031, VGAREG_GRDC_ADDRESS);

    return 0;
}
