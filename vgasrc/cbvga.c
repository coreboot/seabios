// Simple framebuffer vgabios for use with coreboot native vga init.
//
// Copyright (C) 2014  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2017  Patrick Rudolph <siro@das-labor.org>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "biosvar.h" // GET_BDA
#include "output.h" // dprintf
#include "stdvga.h" // SEG_CTEXT
#include "string.h" // memset16_far
#include "util.h" // find_cb_table
#include "vgabios.h" // SET_VGA
#include "vgafb.h" // handle_gfx_op
#include "vgautil.h" // VBE_total_memory

static int CBmode VAR16;
static struct vgamode_s CBmodeinfo VAR16;
static struct vgamode_s CBemulinfo VAR16;
static u32 CBlinelength VAR16;

static struct cbvga_mode_s
{
    u16 mode;
    struct vgamode_s info;
} cbvesa_modes[] VAR16 = {
    /* VESA 1.0 modes */
    { 0x110, { MM_DIRECT, 640,  480,  15, 8, 16, SEG_GRAPH } },
    { 0x111, { MM_DIRECT, 640,  480,  16, 8, 16, SEG_GRAPH } },
    { 0x112, { MM_DIRECT, 640,  480,  24, 8, 16, SEG_GRAPH } },
    { 0x113, { MM_DIRECT, 800,  600,  15, 8, 16, SEG_GRAPH } },
    { 0x114, { MM_DIRECT, 800,  600,  16, 8, 16, SEG_GRAPH } },
    { 0x115, { MM_DIRECT, 800,  600,  24, 8, 16, SEG_GRAPH } },
    { 0x116, { MM_DIRECT, 1024, 768,  15, 8, 16, SEG_GRAPH } },
    { 0x117, { MM_DIRECT, 1024, 768,  16, 8, 16, SEG_GRAPH } },
    { 0x118, { MM_DIRECT, 1024, 768,  24, 8, 16, SEG_GRAPH } },
    { 0x119, { MM_DIRECT, 1280, 1024, 15, 8, 16, SEG_GRAPH } },
    { 0x11A, { MM_DIRECT, 1280, 1024, 16, 8, 16, SEG_GRAPH } },
    { 0x11B, { MM_DIRECT, 1280, 1024, 24, 8, 16, SEG_GRAPH } },
    { 0x11D, { MM_DIRECT, 1600, 1200, 15, 8, 16, SEG_GRAPH } },
    { 0x11E, { MM_DIRECT, 1600, 1200, 16, 8, 16, SEG_GRAPH } },
    { 0x11F, { MM_DIRECT, 1600, 1200, 24, 8, 16, SEG_GRAPH } },
    /* VESA 2.0 modes */
    { 0x141, { MM_DIRECT, 640,  400,  32, 8, 16, SEG_GRAPH } },
    { 0x142, { MM_DIRECT, 640,  480,  32, 8, 16, SEG_GRAPH } },
    { 0x143, { MM_DIRECT, 800,  600,  32, 8, 16, SEG_GRAPH } },
    { 0x144, { MM_DIRECT, 1024, 768,  32, 8, 16, SEG_GRAPH } },
    { 0x145, { MM_DIRECT, 1280, 1024, 32, 8, 16, SEG_GRAPH } },
    { 0x147, { MM_DIRECT, 1600, 1200, 32, 8, 16, SEG_GRAPH } },
    { 0x149, { MM_DIRECT, 1152, 864,  15, 8, 16, SEG_GRAPH } },
    { 0x14a, { MM_DIRECT, 1152, 864,  16, 8, 16, SEG_GRAPH } },
    { 0x14b, { MM_DIRECT, 1152, 864,  24, 8, 16, SEG_GRAPH } },
    { 0x14c, { MM_DIRECT, 1152, 864,  32, 8, 16, SEG_GRAPH } },
    { 0x175, { MM_DIRECT, 1280, 768,  16, 8, 16, SEG_GRAPH } },
    { 0x176, { MM_DIRECT, 1280, 768,  24, 8, 16, SEG_GRAPH } },
    { 0x177, { MM_DIRECT, 1280, 768,  32, 8, 16, SEG_GRAPH } },
    { 0x178, { MM_DIRECT, 1280, 800,  16, 8, 16, SEG_GRAPH } },
    { 0x179, { MM_DIRECT, 1280, 800,  24, 8, 16, SEG_GRAPH } },
    { 0x17a, { MM_DIRECT, 1280, 800,  32, 8, 16, SEG_GRAPH } },
    { 0x17b, { MM_DIRECT, 1280, 960,  16, 8, 16, SEG_GRAPH } },
    { 0x17c, { MM_DIRECT, 1280, 960,  24, 8, 16, SEG_GRAPH } },
    { 0x17d, { MM_DIRECT, 1280, 960,  32, 8, 16, SEG_GRAPH } },
    { 0x17e, { MM_DIRECT, 1440, 900,  16, 8, 16, SEG_GRAPH } },
    { 0x17f, { MM_DIRECT, 1440, 900,  24, 8, 16, SEG_GRAPH } },
    { 0x180, { MM_DIRECT, 1440, 900,  32, 8, 16, SEG_GRAPH } },
    { 0x181, { MM_DIRECT, 1400, 1050, 16, 8, 16, SEG_GRAPH } },
    { 0x182, { MM_DIRECT, 1400, 1050, 24, 8, 16, SEG_GRAPH } },
    { 0x183, { MM_DIRECT, 1400, 1050, 32, 8, 16, SEG_GRAPH } },
    { 0x184, { MM_DIRECT, 1680, 1050, 16, 8, 16, SEG_GRAPH } },
    { 0x185, { MM_DIRECT, 1680, 1050, 24, 8, 16, SEG_GRAPH } },
    { 0x186, { MM_DIRECT, 1680, 1050, 32, 8, 16, SEG_GRAPH } },
    { 0x187, { MM_DIRECT, 1920, 1200, 16, 8, 16, SEG_GRAPH } },
    { 0x188, { MM_DIRECT, 1920, 1200, 24, 8, 16, SEG_GRAPH } },
    { 0x189, { MM_DIRECT, 1920, 1200, 32, 8, 16, SEG_GRAPH } },
    { 0x18a, { MM_DIRECT, 2560, 1600, 16, 8, 16, SEG_GRAPH } },
    { 0x18b, { MM_DIRECT, 2560, 1600, 24, 8, 16, SEG_GRAPH } },
    { 0x18c, { MM_DIRECT, 2560, 1600, 32, 8, 16, SEG_GRAPH } },
    { 0x18d, { MM_DIRECT, 1280, 720,  16, 8, 16, SEG_GRAPH } },
    { 0x18e, { MM_DIRECT, 1280, 720,  24, 8, 16, SEG_GRAPH } },
    { 0x18f, { MM_DIRECT, 1280, 720,  32, 8, 16, SEG_GRAPH } },
    { 0x190, { MM_DIRECT, 1920, 1080, 16, 8, 16, SEG_GRAPH } },
    { 0x191, { MM_DIRECT, 1920, 1080, 24, 8, 16, SEG_GRAPH } },
    { 0x192, { MM_DIRECT, 1920, 1080, 32, 8, 16, SEG_GRAPH } },
};

struct vgamode_s *cbvga_find_mode(int mode)
{
    if (mode == GET_GLOBAL(CBmode))
        return &CBmodeinfo;
    if (mode == 0x03)
        return &CBemulinfo;

    int i;
    for (i = 0; i < ARRAY_SIZE(cbvesa_modes); i++) {
        struct cbvga_mode_s *cbmode_g = &cbvesa_modes[i];
        if (GET_GLOBAL(cbmode_g->mode) == 0xffff)
            continue;
        if (GET_GLOBAL(cbmode_g->mode) == mode)
            return &cbmode_g->info;
    }
    return NULL;
}

void
cbvga_list_modes(u16 seg, u16 *dest, u16 *last)
{
    int seen = 0;

    if (GET_GLOBAL(CBmode) != 0x3) {
        /* Advertise additional SVGA modes for Microsoft NTLDR graphical mode.
         * Microsoft NTLDR:
         * + Graphical mode uses a maximum resolution of 1600x1200.
         * + Expects to find VESA mode with 800x600 or 1024x768.
         * + 24 Bpp and 32 Bpp are supported
         */
        int i;
        for (i = 0; i < ARRAY_SIZE(cbvesa_modes) && dest < last; i++) {
            struct cbvga_mode_s *cbmode_g = &cbvesa_modes[i];
            u16 mode = GET_GLOBAL(cbmode_g->mode);
            if (mode == 0xffff)
                continue;
            SET_FARVAR(seg, *dest, mode);
            dest++;
            if (GET_GLOBAL(CBmode) == mode)
                seen = 1;
        }
    }
    if (dest < last && !seen) {
        SET_FARVAR(seg, *dest, GET_GLOBAL(CBmode));
        dest++;
    }
    SET_FARVAR(seg, *dest, 0xffff);
}

int
cbvga_get_window(struct vgamode_s *vmode_g, int window)
{
    return -1;
}

int
cbvga_set_window(struct vgamode_s *vmode_g, int window, int val)
{
    return -1;
}

int
cbvga_get_linelength(struct vgamode_s *vmode_g)
{
    return GET_GLOBAL(CBlinelength);
}

int
cbvga_set_linelength(struct vgamode_s *vmode_g, int val)
{
    return -1;
}

int
cbvga_get_displaystart(struct vgamode_s *vmode_g)
{
    return 0;
}

int
cbvga_set_displaystart(struct vgamode_s *vmode_g, int val)
{
    return -1;
}

int
cbvga_get_dacformat(struct vgamode_s *vmode_g)
{
    return -1;
}

int
cbvga_set_dacformat(struct vgamode_s *vmode_g, int val)
{
    return -1;
}

int
cbvga_save_restore(int cmd, u16 seg, void *data)
{
    if (cmd & (SR_HARDWARE|SR_DAC|SR_REGISTERS))
        return -1;
    return bda_save_restore(cmd, seg, data);
}

int
cbvga_set_mode(struct vgamode_s *vmode_g, int flags)
{
    u8 emul = vmode_g == &CBemulinfo || GET_GLOBAL(CBmode) == 0x03;
    /*
     * The extra_stack flag is false when running in windows x86
     * emulator, to avoid stack switching triggering bugs.  Using the
     * same flag here to skip screen clearing, because the windows
     * emulator seems to have problems to handle the int 1587 call
     * too, and GO_MEMSET uses that.
     */
    u8 extra_stack = GET_BDA_EXT(flags) & BF_EXTRA_STACK;
    MASK_BDA_EXT(flags, BF_EMULATE_TEXT, emul ? BF_EMULATE_TEXT : 0);
    if (!(flags & MF_NOCLEARMEM)) {
        if (GET_GLOBAL(CBmodeinfo.memmodel) == MM_TEXT) {
            memset16_far(SEG_CTEXT, (void*)0, 0x0720, 80*25*2);
            return 0;
        }
        if (extra_stack || flags & MF_LEGACY) {
            struct gfx_op op;
            init_gfx_op(&op, &CBmodeinfo);
            op.x = op.y = 0;
            op.xlen = GET_GLOBAL(CBmodeinfo.width);
            op.ylen = GET_GLOBAL(CBmodeinfo.height);
            op.op = GO_MEMSET;
            handle_gfx_op(&op);
        }
    }
    return 0;
}

int
cbvga_get_linesize(struct vgamode_s *vmode_g)
{
    /* Can't change mode, always report active pitch. */
    return GET_GLOBAL(CBlinelength);
}

#define CB_TAG_FRAMEBUFFER      0x0012
struct cb_framebuffer {
    u32 tag;
    u32 size;

    u64 physical_address;
    u32 x_resolution;
    u32 y_resolution;
    u32 bytes_per_line;
    u8 bits_per_pixel;
    u8 red_mask_pos;
    u8 red_mask_size;
    u8 green_mask_pos;
    u8 green_mask_size;
    u8 blue_mask_pos;
    u8 blue_mask_size;
    u8 reserved_mask_pos;
    u8 reserved_mask_size;
};

void
cbvga_setup_modes(u64 addr, u8 bpp, u32 xlines, u32 ylines, u32 linelength)
{
    int i;

    SET_VGA(CBmode, 0x140);
    SET_VGA(VBE_framebuffer, addr);
    SET_VGA(VBE_total_memory, linelength * ylines);
    SET_VGA(CBlinelength, linelength);
    SET_VGA(CBmodeinfo.memmodel, MM_DIRECT);
    SET_VGA(CBmodeinfo.width, xlines);
    SET_VGA(CBmodeinfo.height, ylines);
    SET_VGA(CBmodeinfo.depth, bpp);
    SET_VGA(CBmodeinfo.cwidth, 8);
    SET_VGA(CBmodeinfo.cheight, 16);
    memcpy_far(get_global_seg(), &CBemulinfo
               , get_global_seg(), &CBmodeinfo, sizeof(CBemulinfo));

    // Validate modes
    for (i = 0; i < ARRAY_SIZE(cbvesa_modes); i++) {
        struct cbvga_mode_s *cbmode_g = &cbvesa_modes[i];
        /* Skip VBE modes that doesn't fit into coreboot's framebuffer */
        if ((GET_GLOBAL(cbmode_g->info.height) > ylines)
            || (GET_GLOBAL(cbmode_g->info.width) > xlines)
            || (GET_GLOBAL(cbmode_g->info.depth) != bpp)) {
            dprintf(3, "Removing mode %x\n", GET_GLOBAL(cbmode_g->mode));
            SET_VGA(cbmode_g->mode, 0xffff);
        }
        if ((GET_GLOBAL(cbmode_g->info.height) == ylines)
            && (GET_GLOBAL(cbmode_g->info.width) == xlines)
            && (GET_GLOBAL(cbmode_g->info.depth) == bpp)) {
            SET_VGA(CBmode, GET_GLOBAL(cbmode_g->mode));
        }
    }
}

int
cbvga_setup(void)
{
    dprintf(1, "coreboot vga init\n");

    if (GET_GLOBAL(HaveRunInit))
        return 0;

    struct cb_header *cbh = find_cb_table();
    if (!cbh) {
        dprintf(1, "Unable to find coreboot table\n");
        return -1;
    }
    struct cb_framebuffer *cbfb = find_cb_subtable(cbh, CB_TAG_FRAMEBUFFER);
    if (!cbfb) {
        // Assume there is an EGA text framebuffer.
        dprintf(1, "Did not find coreboot framebuffer - assuming EGA text\n");
        SET_VGA(CBmode, 0x03);
        SET_VGA(CBlinelength, 80*2);
        SET_VGA(CBmodeinfo.memmodel, MM_TEXT);
        SET_VGA(CBmodeinfo.width, 80);
        SET_VGA(CBmodeinfo.height, 25);
        SET_VGA(CBmodeinfo.depth, 4);
        SET_VGA(CBmodeinfo.cwidth, 9);
        SET_VGA(CBmodeinfo.cheight, 16);
        SET_VGA(CBmodeinfo.sstart, SEG_CTEXT);
        return 0;
    }

    u64 addr = GET_FARVAR(0, cbfb->physical_address);
    u8 bpp = GET_FARVAR(0, cbfb->blue_mask_size)
             + GET_FARVAR(0, cbfb->green_mask_size)
             + GET_FARVAR(0, cbfb->red_mask_size)
             + GET_FARVAR(0, cbfb->reserved_mask_size);
    u32 xlines = GET_FARVAR(0, cbfb->x_resolution);
    u32 ylines = GET_FARVAR(0, cbfb->y_resolution);
    u32 linelength = GET_FARVAR(0, cbfb->bytes_per_line);
    //fall back to coreboot reported bpp if calculated value invalid
    if (bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32)
        bpp = GET_FARVAR(0, cbfb->bits_per_pixel);

    dprintf(1, "Found FB @ %llx %dx%d with %d bpp (%d stride)\n"
            , addr, xlines, ylines, bpp, linelength);

    if (!addr || addr > 0xffffffff
        || (bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32)) {
        dprintf(1, "Unable to use FB\n");
        return -1;
    }

    cbvga_setup_modes(addr, bpp, xlines, ylines, linelength);
    return 0;
}
