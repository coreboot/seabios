// VGA bios implementation
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2001-2008 the LGPL VGABios developers Team
//
// This file may be distributed under the terms of the GNU LGPLv3 license.


// TODO:
//  * introduce "struct vregs", or add ebp to struct bregs.
//  * define structs for save/restore state
//  * review correctness of converted asm by comparing with RBIL
//  * refactor redundant code into sub-functions
//  * See if there is a method to the in/out stuff that can be encapsulated.
//  * remove "biosfn" prefixes
//  * don't hardcode 0xc000
//  * add defs for 0xa000/0xb800
//  * verify all funcs static
//
//  * convert vbe/clext code
//
//  * separate code into separate files
//  * extract hw code from bios interfaces

#include "bregs.h" // struct bregs
#include "biosvar.h" // GET_BDA
#include "util.h" // memset
#include "vgatables.h" // vga_modes

// XXX
#define CONFIG_VBE 0
#define CONFIG_CIRRUS 0

// XXX
#define DEBUG_VGA_POST 1
#define DEBUG_VGA_10 3

#define SET_VGA(var, val) SET_FARVAR(0xc000, (var), (val))


// ===================================================================
//
// Video Utils
//
// ===================================================================

// -------------------------------------------------------------------
inline void
call16_vgaint(u32 eax, u32 ebx)
{
    asm volatile(
        "int $0x10\n"
        "cli\n"
        "cld"
        :
        : "a"(eax), "b"(ebx)
        : "cc", "memory");
}

// XXX
inline void
memcpy16_far(u16 d_seg, void *d_far, u16 s_seg, const void *s_far, size_t len)
{
    memcpy_far(d_seg, d_far, s_seg, s_far, len);
}


// ===================================================================
//
// BIOS functions
//
// ===================================================================

// -------------------------------------------------------------------
static void
biosfn_perform_gray_scale_summing(u16 start, u16 count)
{
    inb(VGAREG_ACTL_RESET);
    outb(0x00, VGAREG_ACTL_ADDRESS);

    int i;
    for (i = start; i < start+count; i++) {
        // set read address and switch to read mode
        outb(i, VGAREG_DAC_READ_ADDRESS);
        // get 6-bit wide RGB data values
        u8 r = inb(VGAREG_DAC_DATA);
        u8 g = inb(VGAREG_DAC_DATA);
        u8 b = inb(VGAREG_DAC_DATA);

        // intensity = ( 0.3 * Red ) + ( 0.59 * Green ) + ( 0.11 * Blue )
        u16 intensity = ((77 * r + 151 * g + 28 * b) + 0x80) >> 8;

        if (intensity > 0x3f)
            intensity = 0x3f;

        // set write address and switch to write mode
        outb(i, VGAREG_DAC_WRITE_ADDRESS);
        // write new intensity value
        outb(intensity & 0xff, VGAREG_DAC_DATA);
        outb(intensity & 0xff, VGAREG_DAC_DATA);
        outb(intensity & 0xff, VGAREG_DAC_DATA);
    }
    inb(VGAREG_ACTL_RESET);
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

// -------------------------------------------------------------------
static void
biosfn_set_cursor_shape(u8 CH, u8 CL)
{
    CH &= 0x3f;
    CL &= 0x1f;

    u16 curs = (CH << 8) + CL;
    SET_BDA(cursor_type, curs);

    u8 modeset_ctl = GET_BDA(modeset_ctl);
    u16 cheight = GET_BDA(char_height);
    if ((modeset_ctl & 0x01) && (cheight > 8) && (CL < 8) && (CH < 0x20)) {
        if (CL != (CH + 1))
            CH = ((CH + 1) * cheight / 8) - 1;
        else
            CH = ((CL + 1) * cheight / 8) - 2;
        CL = ((CL + 1) * cheight / 8) - 1;
    }
    // CTRC regs 0x0a and 0x0b
    u16 crtc_addr = GET_BDA(crtc_address);
    outb(0x0a, crtc_addr);
    outb(CH, crtc_addr + 1);
    outb(0x0b, crtc_addr);
    outb(CL, crtc_addr + 1);
}

// -------------------------------------------------------------------
static void
biosfn_set_cursor_pos(u8 page, u16 cursor)
{
    // Should not happen...
    if (page > 7)
        return;

    // Bios cursor pos
    SET_BDA(cursor_pos[page], cursor);

    // Set the hardware cursor
    u8 current = GET_BDA(video_page);
    if (page != current)
        return;

    // Get the dimensions
    u16 nbcols = GET_BDA(video_cols);
    u16 nbrows = GET_BDA(video_rows) + 1;

    u8 xcurs = cursor & 0x00ff;
    u8 ycurs = (cursor & 0xff00) >> 8;

    // Calculate the address knowing nbcols nbrows and page num
    u16 address = SCREEN_IO_START(nbcols, nbrows, page) + xcurs + ycurs * nbcols;

    // CRTC regs 0x0e and 0x0f
    u16 crtc_addr = GET_BDA(crtc_address);
    outb(0x0e, crtc_addr);
    outb((address & 0xff00) >> 8, crtc_addr + 1);
    outb(0x0f, crtc_addr);
    outb(address & 0x00ff, crtc_addr + 1);
}

// -------------------------------------------------------------------
static void
biosfn_get_cursor_pos(u8 page, u16 *shape, u16 *pos)
{
    // Default
    *shape = 0;
    *pos = 0;
    if (page > 7)
        return;

    // FIXME should handle VGA 14/16 lines
    *shape = GET_BDA(cursor_type);
    *pos = GET_BDA(cursor_pos[page]);
}

// -------------------------------------------------------------------
static void
biosfn_set_active_page(u8 page)
{
    if (page > 7)
        return;

    // Get the mode
    struct vgamode_s *vmode_g = find_vga_entry(GET_BDA(video_mode));
    if (!vmode_g)
        return;

    // Get pos curs pos for the right page
    u16 cursor, dummy;
    biosfn_get_cursor_pos(page, &dummy, &cursor);

    u16 address;
    if (GET_GLOBAL(vmode_g->class) == TEXT) {
        // Get the dimensions
        u16 nbcols = GET_BDA(video_cols);
        u16 nbrows = GET_BDA(video_rows) + 1;

        // Calculate the address knowing nbcols nbrows and page num
        address = SCREEN_MEM_START(nbcols, nbrows, page);
        SET_BDA(video_pagestart, address);

        // Start address
        address = SCREEN_IO_START(nbcols, nbrows, page);
    } else {
        struct VideoParam_s *vparam_g = GET_GLOBAL(vmode_g->vparam);
        address = page * GET_GLOBAL(vparam_g->slength);
    }

    // CRTC regs 0x0c and 0x0d
    u16 crtc_addr = GET_BDA(crtc_address);
    outb(0x0c, crtc_addr);
    outb((address & 0xff00) >> 8, crtc_addr + 1);
    outb(0x0d, crtc_addr);
    outb(address & 0x00ff, crtc_addr + 1);

    // And change the BIOS page
    SET_BDA(video_page, page);

    dprintf(1, "Set active page %02x address %04x\n", page, address);

    // Display the cursor, now the page is active
    biosfn_set_cursor_pos(page, cursor);
}

static void
biosfn_set_video_mode(u8 mode)
{                               // mode: Bit 7 is 1 if no clear screen
    if (CONFIG_CIRRUS)
        cirrus_set_video_mode(mode);

#ifdef VBE
    if (vbe_has_vbe_display())
        dispi_set_enable(VBE_DISPI_DISABLED);
#endif

    // The real mode
    u8 noclearmem = mode & 0x80;
    mode = mode & 0x7f;

    // find the entry in the video modes
    struct vgamode_s *vmode_g = find_vga_entry(mode);
    dprintf(1, "mode search %02x found %p\n", mode, vmode_g);
    if (!vmode_g)
        return;

    struct VideoParam_s *vparam_g = GET_GLOBAL(vmode_g->vparam);
    u16 twidth = GET_GLOBAL(vparam_g->twidth);
    u16 theightm1 = GET_GLOBAL(vparam_g->theightm1);
    u16 cheight = GET_GLOBAL(vparam_g->cheight);

    // Read the bios mode set control
    u8 modeset_ctl = GET_BDA(modeset_ctl);

    // Then we know the number of lines
// FIXME

    // if palette loading (bit 3 of modeset ctl = 0)
    if ((modeset_ctl & 0x08) == 0) {    // Set the PEL mask
        outb(GET_GLOBAL(vmode_g->pelmask), VGAREG_PEL_MASK);

        // Set the whole dac always, from 0
        outb(0x00, VGAREG_DAC_WRITE_ADDRESS);

        // From which palette
        u8 *palette_g = GET_GLOBAL(vmode_g->dac);
        u16 palsize = GET_GLOBAL(vmode_g->dacsize);
        // Always 256*3 values
        u16 i;
        for (i = 0; i < 0x0100; i++) {
            if (i <= palsize) {
                outb(GET_GLOBAL(palette_g[(i * 3) + 0]), VGAREG_DAC_DATA);
                outb(GET_GLOBAL(palette_g[(i * 3) + 1]), VGAREG_DAC_DATA);
                outb(GET_GLOBAL(palette_g[(i * 3) + 2]), VGAREG_DAC_DATA);
            } else {
                outb(0, VGAREG_DAC_DATA);
                outb(0, VGAREG_DAC_DATA);
                outb(0, VGAREG_DAC_DATA);
            }
        }
        if ((modeset_ctl & 0x02) == 0x02)
            biosfn_perform_gray_scale_summing(0x00, 0x100);
    }
    // Reset Attribute Ctl flip-flop
    inb(VGAREG_ACTL_RESET);

    // Set Attribute Ctl
    u16 i;
    for (i = 0; i <= 0x13; i++) {
        outb(i, VGAREG_ACTL_ADDRESS);
        outb(GET_GLOBAL(vparam_g->actl_regs[i]), VGAREG_ACTL_WRITE_DATA);
    }
    outb(0x14, VGAREG_ACTL_ADDRESS);
    outb(0x00, VGAREG_ACTL_WRITE_DATA);

    // Set Sequencer Ctl
    outb(0, VGAREG_SEQU_ADDRESS);
    outb(0x03, VGAREG_SEQU_DATA);
    for (i = 1; i <= 4; i++) {
        outb(i, VGAREG_SEQU_ADDRESS);
        outb(GET_GLOBAL(vparam_g->sequ_regs[i - 1]), VGAREG_SEQU_DATA);
    }

    // Set Grafx Ctl
    for (i = 0; i <= 8; i++) {
        outb(i, VGAREG_GRDC_ADDRESS);
        outb(GET_GLOBAL(vparam_g->grdc_regs[i]), VGAREG_GRDC_DATA);
    }

    // Set CRTC address VGA or MDA
    u16 crtc_addr = VGAREG_VGA_CRTC_ADDRESS;
    if (GET_GLOBAL(vmode_g->memmodel) == MTEXT)
        crtc_addr = VGAREG_MDA_CRTC_ADDRESS;

    // Disable CRTC write protection
    outw(0x0011, crtc_addr);
    // Set CRTC regs
    for (i = 0; i <= 0x18; i++) {
        outb(i, crtc_addr);
        outb(GET_GLOBAL(vparam_g->crtc_regs[i]), crtc_addr + 1);
    }

    // Set the misc register
    outb(GET_GLOBAL(vparam_g->miscreg), VGAREG_WRITE_MISC_OUTPUT);

    // Enable video
    outb(0x20, VGAREG_ACTL_ADDRESS);
    inb(VGAREG_ACTL_RESET);

    if (noclearmem == 0x00) {
        if (GET_GLOBAL(vmode_g->class) == TEXT) {
            memset16_far(GET_GLOBAL(vmode_g->sstart), 0, 0x0720, 32*1024);
        } else {
            if (mode < 0x0d) {
                memset16_far(GET_GLOBAL(vmode_g->sstart), 0, 0x0000, 32*1024);
            } else {
                outb(0x02, VGAREG_SEQU_ADDRESS);
                u8 mmask = inb(VGAREG_SEQU_DATA);
                outb(0x0f, VGAREG_SEQU_DATA);   // all planes
                memset16_far(GET_GLOBAL(vmode_g->sstart), 0, 0x0000, 64*1024);
                outb(mmask, VGAREG_SEQU_DATA);
            }
        }
    }
    // Set the BIOS mem
    SET_BDA(video_mode, mode);
    SET_BDA(video_cols, twidth);
    SET_BDA(video_pagesize, GET_GLOBAL(vparam_g->slength));
    SET_BDA(crtc_address, crtc_addr);
    SET_BDA(video_rows, theightm1);
    SET_BDA(char_height, cheight);
    SET_BDA(video_ctl, (0x60 | noclearmem));
    SET_BDA(video_switches, 0xF9);
    SET_BDA(modeset_ctl, GET_BDA(modeset_ctl) & 0x7f);

    // FIXME We nearly have the good tables. to be reworked
    SET_BDA(dcc_index, 0x08);   // 8 is VGA should be ok for now
    SET_BDA(video_savetable_ptr, (u32)video_save_pointer_table);
    SET_BDA(video_savetable_seg, 0xc000);

    // FIXME
    SET_BDA(video_msr, 0x00); // Unavailable on vanilla vga, but...
    SET_BDA(video_pal, 0x00); // Unavailable on vanilla vga, but...

    // Set cursor shape
    if (GET_GLOBAL(vmode_g->class) == TEXT)
        biosfn_set_cursor_shape(0x06, 0x07);
    // Set cursor pos for page 0..7
    for (i = 0; i < 8; i++)
        biosfn_set_cursor_pos(i, 0x0000);

    // Set active page 0
    biosfn_set_active_page(0x00);

    // Write the fonts in memory
    if (GET_GLOBAL(vmode_g->class) == TEXT) {
        call16_vgaint(0x1104, 0);
        call16_vgaint(0x1103, 0);
    }
    // Set the ints 0x1F and 0x43
    SET_IVT(0x1f, 0xC000, (u32)&vgafont8[128 * 8]);

    switch (cheight) {
    case 8:
        SET_IVT(0x43, 0xC000, (u32)vgafont8);
        break;
    case 14:
        SET_IVT(0x43, 0xC000, (u32)vgafont14);
        break;
    case 16:
        SET_IVT(0x43, 0xC000, (u32)vgafont16);
        break;
    }
}

// -------------------------------------------------------------------
static void
vgamem_copy_pl4(u8 xstart, u8 ysrc, u8 ydest, u8 cols, u8 nbcols,
                u8 cheight)
{
    u16 src = ysrc * cheight * nbcols + xstart;
    u16 dest = ydest * cheight * nbcols + xstart;
    outw(0x0105, VGAREG_GRDC_ADDRESS);
    u8 i;
    for (i = 0; i < cheight; i++)
        memcpy_far(0xa000, (void*)(dest + i * nbcols)
                   , 0xa000, (void*)(src + i * nbcols), cols);
    outw(0x0005, VGAREG_GRDC_ADDRESS);
}

// -------------------------------------------------------------------
static void
vgamem_fill_pl4(u8 xstart, u8 ystart, u8 cols, u8 nbcols, u8 cheight,
                u8 attr)
{
    u16 dest = ystart * cheight * nbcols + xstart;
    outw(0x0205, VGAREG_GRDC_ADDRESS);
    u8 i;
    for (i = 0; i < cheight; i++)
        memset_far(0xa000, (void*)(dest + i * nbcols), attr, cols);
    outw(0x0005, VGAREG_GRDC_ADDRESS);
}

// -------------------------------------------------------------------
static void
vgamem_copy_cga(u8 xstart, u8 ysrc, u8 ydest, u8 cols, u8 nbcols,
                u8 cheight)
{
    u16 src = ((ysrc * cheight * nbcols) >> 1) + xstart;
    u16 dest = ((ydest * cheight * nbcols) >> 1) + xstart;
    u8 i;
    for (i = 0; i < cheight; i++)
        if (i & 1)
            memcpy_far(0xb800, (void*)(0x2000 + dest + (i >> 1) * nbcols)
                       , 0xb800, (void*)(0x2000 + src + (i >> 1) * nbcols)
                       , cols);
        else
            memcpy_far(0xb800, (void*)(dest + (i >> 1) * nbcols)
                       , 0xb800, (void*)(src + (i >> 1) * nbcols), cols);
}

// -------------------------------------------------------------------
static void
vgamem_fill_cga(u8 xstart, u8 ystart, u8 cols, u8 nbcols, u8 cheight,
                u8 attr)
{
    u16 dest = ((ystart * cheight * nbcols) >> 1) + xstart;
    u8 i;
    for (i = 0; i < cheight; i++)
        if (i & 1)
            memset_far(0xb800, (void*)(0x2000 + dest + (i >> 1) * nbcols)
                       , attr, cols);
        else
            memset_far(0xb800, (void*)(dest + (i >> 1) * nbcols), attr, cols);
}

// -------------------------------------------------------------------
static void
biosfn_scroll(u8 nblines, u8 attr, u8 rul, u8 cul, u8 rlr, u8 clr, u8 page,
              u8 dir)
{
    // page == 0xFF if current
    if (rul > rlr)
        return;
    if (cul > clr)
        return;

    // Get the mode
    struct vgamode_s *vmode_g = find_vga_entry(GET_BDA(video_mode));
    if (!vmode_g)
        return;

    // Get the dimensions
    u16 nbrows = GET_BDA(video_rows) + 1;
    u16 nbcols = GET_BDA(video_cols);

    // Get the current page
    if (page == 0xFF)
        page = GET_BDA(video_page);

    if (rlr >= nbrows)
        rlr = nbrows - 1;
    if (clr >= nbcols)
        clr = nbcols - 1;
    if (nblines > nbrows)
        nblines = 0;
    u8 cols = clr - cul + 1;

    if (GET_GLOBAL(vmode_g->class) == TEXT) {
        // Compute the address
        void *address = (void*)(SCREEN_MEM_START(nbcols, nbrows, page));
        dprintf(3, "Scroll, address %p (%d %d %02x)\n"
                , address, nbrows, nbcols, page);

        if (nblines == 0 && rul == 0 && cul == 0 && rlr == nbrows - 1
            && clr == nbcols - 1) {
            memset16_far(GET_GLOBAL(vmode_g->sstart), address
                         , (u16)attr * 0x100 + ' ', nbrows * nbcols * 2);
        } else {                // if Scroll up
            if (dir == SCROLL_UP) {
                u16 i;
                for (i = rul; i <= rlr; i++)
                    if ((i + nblines > rlr) || (nblines == 0))
                        memset16_far(GET_GLOBAL(vmode_g->sstart)
                                     , address + (i * nbcols + cul) * 2
                                     , (u16)attr * 0x100 + ' ', cols * 2);
                    else
                        memcpy16_far(GET_GLOBAL(vmode_g->sstart)
                                     , address + (i * nbcols + cul) * 2
                                     , GET_GLOBAL(vmode_g->sstart)
                                     , (void*)(((i + nblines) * nbcols + cul) * 2)
                                     , cols * 2);
            } else {
                u16 i;
                for (i = rlr; i >= rul; i--) {
                    if ((i < rul + nblines) || (nblines == 0))
                        memset16_far(GET_GLOBAL(vmode_g->sstart)
                                     , address + (i * nbcols + cul) * 2
                                     , (u16)attr * 0x100 + ' ', cols * 2);
                    else
                        memcpy16_far(GET_GLOBAL(vmode_g->sstart)
                                     , address + (i * nbcols + cul) * 2
                                     , GET_GLOBAL(vmode_g->sstart)
                                     , (void*)(((i - nblines) * nbcols + cul) * 2)
                                     , cols * 2);
                    if (i > rlr)
                        break;
                }
            }
        }
        return;
    }

    // FIXME gfx mode not complete
    struct VideoParam_s *vparam_g = GET_GLOBAL(vmode_g->vparam);
    u8 cheight = GET_GLOBAL(vparam_g->cheight);
    switch (GET_GLOBAL(vmode_g->memmodel)) {
    case PLANAR4:
    case PLANAR1:
        if (nblines == 0 && rul == 0 && cul == 0 && rlr == nbrows - 1
            && clr == nbcols - 1) {
            outw(0x0205, VGAREG_GRDC_ADDRESS);
            memset_far(GET_GLOBAL(vmode_g->sstart), 0, attr,
                       nbrows * nbcols * cheight);
            outw(0x0005, VGAREG_GRDC_ADDRESS);
        } else {            // if Scroll up
            if (dir == SCROLL_UP) {
                u16 i;
                for (i = rul; i <= rlr; i++)
                    if ((i + nblines > rlr) || (nblines == 0))
                        vgamem_fill_pl4(cul, i, cols, nbcols, cheight,
                                        attr);
                    else
                        vgamem_copy_pl4(cul, i + nblines, i, cols,
                                        nbcols, cheight);
            } else {
                u16 i;
                for (i = rlr; i >= rul; i--) {
                    if ((i < rul + nblines) || (nblines == 0))
                        vgamem_fill_pl4(cul, i, cols, nbcols, cheight,
                                        attr);
                    else
                        vgamem_copy_pl4(cul, i, i - nblines, cols,
                                        nbcols, cheight);
                    if (i > rlr)
                        break;
                }
            }
        }
        break;
    case CGA: {
        u8 bpp = GET_GLOBAL(vmode_g->pixbits);
        if (nblines == 0 && rul == 0 && cul == 0 && rlr == nbrows - 1
            && clr == nbcols - 1) {
            memset_far(GET_GLOBAL(vmode_g->sstart), 0, attr,
                       nbrows * nbcols * cheight * bpp);
        } else {
            if (bpp == 2) {
                cul <<= 1;
                cols <<= 1;
                nbcols <<= 1;
            }
            // if Scroll up
            if (dir == SCROLL_UP) {
                u16 i;
                for (i = rul; i <= rlr; i++)
                    if ((i + nblines > rlr) || (nblines == 0))
                        vgamem_fill_cga(cul, i, cols, nbcols, cheight,
                                        attr);
                    else
                        vgamem_copy_cga(cul, i + nblines, i, cols,
                                        nbcols, cheight);
            } else {
                u16 i;
                for (i = rlr; i >= rul; i--) {
                    if ((i < rul + nblines) || (nblines == 0))
                        vgamem_fill_cga(cul, i, cols, nbcols, cheight,
                                        attr);
                    else
                        vgamem_copy_cga(cul, i, i - nblines, cols,
                                        nbcols, cheight);
                    if (i > rlr)
                        break;
                }
            }
        }
        break;
    }
    default:
        dprintf(1, "Scroll in graphics mode\n");
    }
}

// -------------------------------------------------------------------
static void
biosfn_read_char_attr(u8 page, u16 *car)
{
    // Get the mode
    struct vgamode_s *vmode_g = find_vga_entry(GET_BDA(video_mode));
    if (!vmode_g)
        return;

    // Get the cursor pos for the page
    u16 cursor, dummy;
    biosfn_get_cursor_pos(page, &dummy, &cursor);
    u8 xcurs = cursor & 0x00ff;
    u8 ycurs = (cursor & 0xff00) >> 8;

    // Get the dimensions
    u16 nbrows = GET_BDA(video_rows) + 1;
    u16 nbcols = GET_BDA(video_cols);

    if (GET_GLOBAL(vmode_g->class) == TEXT) {
        // Compute the address
        u16 *address_far = (void*)(SCREEN_MEM_START(nbcols, nbrows, page)
                                   + (xcurs + ycurs * nbcols) * 2);

        *car = GET_FARVAR(GET_GLOBAL(vmode_g->sstart), *address_far);
    } else {
        // FIXME gfx mode
        dprintf(1, "Read char in graphics mode\n");
    }
}

// -------------------------------------------------------------------
static void
write_gfx_char_pl4(u8 car, u8 attr, u8 xcurs, u8 ycurs, u8 nbcols,
                   u8 cheight)
{
    u8 *fdata_g;
    switch (cheight) {
    case 14:
        fdata_g = vgafont14;
        break;
    case 16:
        fdata_g = vgafont16;
        break;
    default:
        fdata_g = vgafont8;
    }
    u16 addr = xcurs + ycurs * cheight * nbcols;
    u16 src = car * cheight;
    outw(0x0f02, VGAREG_SEQU_ADDRESS);
    outw(0x0205, VGAREG_GRDC_ADDRESS);
    if (attr & 0x80)
        outw(0x1803, VGAREG_GRDC_ADDRESS);
    else
        outw(0x0003, VGAREG_GRDC_ADDRESS);
    u8 i;
    for (i = 0; i < cheight; i++) {
        u8 *dest_far = (void*)(addr + i * nbcols);
        u8 j;
        for (j = 0; j < 8; j++) {
            u8 mask = 0x80 >> j;
            outw((mask << 8) | 0x08, VGAREG_GRDC_ADDRESS);
            GET_FARVAR(0xa000, *dest_far);
            if (GET_GLOBAL(fdata_g[src + i]) & mask)
                SET_FARVAR(0xa000, *dest_far, attr & 0x0f);
            else
                SET_FARVAR(0xa000, *dest_far, 0x00);
        }
    }
    outw(0xff08, VGAREG_GRDC_ADDRESS);
    outw(0x0005, VGAREG_GRDC_ADDRESS);
    outw(0x0003, VGAREG_GRDC_ADDRESS);
}

// -------------------------------------------------------------------
static void
write_gfx_char_cga(u8 car, u8 attr, u8 xcurs, u8 ycurs, u8 nbcols, u8 bpp)
{
    u8 *fdata_g = vgafont8;
    u16 addr = (xcurs * bpp) + ycurs * 320;
    u16 src = car * 8;
    u8 i;
    for (i = 0; i < 8; i++) {
        u8 *dest_far = (void*)(addr + (i >> 1) * 80);
        if (i & 1)
            dest_far += 0x2000;
        u8 mask = 0x80;
        if (bpp == 1) {
            u8 data = 0;
            if (attr & 0x80)
                data = GET_FARVAR(0xb800, *dest_far);
            u8 j;
            for (j = 0; j < 8; j++) {
                if (GET_GLOBAL(fdata_g[src + i]) & mask) {
                    if (attr & 0x80)
                        data ^= (attr & 0x01) << (7 - j);
                    else
                        data |= (attr & 0x01) << (7 - j);
                }
                mask >>= 1;
            }
            SET_FARVAR(0xb800, *dest_far, data);
        } else {
            while (mask > 0) {
                u8 data = 0;
                if (attr & 0x80)
                    data = GET_FARVAR(0xb800, *dest_far);
                u8 j;
                for (j = 0; j < 4; j++) {
                    if (GET_GLOBAL(fdata_g[src + i]) & mask) {
                        if (attr & 0x80)
                            data ^= (attr & 0x03) << ((3 - j) * 2);
                        else
                            data |= (attr & 0x03) << ((3 - j) * 2);
                    }
                    mask >>= 1;
                }
                SET_FARVAR(0xb800, *dest_far, data);
                dest_far += 1;
            }
        }
    }
}

// -------------------------------------------------------------------
static void
write_gfx_char_lin(u8 car, u8 attr, u8 xcurs, u8 ycurs, u8 nbcols)
{
    u8 *fdata_g = vgafont8;
    u16 addr = xcurs * 8 + ycurs * nbcols * 64;
    u16 src = car * 8;
    u8 i;
    for (i = 0; i < 8; i++) {
        u8 *dest_far = (void*)(addr + i * nbcols * 8);
        u8 mask = 0x80;
        u8 j;
        for (j = 0; j < 8; j++) {
            u8 data = 0x00;
            if (GET_GLOBAL(fdata_g[src + i]) & mask)
                data = attr;
            SET_FARVAR(0xa000, dest_far[j], data);
            mask >>= 1;
        }
    }
}

// -------------------------------------------------------------------
static void
biosfn_write_char_attr(u8 car, u8 page, u8 attr, u16 count)
{
    // Get the mode
    struct vgamode_s *vmode_g = find_vga_entry(GET_BDA(video_mode));
    if (!vmode_g)
        return;

    // Get the cursor pos for the page
    u16 cursor, dummy;
    biosfn_get_cursor_pos(page, &dummy, &cursor);
    u8 xcurs = cursor & 0x00ff;
    u8 ycurs = (cursor & 0xff00) >> 8;

    // Get the dimensions
    u16 nbrows = GET_BDA(video_rows) + 1;
    u16 nbcols = GET_BDA(video_cols);

    if (GET_GLOBAL(vmode_g->class) == TEXT) {
        // Compute the address
        void *address = (void*)(SCREEN_MEM_START(nbcols, nbrows, page)
                                + (xcurs + ycurs * nbcols) * 2);

        dummy = ((u16)attr << 8) + car;
        memset16_far(GET_GLOBAL(vmode_g->sstart), address, dummy, count * 2);
        return;
    }

    // FIXME gfx mode not complete
    struct VideoParam_s *vparam_g = GET_GLOBAL(vmode_g->vparam);
    u8 cheight = GET_GLOBAL(vparam_g->cheight);
    u8 bpp = GET_GLOBAL(vmode_g->pixbits);
    while ((count-- > 0) && (xcurs < nbcols)) {
        switch (GET_GLOBAL(vmode_g->memmodel)) {
        case PLANAR4:
        case PLANAR1:
            write_gfx_char_pl4(car, attr, xcurs, ycurs, nbcols,
                               cheight);
            break;
        case CGA:
            write_gfx_char_cga(car, attr, xcurs, ycurs, nbcols, bpp);
            break;
        case LINEAR8:
            write_gfx_char_lin(car, attr, xcurs, ycurs, nbcols);
            break;
        }
        xcurs++;
    }
}

// -------------------------------------------------------------------
static void
biosfn_write_char_only(u8 car, u8 page, u8 attr, u16 count)
{
    // Get the mode
    struct vgamode_s *vmode_g = find_vga_entry(GET_BDA(video_mode));
    if (!vmode_g)
        return;

    // Get the cursor pos for the page
    u16 cursor, dummy;
    biosfn_get_cursor_pos(page, &dummy, &cursor);
    u8 xcurs = cursor & 0x00ff;
    u8 ycurs = (cursor & 0xff00) >> 8;

    // Get the dimensions
    u16 nbrows = GET_BDA(video_rows) + 1;
    u16 nbcols = GET_BDA(video_cols);

    if (GET_GLOBAL(vmode_g->class) == TEXT) {
        // Compute the address
        u8 *address_far = (void*)(SCREEN_MEM_START(nbcols, nbrows, page)
                                  + (xcurs + ycurs * nbcols) * 2);
        while (count-- > 0) {
            SET_FARVAR(GET_GLOBAL(vmode_g->sstart), *address_far, car);
            address_far += 2;
        }
        return;
    }

    // FIXME gfx mode not complete
    struct VideoParam_s *vparam_g = GET_GLOBAL(vmode_g->vparam);
    u8 cheight = GET_GLOBAL(vparam_g->cheight);
    u8 bpp = GET_GLOBAL(vmode_g->pixbits);
    while ((count-- > 0) && (xcurs < nbcols)) {
        switch (GET_GLOBAL(vmode_g->memmodel)) {
        case PLANAR4:
        case PLANAR1:
            write_gfx_char_pl4(car, attr, xcurs, ycurs, nbcols,
                               cheight);
            break;
        case CGA:
            write_gfx_char_cga(car, attr, xcurs, ycurs, nbcols, bpp);
            break;
        case LINEAR8:
            write_gfx_char_lin(car, attr, xcurs, ycurs, nbcols);
            break;
        }
        xcurs++;
    }
}

// -------------------------------------------------------------------
static void
biosfn_set_border_color(struct bregs *regs)
{
    inb(VGAREG_ACTL_RESET);
    outb(0x00, VGAREG_ACTL_ADDRESS);
    u8 al = regs->bl & 0x0f;
    if (al & 0x08)
        al += 0x08;
    outb(al, VGAREG_ACTL_WRITE_DATA);
    u8 bl = regs->bl & 0x10;

    int i;
    for (i = 1; i < 4; i++) {
        outb(i, VGAREG_ACTL_ADDRESS);

        al = inb(VGAREG_ACTL_READ_DATA);
        al &= 0xef;
        al |= bl;
        outb(al, VGAREG_ACTL_WRITE_DATA);
    }
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

static void
biosfn_set_palette(struct bregs *regs)
{
    inb(VGAREG_ACTL_RESET);
    u8 bl = regs->bl & 0x01;
    int i;
    for (i = 1; i < 4; i++) {
        outb(i, VGAREG_ACTL_ADDRESS);

        u8 al = inb(VGAREG_ACTL_READ_DATA);
        al &= 0xfe;
        al |= bl;
        outb(al, VGAREG_ACTL_WRITE_DATA);
    }
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

// -------------------------------------------------------------------
static void
biosfn_write_pixel(u8 BH, u8 AL, u16 CX, u16 DX)
{
    // Get the mode
    struct vgamode_s *vmode_g = find_vga_entry(GET_BDA(video_mode));
    if (!vmode_g)
        return;
    if (GET_GLOBAL(vmode_g->class) == TEXT)
        return;

    u8 *addr_far, mask, attr, data;
    switch (GET_GLOBAL(vmode_g->memmodel)) {
    case PLANAR4:
    case PLANAR1:
        addr_far = (void*)(CX / 8 + DX * GET_BDA(video_cols));
        mask = 0x80 >> (CX & 0x07);
        outw((mask << 8) | 0x08, VGAREG_GRDC_ADDRESS);
        outw(0x0205, VGAREG_GRDC_ADDRESS);
        data = GET_FARVAR(0xa000, *addr_far);
        if (AL & 0x80)
            outw(0x1803, VGAREG_GRDC_ADDRESS);
        SET_FARVAR(0xa000, *addr_far, AL);
        outw(0xff08, VGAREG_GRDC_ADDRESS);
        outw(0x0005, VGAREG_GRDC_ADDRESS);
        outw(0x0003, VGAREG_GRDC_ADDRESS);
        break;
    case CGA:
        if (GET_GLOBAL(vmode_g->pixbits) == 2)
            addr_far = (void*)((CX >> 2) + (DX >> 1) * 80);
        else
            addr_far = (void*)((CX >> 3) + (DX >> 1) * 80);
        if (DX & 1)
            addr_far += 0x2000;
        data = GET_FARVAR(0xb800, *addr_far);
        if (GET_GLOBAL(vmode_g->pixbits) == 2) {
            attr = (AL & 0x03) << ((3 - (CX & 0x03)) * 2);
            mask = 0x03 << ((3 - (CX & 0x03)) * 2);
        } else {
            attr = (AL & 0x01) << (7 - (CX & 0x07));
            mask = 0x01 << (7 - (CX & 0x07));
        }
        if (AL & 0x80) {
            data ^= attr;
        } else {
            data &= ~mask;
            data |= attr;
        }
        SET_FARVAR(0xb800, *addr_far, data);
        break;
    case LINEAR8:
        addr_far = (void*)(CX + DX * (GET_BDA(video_cols) * 8));
        SET_FARVAR(0xa000, *addr_far, AL);
        break;
    }
}

// -------------------------------------------------------------------
static void
biosfn_read_pixel(u8 BH, u16 CX, u16 DX, u16 *AX)
{
    // Get the mode
    struct vgamode_s *vmode_g = find_vga_entry(GET_BDA(video_mode));
    if (!vmode_g)
        return;
    if (GET_GLOBAL(vmode_g->class) == TEXT)
        return;

    u8 *addr_far, mask, attr=0, data, i;
    switch (GET_GLOBAL(vmode_g->memmodel)) {
    case PLANAR4:
    case PLANAR1:
        addr_far = (void*)(CX / 8 + DX * GET_BDA(video_cols));
        mask = 0x80 >> (CX & 0x07);
        attr = 0x00;
        for (i = 0; i < 4; i++) {
            outw((i << 8) | 0x04, VGAREG_GRDC_ADDRESS);
            data = GET_FARVAR(0xa000, *addr_far) & mask;
            if (data > 0)
                attr |= (0x01 << i);
        }
        break;
    case CGA:
        addr_far = (void*)((CX >> 2) + (DX >> 1) * 80);
        if (DX & 1)
            addr_far += 0x2000;
        data = GET_FARVAR(0xb800, *addr_far);
        if (GET_GLOBAL(vmode_g->pixbits) == 2)
            attr = (data >> ((3 - (CX & 0x03)) * 2)) & 0x03;
        else
            attr = (data >> (7 - (CX & 0x07))) & 0x01;
        break;
    case LINEAR8:
        addr_far = (void*)(CX + DX * (GET_BDA(video_cols) * 8));
        attr = GET_FARVAR(0xa000, *addr_far);
        break;
    }
    *AX = (*AX & 0xff00) | attr;
}

// -------------------------------------------------------------------
static void
biosfn_write_teletype(u8 car, u8 page, u8 attr, u8 flag)
{                               // flag = WITH_ATTR / NO_ATTR
    // special case if page is 0xff, use current page
    if (page == 0xff)
        page = GET_BDA(video_page);

    // Get the mode
    struct vgamode_s *vmode_g = find_vga_entry(GET_BDA(video_mode));
    if (!vmode_g)
        return;

    // Get the cursor pos for the page
    u16 cursor, dummy;
    biosfn_get_cursor_pos(page, &dummy, &cursor);
    u8 xcurs = cursor & 0x00ff;
    u8 ycurs = (cursor & 0xff00) >> 8;

    // Get the dimensions
    u16 nbrows = GET_BDA(video_rows) + 1;
    u16 nbcols = GET_BDA(video_cols);

    switch (car) {
    case 7:
        //FIXME should beep
        break;

    case 8:
        if (xcurs > 0)
            xcurs--;
        break;

    case '\r':
        xcurs = 0;
        break;

    case '\n':
        ycurs++;
        break;

    case '\t':
        do {
            biosfn_write_teletype(' ', page, attr, flag);
            biosfn_get_cursor_pos(page, &dummy, &cursor);
            xcurs = cursor & 0x00ff;
            ycurs = (cursor & 0xff00) >> 8;
        } while (xcurs % 8 == 0);
        break;

    default:

        if (GET_GLOBAL(vmode_g->class) == TEXT) {
            // Compute the address
            u8 *address_far = (void*)(SCREEN_MEM_START(nbcols, nbrows, page)
                                      + (xcurs + ycurs * nbcols) * 2);
            // Write the char
            SET_FARVAR(GET_GLOBAL(vmode_g->sstart), address_far[0], car);
            if (flag == WITH_ATTR)
                SET_FARVAR(GET_GLOBAL(vmode_g->sstart), address_far[1], attr);
        } else {
            // FIXME gfx mode not complete
            struct VideoParam_s *vparam_g = GET_GLOBAL(vmode_g->vparam);
            u8 cheight = GET_GLOBAL(vparam_g->cheight);
            u8 bpp = GET_GLOBAL(vmode_g->pixbits);
            switch (GET_GLOBAL(vmode_g->memmodel)) {
            case PLANAR4:
            case PLANAR1:
                write_gfx_char_pl4(car, attr, xcurs, ycurs, nbcols, cheight);
                break;
            case CGA:
                write_gfx_char_cga(car, attr, xcurs, ycurs, nbcols, bpp);
                break;
            case LINEAR8:
                write_gfx_char_lin(car, attr, xcurs, ycurs, nbcols);
                break;
            }
        }
        xcurs++;
    }

    // Do we need to wrap ?
    if (xcurs == nbcols) {
        xcurs = 0;
        ycurs++;
    }
    // Do we need to scroll ?
    if (ycurs == nbrows) {
        if (GET_GLOBAL(vmode_g->class) == TEXT)
            biosfn_scroll(0x01, 0x07, 0, 0, nbrows - 1, nbcols - 1, page,
                          SCROLL_UP);
        else
            biosfn_scroll(0x01, 0x00, 0, 0, nbrows - 1, nbcols - 1, page,
                          SCROLL_UP);
        ycurs -= 1;
    }
    // Set the cursor for the page
    cursor = ycurs;
    cursor <<= 8;
    cursor += xcurs;
    biosfn_set_cursor_pos(page, cursor);
}

// -------------------------------------------------------------------
static void
biosfn_get_video_mode(struct bregs *regs)
{
    regs->bh = GET_BDA(video_page);
    regs->al = GET_BDA(video_mode) | (GET_BDA(video_ctl) & 0x80);
    regs->ah = GET_BDA(video_cols);
}

// -------------------------------------------------------------------
static void
biosfn_set_overscan_border_color(struct bregs *regs)
{
    inb(VGAREG_ACTL_RESET);
    outb(0x11, VGAREG_ACTL_ADDRESS);
    outb(regs->bh, VGAREG_ACTL_WRITE_DATA);
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

// -------------------------------------------------------------------
static void
biosfn_set_all_palette_reg(struct bregs *regs)
{
    inb(VGAREG_ACTL_RESET);

    u8 *data_far = (u8*)(regs->dx + 0);
    int i;
    for (i = 0; i < 0x10; i++) {
        outb(i, VGAREG_ACTL_ADDRESS);
        u8 val = GET_FARVAR(regs->es, *data_far);
        outb(val, VGAREG_ACTL_WRITE_DATA);
        data_far++;
    }
    outb(0x11, VGAREG_ACTL_ADDRESS);
    outb(GET_FARVAR(regs->es, *data_far), VGAREG_ACTL_WRITE_DATA);
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

// -------------------------------------------------------------------
static void
biosfn_toggle_intensity(struct bregs *regs)
{
    inb(VGAREG_ACTL_RESET);
    outb(0x10, VGAREG_ACTL_ADDRESS);
    u8 val = (inb(VGAREG_ACTL_READ_DATA) & 0x7f) | ((regs->bl & 0x01) << 3);
    outb(val, VGAREG_ACTL_WRITE_DATA);
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

// -------------------------------------------------------------------
void
biosfn_set_single_palette_reg(u8 reg, u8 val)
{
    inb(VGAREG_ACTL_RESET);
    outb(reg, VGAREG_ACTL_ADDRESS);
    outb(val, VGAREG_ACTL_WRITE_DATA);
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

// -------------------------------------------------------------------
u8
biosfn_get_single_palette_reg(u8 reg)
{
    inb(VGAREG_ACTL_RESET);
    outb(reg, VGAREG_ACTL_ADDRESS);
    u8 v = inb(VGAREG_ACTL_READ_DATA);
    inb(VGAREG_ACTL_RESET);
    outb(0x20, VGAREG_ACTL_ADDRESS);
    return v;
}

// -------------------------------------------------------------------
static void
biosfn_read_overscan_border_color(struct bregs *regs)
{
    inb(VGAREG_ACTL_RESET);
    outb(0x11, VGAREG_ACTL_ADDRESS);
    regs->bh = inb(VGAREG_ACTL_READ_DATA);
    inb(VGAREG_ACTL_RESET);
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

// -------------------------------------------------------------------
static void
biosfn_get_all_palette_reg(struct bregs *regs)
{
    u8 *data_far = (u8*)(regs->dx + 0);
    int i;
    for (i = 0; i < 0x10; i++) {
        inb(VGAREG_ACTL_RESET);
        outb(i, VGAREG_ACTL_ADDRESS);
        SET_FARVAR(regs->es, *data_far, inb(VGAREG_ACTL_READ_DATA));
        data_far++;
    }
    inb(VGAREG_ACTL_RESET);
    outb(0x11, VGAREG_ACTL_ADDRESS);
    SET_FARVAR(regs->es, *data_far, inb(VGAREG_ACTL_READ_DATA));
    inb(VGAREG_ACTL_RESET);
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

// -------------------------------------------------------------------
static void
biosfn_set_single_dac_reg(struct bregs *regs)
{
    outb(regs->bl, VGAREG_DAC_WRITE_ADDRESS);
    outb(regs->dh, VGAREG_DAC_DATA);
    outb(regs->ch, VGAREG_DAC_DATA);
    outb(regs->cl, VGAREG_DAC_DATA);
}

// -------------------------------------------------------------------
static void
biosfn_set_all_dac_reg(struct bregs *regs)
{
    outb(regs->bl, VGAREG_DAC_WRITE_ADDRESS);
    u8 *data_far = (u8*)(regs->dx + 0);
    int count = regs->cx;
    while (count) {
        outb(GET_FARVAR(regs->es, *data_far), VGAREG_DAC_DATA);
        data_far++;
        outb(GET_FARVAR(regs->es, *data_far), VGAREG_DAC_DATA);
        data_far++;
        outb(GET_FARVAR(regs->es, *data_far), VGAREG_DAC_DATA);
        data_far++;
        count--;
    }
}

// -------------------------------------------------------------------
static void
biosfn_select_video_dac_color_page(struct bregs *regs)
{
    inb(VGAREG_ACTL_RESET);
    outb(0x10, VGAREG_ACTL_ADDRESS);
    u8 val = inb(VGAREG_ACTL_READ_DATA);
    if (!(regs->bl & 0x01)) {
        val = (val & 0x7f) | (regs->bh << 7);
        outb(val, VGAREG_ACTL_WRITE_DATA);
        outb(0x20, VGAREG_ACTL_ADDRESS);
        return;
    }
    inb(VGAREG_ACTL_RESET);
    outb(0x14, VGAREG_ACTL_ADDRESS);
    u8 bh = regs->bh;
    if (!(val & 0x80))
        bh <<= 2;
    bh &= 0x0f;
    outb(bh, VGAREG_ACTL_WRITE_DATA);
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

// -------------------------------------------------------------------
static void
biosfn_read_single_dac_reg(struct bregs *regs)
{
    outb(regs->bl, VGAREG_DAC_READ_ADDRESS);
    regs->dh = inb(VGAREG_DAC_DATA);
    regs->ch = inb(VGAREG_DAC_DATA);
    regs->cl = inb(VGAREG_DAC_DATA);
}

// -------------------------------------------------------------------
static void
biosfn_read_all_dac_reg(struct bregs *regs)
{
    outb(regs->bl, VGAREG_DAC_READ_ADDRESS);
    u8 *data_far = (u8*)(regs->dx + 0);
    int count = regs->cx;
    while (count) {
        SET_FARVAR(regs->es, *data_far, inb(VGAREG_DAC_DATA));
        data_far++;
        SET_FARVAR(regs->es, *data_far, inb(VGAREG_DAC_DATA));
        data_far++;
        SET_FARVAR(regs->es, *data_far, inb(VGAREG_DAC_DATA));
        data_far++;
        count--;
    }
}

// -------------------------------------------------------------------
static void
biosfn_set_pel_mask(struct bregs *regs)
{
    outb(regs->bl, VGAREG_PEL_MASK);
}

// -------------------------------------------------------------------
static void
biosfn_read_pel_mask(struct bregs *regs)
{
    regs->bl = inb(VGAREG_PEL_MASK);
}

// -------------------------------------------------------------------
static void
biosfn_read_video_dac_state(struct bregs *regs)
{
    inb(VGAREG_ACTL_RESET);
    outb(0x10, VGAREG_ACTL_ADDRESS);
    u8 val1 = inb(VGAREG_ACTL_READ_DATA) >> 7;

    inb(VGAREG_ACTL_RESET);
    outb(0x14, VGAREG_ACTL_ADDRESS);
    u8 val2 = inb(VGAREG_ACTL_READ_DATA) & 0x0f;
    if (!(val1 & 0x01))
        val2 >>= 2;

    inb(VGAREG_ACTL_RESET);
    outb(0x20, VGAREG_ACTL_ADDRESS);

    regs->bl = val1;
    regs->bh = val2;
}

// -------------------------------------------------------------------
static void
get_font_access()
{
    outw(0x0100, VGAREG_SEQU_ADDRESS);
    outw(0x0402, VGAREG_SEQU_ADDRESS);
    outw(0x0704, VGAREG_SEQU_ADDRESS);
    outw(0x0300, VGAREG_SEQU_ADDRESS);
    outw(0x0204, VGAREG_GRDC_ADDRESS);
    outw(0x0005, VGAREG_GRDC_ADDRESS);
    outw(0x0406, VGAREG_GRDC_ADDRESS);
}

static void
release_font_access()
{
    outw(0x0100, VGAREG_SEQU_ADDRESS);
    outw(0x0302, VGAREG_SEQU_ADDRESS);
    outw(0x0304, VGAREG_SEQU_ADDRESS);
    outw(0x0300, VGAREG_SEQU_ADDRESS);
    u16 v = inw(VGAREG_READ_MISC_OUTPUT);
    v = ((v & 0x01) << 10) | 0x0a06;
    outw(v, VGAREG_GRDC_ADDRESS);
    outw(0x0004, VGAREG_GRDC_ADDRESS);
    outw(0x1005, VGAREG_GRDC_ADDRESS);
}

static void
set_scan_lines(u8 lines)
{
    u16 crtc_addr = GET_BDA(crtc_address);
    outb(0x09, crtc_addr);
    u8 crtc_r9 = inb(crtc_addr + 1);
    crtc_r9 = (crtc_r9 & 0xe0) | (lines - 1);
    outb(crtc_r9, crtc_addr + 1);
    if (lines == 8)
        biosfn_set_cursor_shape(0x06, 0x07);
    else
        biosfn_set_cursor_shape(lines - 4, lines - 3);
    SET_BDA(char_height, lines);
    outb(0x12, crtc_addr);
    u16 vde = inb(crtc_addr + 1);
    outb(0x07, crtc_addr);
    u8 ovl = inb(crtc_addr + 1);
    vde += (((ovl & 0x02) << 7) + ((ovl & 0x40) << 3) + 1);
    u8 rows = vde / lines;
    SET_BDA(video_rows, rows - 1);
    u16 cols = GET_BDA(video_cols);
    SET_BDA(video_pagesize, rows * cols * 2);
}

static void
biosfn_load_text_user_pat(u8 AL, u16 ES, u16 BP, u16 CX, u16 DX, u8 BL,
                          u8 BH)
{
    get_font_access();
    u16 blockaddr = ((BL & 0x03) << 14) + ((BL & 0x04) << 11);
    u16 i;
    for (i = 0; i < CX; i++) {
        void *src = (void*)(BP + i * BH);
        void *dest = (void*)(blockaddr + (DX + i) * 32);
        memcpy_far(0xA000, dest, ES, src, BH);
    }
    release_font_access();
    if (AL >= 0x10)
        set_scan_lines(BH);
}

static void
biosfn_load_text_8_14_pat(u8 AL, u8 BL)
{
    get_font_access();
    u16 blockaddr = ((BL & 0x03) << 14) + ((BL & 0x04) << 11);
    u16 i;
    for (i = 0; i < 0x100; i++) {
        u16 src = i * 14;
        void *dest = (void*)(blockaddr + i * 32);
        memcpy_far(0xA000, dest, 0xC000, &vgafont14[src], 14);
    }
    release_font_access();
    if (AL >= 0x10)
        set_scan_lines(14);
}

static void
biosfn_load_text_8_8_pat(u8 AL, u8 BL)
{
    get_font_access();
    u16 blockaddr = ((BL & 0x03) << 14) + ((BL & 0x04) << 11);
    u16 i;
    for (i = 0; i < 0x100; i++) {
        u16 src = i * 8;
        void *dest = (void*)(blockaddr + i * 32);
        memcpy_far(0xA000, dest, 0xC000, &vgafont8[src], 8);
    }
    release_font_access();
    if (AL >= 0x10)
        set_scan_lines(8);
}

// -------------------------------------------------------------------
static void
biosfn_set_text_block_specifier(struct bregs *regs)
{
    outw((regs->bl << 8) | 0x03, VGAREG_SEQU_ADDRESS);
}

// -------------------------------------------------------------------
static void
biosfn_load_text_8_16_pat(u8 AL, u8 BL)
{
    get_font_access();
    u16 blockaddr = ((BL & 0x03) << 14) + ((BL & 0x04) << 11);
    u16 i;
    for (i = 0; i < 0x100; i++) {
        u16 src = i * 16;
        void *dest = (void*)(blockaddr + i * 32);
        memcpy_far(0xA000, dest, 0xC000, &vgafont16[src], 16);
    }
    release_font_access();
    if (AL >= 0x10)
        set_scan_lines(16);
}

// -------------------------------------------------------------------
static void
biosfn_get_font_info(u8 BH, u16 *ES, u16 *BP, u16 *CX, u16 *DX)
{
    switch (BH) {
    case 0x00: {
        u32 segoff = GET_IVT(0x1f).segoff;
        *ES = segoff >> 16;
        *BP = segoff;
        break;
    }
    case 0x01: {
        u32 segoff = GET_IVT(0x43).segoff;
        *ES = segoff >> 16;
        *BP = segoff;
        break;
    }
    case 0x02:
        *ES = 0xC000;
        *BP = (u32)vgafont14;
        break;
    case 0x03:
        *ES = 0xC000;
        *BP = (u32)vgafont8;
        break;
    case 0x04:
        *ES = 0xC000;
        *BP = (u32)vgafont8 + 128 * 8;
        break;
    case 0x05:
        *ES = 0xC000;
        *BP = (u32)vgafont14alt;
        break;
    case 0x06:
        *ES = 0xC000;
        *BP = (u32)vgafont16;
        break;
    case 0x07:
        *ES = 0xC000;
        *BP = (u32)vgafont16alt;
        break;
    default:
        dprintf(1, "Get font info BH(%02x) was discarded\n", BH);
        return;
    }
    // Set byte/char of on screen font
    *CX = GET_BDA(char_height) & 0xff;

    // Set Highest char row
    *DX = GET_BDA(video_rows);
}

// -------------------------------------------------------------------
static void
biosfn_get_ega_info(struct bregs *regs)
{
    regs->cx = GET_BDA(video_switches) & 0x0f;
    regs->ax = GET_BDA(crtc_address);
    if (regs->ax == VGAREG_MDA_CRTC_ADDRESS)
        regs->bx = 0x0103;
    else
        regs->bx = 0x0003;
}

// -------------------------------------------------------------------
static void
biosfn_select_vert_res(struct bregs *regs)
{
    u8 mctl = GET_BDA(modeset_ctl);
    u8 vswt = GET_BDA(video_switches);

    switch (regs->al) {
    case 0x00:
        // 200 lines
        mctl = (mctl & ~0x10) | 0x80;
        vswt = (vswt & ~0x0f) | 0x08;
        break;
    case 0x01:
        // 350 lines
        mctl &= ~0x90;
        vswt = (vswt & ~0x0f) | 0x09;
        break;
    case 0x02:
        // 400 lines
        mctl = (mctl & ~0x80) | 0x10;
        vswt = (vswt & ~0x0f) | 0x09;
        break;
    default:
        dprintf(1, "Select vert res (%02x) was discarded\n", regs->al);
        break;
    }
    SET_BDA(modeset_ctl, mctl);
    SET_BDA(video_switches, vswt);
    regs->ax = 0x1212;
}

static void
biosfn_enable_default_palette_loading(struct bregs *regs)
{
    u8 v = (regs->al & 0x01) << 3;
    u8 mctl = GET_BDA(video_ctl) & ~0x08;
    SET_BDA(video_ctl, mctl | v);
    regs->ax = 0x1212;
}

static void
biosfn_enable_video_addressing(struct bregs *regs)
{
    u8 v = ((regs->al << 1) & 0x02) ^ 0x02;
    u8 v2 = inb(VGAREG_READ_MISC_OUTPUT) & ~0x02;
    outb(v | v2, VGAREG_WRITE_MISC_OUTPUT);
    regs->ax = 0x1212;
}


static void
biosfn_enable_grayscale_summing(struct bregs *regs)
{
    u8 v = ((regs->al << 1) & 0x02) ^ 0x02;
    u8 v2 = GET_BDA(modeset_ctl) & ~0x02;
    SET_BDA(modeset_ctl, v | v2);
    regs->ax = 0x1212;
}

static void
biosfn_enable_cursor_emulation(struct bregs *regs)
{
    u8 v = (regs->al & 0x01) ^ 0x01;
    u8 v2 = GET_BDA(modeset_ctl) & ~0x01;
    SET_BDA(modeset_ctl, v | v2);
    regs->ax = 0x1212;
}

// -------------------------------------------------------------------
static void
biosfn_write_string(u8 flag, u8 page, u8 attr, u16 count, u8 row, u8 col,
                    u16 seg, u8 *offset_far)
{
    // Read curs info for the page
    u16 oldcurs, dummy;
    biosfn_get_cursor_pos(page, &dummy, &oldcurs);

    // if row=0xff special case : use current cursor position
    if (row == 0xff) {
        col = oldcurs & 0x00ff;
        row = (oldcurs & 0xff00) >> 8;
    }

    u16 newcurs = row;
    newcurs <<= 8;
    newcurs += col;
    biosfn_set_cursor_pos(page, newcurs);

    while (count-- != 0) {
        u8 car = GET_FARVAR(seg, *offset_far);
        offset_far++;
        if ((flag & 0x02) != 0) {
            attr = GET_FARVAR(seg, *offset_far);
            offset_far++;
        }

        biosfn_write_teletype(car, page, attr, WITH_ATTR);
    }

    // Set back curs pos
    if ((flag & 0x01) == 0)
        biosfn_set_cursor_pos(page, oldcurs);
}

// -------------------------------------------------------------------
static void
biosfn_read_display_code(struct bregs *regs)
{
    regs->bx = GET_BDA(dcc_index);
    regs->al = 0x1a;
}

static void
biosfn_set_display_code(struct bregs *regs)
{
    SET_BDA(dcc_index, regs->bl);
    dprintf(1, "Alternate Display code (%02x) was discarded\n", regs->bh);
    regs->al = 0x1a;
}

// -------------------------------------------------------------------
static void
biosfn_read_state_info(u16 BX, u16 ES, u16 DI)
{
    // Address of static functionality table
    SET_FARVAR(ES, *(u16*)(DI + 0x00), (u32)static_functionality);
    SET_FARVAR(ES, *(u16*)(DI + 0x02), 0xC000);

    // Hard coded copy from BIOS area. Should it be cleaner ?
    memcpy_far(ES, (void*)(DI + 0x04), SEG_BDA, (void*)0x49, 30);
    memcpy_far(ES, (void*)(DI + 0x22), SEG_BDA, (void*)0x84, 3);

    SET_FARVAR(ES, *(u8*)(DI + 0x25), GET_BDA(dcc_index));
    SET_FARVAR(ES, *(u8*)(DI + 0x26), 0);
    SET_FARVAR(ES, *(u8*)(DI + 0x27), 16);
    SET_FARVAR(ES, *(u8*)(DI + 0x28), 0);
    SET_FARVAR(ES, *(u8*)(DI + 0x29), 8);
    SET_FARVAR(ES, *(u8*)(DI + 0x2a), 2);
    SET_FARVAR(ES, *(u8*)(DI + 0x2b), 0);
    SET_FARVAR(ES, *(u8*)(DI + 0x2c), 0);
    SET_FARVAR(ES, *(u8*)(DI + 0x31), 3);
    SET_FARVAR(ES, *(u8*)(DI + 0x32), 0);

    memset_far(ES, (void*)(DI + 0x33), 0, 13);
}

// -------------------------------------------------------------------
// -------------------------------------------------------------------
static u16
biosfn_read_video_state_size(u16 CX)
{
    u16 size = 0;
    if (CX & 1)
        size += 0x46;
    if (CX & 2)
        size += (5 + 8 + 5) * 2 + 6;
    if (CX & 4)
        size += 3 + 256 * 3 + 1;
    return size;
}

static u16
biosfn_save_video_state(u16 CX, u16 ES, u16 BX)
{
    u16 crtc_addr = GET_BDA(crtc_address);
    if (CX & 1) {
        SET_FARVAR(ES, *(u8*)(BX+0), inb(VGAREG_SEQU_ADDRESS));
        BX++;
        SET_FARVAR(ES, *(u8*)(BX+0), inb(crtc_addr));
        BX++;
        SET_FARVAR(ES, *(u8*)(BX+0), inb(VGAREG_GRDC_ADDRESS));
        BX++;
        inb(VGAREG_ACTL_RESET);
        u16 ar_index = inb(VGAREG_ACTL_ADDRESS);
        SET_FARVAR(ES, *(u8*)(BX+0), ar_index);
        BX++;
        SET_FARVAR(ES, *(u8*)(BX+0), inb(VGAREG_READ_FEATURE_CTL));
        BX++;

        u16 i;
        for (i = 1; i <= 4; i++) {
            outb(i, VGAREG_SEQU_ADDRESS);
            SET_FARVAR(ES, *(u8*)(BX+0), inb(VGAREG_SEQU_DATA));
            BX++;
        }
        outb(0, VGAREG_SEQU_ADDRESS);
        SET_FARVAR(ES, *(u8*)(BX+0), inb(VGAREG_SEQU_DATA));
        BX++;

        for (i = 0; i <= 0x18; i++) {
            outb(i, crtc_addr);
            SET_FARVAR(ES, *(u8*)(BX+0), inb(crtc_addr + 1));
            BX++;
        }

        for (i = 0; i <= 0x13; i++) {
            inb(VGAREG_ACTL_RESET);
            outb(i | (ar_index & 0x20), VGAREG_ACTL_ADDRESS);
            SET_FARVAR(ES, *(u8*)(BX+0), inb(VGAREG_ACTL_READ_DATA));
            BX++;
        }
        inb(VGAREG_ACTL_RESET);

        for (i = 0; i <= 8; i++) {
            outb(i, VGAREG_GRDC_ADDRESS);
            SET_FARVAR(ES, *(u8*)(BX+0), inb(VGAREG_GRDC_DATA));
            BX++;
        }

        SET_FARVAR(ES, *(u16*)(BX+0), crtc_addr);
        BX += 2;

        /* XXX: read plane latches */
        SET_FARVAR(ES, *(u8*)(BX+0), 0);
        BX++;
        SET_FARVAR(ES, *(u8*)(BX+0), 0);
        BX++;
        SET_FARVAR(ES, *(u8*)(BX+0), 0);
        BX++;
        SET_FARVAR(ES, *(u8*)(BX+0), 0);
        BX++;
    }
    if (CX & 2) {
        SET_FARVAR(ES, *(u8*)(BX+0), GET_BDA(video_mode));
        BX++;
        SET_FARVAR(ES, *(u16*)(BX+0), GET_BDA(video_cols));
        BX += 2;
        SET_FARVAR(ES, *(u16*)(BX+0), GET_BDA(video_pagesize));
        BX += 2;
        SET_FARVAR(ES, *(u16*)(BX+0), GET_BDA(crtc_address));
        BX += 2;
        SET_FARVAR(ES, *(u8*)(BX+0), GET_BDA(video_rows));
        BX++;
        SET_FARVAR(ES, *(u16*)(BX+0), GET_BDA(char_height));
        BX += 2;
        SET_FARVAR(ES, *(u8*)(BX+0), GET_BDA(video_ctl));
        BX++;
        SET_FARVAR(ES, *(u8*)(BX+0), GET_BDA(video_switches));
        BX++;
        SET_FARVAR(ES, *(u8*)(BX+0), GET_BDA(modeset_ctl));
        BX++;
        SET_FARVAR(ES, *(u16*)(BX+0), GET_BDA(cursor_type));
        BX += 2;
        u16 i;
        for (i = 0; i < 8; i++) {
            SET_FARVAR(ES, *(u16*)(BX+0), GET_BDA(cursor_pos[i]));
            BX += 2;
        }
        SET_FARVAR(ES, *(u16*)(BX+0), GET_BDA(video_pagestart));
        BX += 2;
        SET_FARVAR(ES, *(u8*)(BX+0), GET_BDA(video_page));
        BX++;
        /* current font */
        SET_FARVAR(ES, *(u16*)(BX+0), GET_FARVAR(0, *(u16*)(0x1f * 4)));
        BX += 2;
        SET_FARVAR(ES, *(u16*)(BX+0), GET_FARVAR(0, *(u16*)(0x1f * 4 + 2)));
        BX += 2;
        SET_FARVAR(ES, *(u16*)(BX+0), GET_FARVAR(0, *(u16*)(0x43 * 4)));
        BX += 2;
        SET_FARVAR(ES, *(u16*)(BX+0), GET_FARVAR(0, *(u16*)(0x43 * 4 + 2)));
        BX += 2;
    }
    if (CX & 4) {
        /* XXX: check this */
        SET_FARVAR(ES, *(u8*)(BX+0), inb(VGAREG_DAC_STATE));
        BX++;                   /* read/write mode dac */
        SET_FARVAR(ES, *(u8*)(BX+0), inb(VGAREG_DAC_WRITE_ADDRESS));
        BX++;                   /* pix address */
        SET_FARVAR(ES, *(u8*)(BX+0), inb(VGAREG_PEL_MASK));
        BX++;
        // Set the whole dac always, from 0
        outb(0x00, VGAREG_DAC_WRITE_ADDRESS);
        u16 i;
        for (i = 0; i < 256 * 3; i++) {
            SET_FARVAR(ES, *(u8*)(BX+0), inb(VGAREG_DAC_DATA));
            BX++;
        }
        SET_FARVAR(ES, *(u8*)(BX+0), 0);
        BX++;                   /* color select register */
    }
    return BX;
}

static u16
biosfn_restore_video_state(u16 CX, u16 ES, u16 BX)
{
    if (CX & 1) {
        // Reset Attribute Ctl flip-flop
        inb(VGAREG_ACTL_RESET);

        u16 crtc_addr = GET_FARVAR(ES, *(u16*)(BX + 0x40));
        u16 addr1 = BX;
        BX += 5;

        u16 i;
        for (i = 1; i <= 4; i++) {
            outb(i, VGAREG_SEQU_ADDRESS);
            outb(GET_FARVAR(ES, *(u8*)(BX+0)), VGAREG_SEQU_DATA);
            BX++;
        }
        outb(0, VGAREG_SEQU_ADDRESS);
        outb(GET_FARVAR(ES, *(u8*)(BX+0)), VGAREG_SEQU_DATA);
        BX++;

        // Disable CRTC write protection
        outw(0x0011, crtc_addr);
        // Set CRTC regs
        for (i = 0; i <= 0x18; i++) {
            if (i != 0x11) {
                outb(i, crtc_addr);
                outb(GET_FARVAR(ES, *(u8*)(BX+0)), crtc_addr + 1);
            }
            BX++;
        }
        // select crtc base address
        u16 v = inb(VGAREG_READ_MISC_OUTPUT) & ~0x01;
        if (crtc_addr == VGAREG_VGA_CRTC_ADDRESS)
            v |= 0x01;
        outb(v, VGAREG_WRITE_MISC_OUTPUT);

        // enable write protection if needed
        outb(0x11, crtc_addr);
        outb(GET_FARVAR(ES, *(u8*)(BX - 0x18 + 0x11)), crtc_addr + 1);

        // Set Attribute Ctl
        u16 ar_index = GET_FARVAR(ES, *(u8*)(addr1 + 0x03));
        inb(VGAREG_ACTL_RESET);
        for (i = 0; i <= 0x13; i++) {
            outb(i | (ar_index & 0x20), VGAREG_ACTL_ADDRESS);
            outb(GET_FARVAR(ES, *(u8*)(BX+0)), VGAREG_ACTL_WRITE_DATA);
            BX++;
        }
        outb(ar_index, VGAREG_ACTL_ADDRESS);
        inb(VGAREG_ACTL_RESET);

        for (i = 0; i <= 8; i++) {
            outb(i, VGAREG_GRDC_ADDRESS);
            outb(GET_FARVAR(ES, *(u8*)(BX+0)), VGAREG_GRDC_DATA);
            BX++;
        }
        BX += 2;                /* crtc_addr */
        BX += 4;                /* plane latches */

        outb(GET_FARVAR(ES, *(u8*)(addr1+0)), VGAREG_SEQU_ADDRESS);
        addr1++;
        outb(GET_FARVAR(ES, *(u8*)(addr1+0)), crtc_addr);
        addr1++;
        outb(GET_FARVAR(ES, *(u8*)(addr1+0)), VGAREG_GRDC_ADDRESS);
        addr1++;
        addr1++;
        outb(GET_FARVAR(ES, *(u8*)(addr1+0)), crtc_addr - 0x4 + 0xa);
        addr1++;
    }
    if (CX & 2) {
        SET_BDA(video_mode, GET_FARVAR(ES, *(u8*)(BX+0)));
        BX++;
        SET_BDA(video_cols, GET_FARVAR(ES, *(u16*)(BX+0)));
        BX += 2;
        SET_BDA(video_pagesize, GET_FARVAR(ES, *(u16*)(BX+0)));
        BX += 2;
        SET_BDA(crtc_address, GET_FARVAR(ES, *(u16*)(BX+0)));
        BX += 2;
        SET_BDA(video_rows, GET_FARVAR(ES, *(u8*)(BX+0)));
        BX++;
        SET_BDA(char_height, GET_FARVAR(ES, *(u16*)(BX+0)));
        BX += 2;
        SET_BDA(video_ctl, GET_FARVAR(ES, *(u8*)(BX+0)));
        BX++;
        SET_BDA(video_switches, GET_FARVAR(ES, *(u8*)(BX+0)));
        BX++;
        SET_BDA(modeset_ctl, GET_FARVAR(ES, *(u8*)(BX+0)));
        BX++;
        SET_BDA(cursor_type, GET_FARVAR(ES, *(u16*)(BX+0)));
        BX += 2;
        u16 i;
        for (i = 0; i < 8; i++) {
            SET_BDA(cursor_pos[i], GET_FARVAR(ES, *(u16*)(BX+0)));
            BX += 2;
        }
        SET_BDA(video_pagestart, GET_FARVAR(ES, *(u16*)(BX+0)));
        BX += 2;
        SET_BDA(video_page, GET_FARVAR(ES, *(u8*)(BX+0)));
        BX++;
        /* current font */
        SET_IVT(0x1f, GET_FARVAR(ES, *(u16*)(BX+2)), GET_FARVAR(ES, *(u16*)(BX+0)));
        BX += 4;
        SET_IVT(0x43, GET_FARVAR(ES, *(u16*)(BX+2)), GET_FARVAR(ES, *(u16*)(BX+0)));
        BX += 4;
    }
    if (CX & 4) {
        BX++;
        u16 v = GET_FARVAR(ES, *(u8*)(BX+0));
        BX++;
        outb(GET_FARVAR(ES, *(u8*)(BX+0)), VGAREG_PEL_MASK);
        BX++;
        // Set the whole dac always, from 0
        outb(0x00, VGAREG_DAC_WRITE_ADDRESS);
        u16 i;
        for (i = 0; i < 256 * 3; i++) {
            outb(GET_FARVAR(ES, *(u8*)(BX+0)), VGAREG_DAC_DATA);
            BX++;
        }
        BX++;
        outb(v, VGAREG_DAC_WRITE_ADDRESS);
    }
    return BX;
}


/****************************************************************
 * VGA int 10 handler
 ****************************************************************/

static void
handle_1000(struct bregs *regs)
{
    // XXX - inline
    biosfn_set_video_mode(regs->al);
    switch(regs->al & 0x7F) {
    case 6:
        regs->al = 0x3F;
        break;
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 7:
        regs->al = 0x30;
        break;
    default:
        regs->al = 0x20;
    }
}

static void
handle_1001(struct bregs *regs)
{
    biosfn_set_cursor_shape(regs->ch, regs->cl);
}

static void
handle_1002(struct bregs *regs)
{
    biosfn_set_cursor_pos(regs->bh, regs->dx);
}

static void
handle_1003(struct bregs *regs)
{
    biosfn_get_cursor_pos(regs->bh, &regs->cx, &regs->dx);
}

// Read light pen pos (unimplemented)
static void
handle_1004(struct bregs *regs)
{
    debug_stub(regs);
    regs->ax = regs->bx = regs->cx = regs->dx = 0;
}

static void
handle_1005(struct bregs *regs)
{
    biosfn_set_active_page(regs->al);
}

static void
handle_1006(struct bregs *regs)
{
    biosfn_scroll(regs->al, regs->bh, regs->ch, regs->cl, regs->dh, regs->dl
                  , 0xFF, SCROLL_UP);
}

static void
handle_1007(struct bregs *regs)
{
    biosfn_scroll(regs->al, regs->bh, regs->ch, regs->cl, regs->dh, regs->dl
                  , 0xFF, SCROLL_DOWN);
}

static void
handle_1008(struct bregs *regs)
{
    // XXX - inline
    biosfn_read_char_attr(regs->bh, &regs->ax);
}

static void
handle_1009(struct bregs *regs)
{
    // XXX - inline
    biosfn_write_char_attr(regs->al, regs->bh, regs->bl, regs->cx);
}

static void
handle_100a(struct bregs *regs)
{
    // XXX - inline
    biosfn_write_char_only(regs->al, regs->bh, regs->bl, regs->cx);
}


static void
handle_100b00(struct bregs *regs)
{
    // XXX - inline
    biosfn_set_border_color(regs);
}

static void
handle_100b01(struct bregs *regs)
{
    // XXX - inline
    biosfn_set_palette(regs);
}

static void
handle_100bXX(struct bregs *regs)
{
    debug_stub(regs);
}

static void
handle_100b(struct bregs *regs)
{
    switch (regs->bh) {
    case 0x00: handle_100b00(regs); break;
    case 0x01: handle_100b01(regs); break;
    default:   handle_100bXX(regs); break;
    }
}


static void
handle_100c(struct bregs *regs)
{
    // XXX - inline
    biosfn_write_pixel(regs->bh, regs->al, regs->cx, regs->dx);
}

static void
handle_100d(struct bregs *regs)
{
    // XXX - inline
    biosfn_read_pixel(regs->bh, regs->cx, regs->dx, &regs->ax);
}

static void
handle_100e(struct bregs *regs)
{
    // Ralf Brown Interrupt list is WRONG on bh(page)
    // We do output only on the current page !
    biosfn_write_teletype(regs->al, 0xff, regs->bl, NO_ATTR);
}

static void
handle_100f(struct bregs *regs)
{
    // XXX - inline
    biosfn_get_video_mode(regs);
}


static void
handle_101000(struct bregs *regs)
{
    if (regs->bl > 0x14)
        return;
    biosfn_set_single_palette_reg(regs->bl, regs->bh);
}

static void
handle_101001(struct bregs *regs)
{
    // XXX - inline
    biosfn_set_overscan_border_color(regs);
}

static void
handle_101002(struct bregs *regs)
{
    // XXX - inline
    biosfn_set_all_palette_reg(regs);
}

static void
handle_101003(struct bregs *regs)
{
    // XXX - inline
    biosfn_toggle_intensity(regs);
}

static void
handle_101007(struct bregs *regs)
{
    if (regs->bl > 0x14)
        return;
    regs->bh = biosfn_get_single_palette_reg(regs->bl);
}

static void
handle_101008(struct bregs *regs)
{
    // XXX - inline
    biosfn_read_overscan_border_color(regs);
}

static void
handle_101009(struct bregs *regs)
{
    // XXX - inline
    biosfn_get_all_palette_reg(regs);
}

static void
handle_101010(struct bregs *regs)
{
    // XXX - inline
    biosfn_set_single_dac_reg(regs);
}

static void
handle_101012(struct bregs *regs)
{
    // XXX - inline
    biosfn_set_all_dac_reg(regs);
}

static void
handle_101013(struct bregs *regs)
{
    // XXX - inline
    biosfn_select_video_dac_color_page(regs);
}

static void
handle_101015(struct bregs *regs)
{
    // XXX - inline
    biosfn_read_single_dac_reg(regs);
}

static void
handle_101017(struct bregs *regs)
{
    // XXX - inline
    biosfn_read_all_dac_reg(regs);
}

static void
handle_101018(struct bregs *regs)
{
    // XXX - inline
    biosfn_set_pel_mask(regs);
}

static void
handle_101019(struct bregs *regs)
{
    // XXX - inline
    biosfn_read_pel_mask(regs);
}

static void
handle_10101a(struct bregs *regs)
{
    // XXX - inline
    biosfn_read_video_dac_state(regs);
}

static void
handle_10101b(struct bregs *regs)
{
    biosfn_perform_gray_scale_summing(regs->bx, regs->cx);
}

static void
handle_1010XX(struct bregs *regs)
{
    debug_stub(regs);
}

static void
handle_1010(struct bregs *regs)
{
    switch (regs->al) {
    case 0x00: handle_101000(regs); break;
    case 0x01: handle_101001(regs); break;
    case 0x02: handle_101002(regs); break;
    case 0x03: handle_101003(regs); break;
    case 0x07: handle_101007(regs); break;
    case 0x08: handle_101008(regs); break;
    case 0x09: handle_101009(regs); break;
    case 0x10: handle_101010(regs); break;
    case 0x12: handle_101012(regs); break;
    case 0x13: handle_101013(regs); break;
    case 0x15: handle_101015(regs); break;
    case 0x17: handle_101017(regs); break;
    case 0x18: handle_101018(regs); break;
    case 0x19: handle_101019(regs); break;
    case 0x1a: handle_10101a(regs); break;
    case 0x1b: handle_10101b(regs); break;
    default:   handle_1010XX(regs); break;
    }
}


static void
handle_101100(struct bregs *regs)
{
    // XXX - inline
    biosfn_load_text_user_pat(regs->al, regs->es, 0 // XXX - regs->bp
                              , regs->cx, regs->dx, regs->bl, regs->bh);
}

static void
handle_101101(struct bregs *regs)
{
    // XXX - inline
    biosfn_load_text_8_14_pat(regs->al, regs->bl);
}

static void
handle_101102(struct bregs *regs)
{
    // XXX - inline
    biosfn_load_text_8_8_pat(regs->al, regs->bl);
}

static void
handle_101103(struct bregs *regs)
{
    // XXX - inline
    biosfn_set_text_block_specifier(regs);
}

static void
handle_101104(struct bregs *regs)
{
    // XXX - inline
    biosfn_load_text_8_16_pat(regs->al, regs->bl);
}

static void
handle_101110(struct bregs *regs)
{
    handle_101100(regs);
}

static void
handle_101111(struct bregs *regs)
{
    handle_101101(regs);
}

static void
handle_101112(struct bregs *regs)
{
    handle_101102(regs);
}

static void
handle_101114(struct bregs *regs)
{
    handle_101104(regs);
}

static void
handle_101130(struct bregs *regs)
{
    // XXX - inline
    biosfn_get_font_info(regs->bh, &regs->es, 0 // &regs->bp
                         , &regs->cx, &regs->dx);
}

static void
handle_1011XX(struct bregs *regs)
{
    debug_stub(regs);
}

static void
handle_1011(struct bregs *regs)
{
    switch (regs->al) {
    case 0x00: handle_101100(regs); break;
    case 0x01: handle_101101(regs); break;
    case 0x02: handle_101102(regs); break;
    case 0x03: handle_101103(regs); break;
    case 0x04: handle_101104(regs); break;
    case 0x10: handle_101110(regs); break;
    case 0x11: handle_101111(regs); break;
    case 0x12: handle_101112(regs); break;
    case 0x14: handle_101114(regs); break;
    case 0x30: handle_101130(regs); break;
    default:   handle_1011XX(regs); break;
    }
}


static void
handle_101210(struct bregs *regs)
{
    // XXX - inline
    biosfn_get_ega_info(regs);
}

static void
handle_101230(struct bregs *regs)
{
    // XXX - inline
    biosfn_select_vert_res(regs);
}

static void
handle_101231(struct bregs *regs)
{
    // XXX - inline
    biosfn_enable_default_palette_loading(regs);
}

static void
handle_101232(struct bregs *regs)
{
    // XXX - inline
    biosfn_enable_video_addressing(regs);
}

static void
handle_101233(struct bregs *regs)
{
    // XXX - inline
    biosfn_enable_grayscale_summing(regs);
}

static void
handle_101234(struct bregs *regs)
{
    // XXX - inline
    biosfn_enable_cursor_emulation(regs);
}

static void
handle_101235(struct bregs *regs)
{
    debug_stub(regs);
    regs->al = 0x12;
}

static void
handle_101236(struct bregs *regs)
{
    debug_stub(regs);
    regs->al = 0x12;
}

static void
handle_1012XX(struct bregs *regs)
{
    debug_stub(regs);
}

static void
handle_1012(struct bregs *regs)
{
    switch (regs->bl) {
    case 0x10: handle_101210(regs); break;
    case 0x30: handle_101230(regs); break;
    case 0x31: handle_101231(regs); break;
    case 0x32: handle_101232(regs); break;
    case 0x33: handle_101233(regs); break;
    case 0x34: handle_101234(regs); break;
    case 0x35: handle_101235(regs); break;
    case 0x36: handle_101236(regs); break;
    default:   handle_1012XX(regs); break;
    }

    // XXX - cirrus has 1280, 1281, 1282, 1285, 129a, 12a0, 12a1, 12a2, 12ae
}


static void
handle_1013(struct bregs *regs)
{
    // XXX - inline
    biosfn_write_string(regs->al, regs->bh, regs->bl, regs->cx
                        , regs->dh, regs->dl, regs->es, 0); // regs->bp);
}


static void
handle_101a00(struct bregs *regs)
{
    // XXX - inline
    biosfn_read_display_code(regs);
}

static void
handle_101a01(struct bregs *regs)
{
    // XXX - inline
    biosfn_set_display_code(regs);
}

static void
handle_101aXX(struct bregs *regs)
{
    debug_stub(regs);
}

static void
handle_101a(struct bregs *regs)
{
    switch (regs->al) {
    case 0x00: handle_101a00(regs); break;
    case 0x01: handle_101a01(regs); break;
    default:   handle_101aXX(regs); break;
    }
}


static void
handle_101b(struct bregs *regs)
{
    // XXX - inline
    biosfn_read_state_info(regs->bx, regs->es, regs->di);
    regs->al = 0x1B;
}


static void
handle_101c00(struct bregs *regs)
{
    // XXX - inline
    regs->bx = biosfn_read_video_state_size(regs->cx);
}

static void
handle_101c01(struct bregs *regs)
{
    // XXX - inline
    biosfn_save_video_state(regs->cx, regs->es, regs->bx);
}

static void
handle_101c02(struct bregs *regs)
{
    // XXX - inline
    biosfn_restore_video_state(regs->cx, regs->es, regs->bx);
}

static void
handle_101cXX(struct bregs *regs)
{
    debug_stub(regs);
}

static void
handle_101c(struct bregs *regs)
{
    switch (regs->al) {
    case 0x00: handle_101c00(regs); break;
    case 0x01: handle_101c01(regs); break;
    case 0x02: handle_101c02(regs); break;
    default:   handle_101cXX(regs); break;
    }
}


static void
handle_104f00(struct bregs *regs)
{
    // XXX - vbe_biosfn_return_controller_information(&AX,ES,DI);
    // XXX - OR cirrus_vesa_00h
}

static void
handle_104f01(struct bregs *regs)
{
    // XXX - vbe_biosfn_return_mode_information(&AX,CX,ES,DI);
    // XXX - OR cirrus_vesa_01h
}

static void
handle_104f02(struct bregs *regs)
{
    // XXX - vbe_biosfn_set_mode(&AX,BX,ES,DI);
    // XXX - OR cirrus_vesa_02h
}

static void
handle_104f03(struct bregs *regs)
{
    // XXX - vbe_biosfn_return_current_mode
    // XXX - OR cirrus_vesa_03h
}

static void
handle_104f04(struct bregs *regs)
{
    // XXX - vbe_biosfn_save_restore_state(&AX, CX, DX, ES, &BX);
}

static void
handle_104f05(struct bregs *regs)
{
    // XXX - vbe_biosfn_display_window_control
    // XXX - OR cirrus_vesa_05h
}

static void
handle_104f06(struct bregs *regs)
{
    // XXX - vbe_biosfn_set_get_logical_scan_line_length
    // XXX - OR cirrus_vesa_06h
}

static void
handle_104f07(struct bregs *regs)
{
    // XXX - vbe_biosfn_set_get_display_start
    // XXX - OR cirrus_vesa_07h
}

static void
handle_104f08(struct bregs *regs)
{
    // XXX - vbe_biosfn_set_get_dac_palette_format
}

static void
handle_104f0a(struct bregs *regs)
{
    // XXX - vbe_biosfn_return_protected_mode_interface
}

static void
handle_104fXX(struct bregs *regs)
{
    debug_stub(regs);
    regs->ax = 0x0100;
}

static void
handle_104f(struct bregs *regs)
{
    if (! CONFIG_VBE) {
        handle_104fXX(regs);
        return;
    }

    // XXX - check vbe_has_vbe_display()?

    switch (regs->al) {
    case 0x00: handle_104f00(regs); break;
    case 0x01: handle_104f01(regs); break;
    case 0x02: handle_104f02(regs); break;
    case 0x03: handle_104f03(regs); break;
    case 0x04: handle_104f04(regs); break;
    case 0x05: handle_104f05(regs); break;
    case 0x06: handle_104f06(regs); break;
    case 0x07: handle_104f07(regs); break;
    case 0x08: handle_104f08(regs); break;
    case 0x0a: handle_104f0a(regs); break;
    default:   handle_104fXX(regs); break;
    }
}


static void
handle_10XX(struct bregs *regs)
{
    debug_stub(regs);
}

// INT 10h Video Support Service Entry Point
void VISIBLE16
handle_10(struct bregs *regs)
{
    debug_enter(regs, DEBUG_VGA_10);
    switch (regs->ah) {
    case 0x00: handle_1000(regs); break;
    case 0x01: handle_1001(regs); break;
    case 0x02: handle_1002(regs); break;
    case 0x03: handle_1003(regs); break;
    case 0x04: handle_1004(regs); break;
    case 0x05: handle_1005(regs); break;
    case 0x06: handle_1006(regs); break;
    case 0x07: handle_1007(regs); break;
    case 0x08: handle_1008(regs); break;
    case 0x09: handle_1009(regs); break;
    case 0x0a: handle_100a(regs); break;
    case 0x0b: handle_100b(regs); break;
    case 0x0c: handle_100c(regs); break;
    case 0x0d: handle_100d(regs); break;
    case 0x0e: handle_100e(regs); break;
    case 0x0f: handle_100f(regs); break;
    case 0x10: handle_1010(regs); break;
    case 0x11: handle_1011(regs); break;
    case 0x12: handle_1012(regs); break;
    case 0x13: handle_1013(regs); break;
    case 0x1a: handle_101a(regs); break;
    case 0x1b: handle_101b(regs); break;
    case 0x1c: handle_101c(regs); break;
    case 0x4f: handle_104f(regs); break;
    default:   handle_10XX(regs); break;
    }
}


/****************************************************************
 * VGA post
 ****************************************************************/

static void
init_bios_area()
{
    // init detected hardware BIOS Area
    // set 80x25 color (not clear from RBIL but usual)
    u16 eqf = GET_BDA(equipment_list_flags);
    SET_BDA(equipment_list_flags, (eqf & 0xffcf) | 0x20);

    // Just for the first int10 find its children

    // the default char height
    SET_BDA(char_height, 0x10);

    // Clear the screen
    SET_BDA(video_ctl, 0x60);

    // Set the basic screen we have
    SET_BDA(video_switches, 0xf9);

    // Set the basic modeset options
    SET_BDA(modeset_ctl, 0x51);

    // Set the  default MSR
    SET_BDA(video_msr, 0x09);
}

static void
init_vga_card()
{
    // switch to color mode and enable CPU access 480 lines
    outb(0xc3, VGAREG_WRITE_MISC_OUTPUT);
    // more than 64k 3C4/04
    outb(0x04, VGAREG_SEQU_ADDRESS);
    outb(0x02, VGAREG_SEQU_DATA);
}

void VISIBLE16
vga_post(struct bregs *regs)
{
    debug_enter(regs, DEBUG_VGA_POST);

    init_vga_card();

    init_bios_area();

    // vbe_init();

    extern void entry_10(void);
    SET_IVT(0x10, 0xC000, (u32)entry_10);

    if (CONFIG_CIRRUS)
        cirrus_init();

    // XXX - clear screen and display info

    // XXX: fill it
    SET_VGA(video_save_pointer_table[0], (u32)video_param_table);
    SET_VGA(video_save_pointer_table[1], 0xC000);

    // Fixup checksum
    extern u8 _rom_header_size, _rom_header_checksum;
    SET_VGA(_rom_header_checksum, 0);
    u8 sum = -checksum_far(0xC000, 0, _rom_header_size * 512);
    SET_VGA(_rom_header_checksum, sum);
}
