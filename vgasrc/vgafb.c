// Code for manipulating VGA framebuffers.
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2001-2008 the LGPL VGABios developers Team
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "biosvar.h" // GET_BDA
#include "util.h" // memset_far
#include "vgatables.h" // find_vga_entry

// TODO
//  * extract hw code from framebuffer code
//  * use clear_screen() in scroll code
//  * normalize params (don't use AX/BX/CX/etc.)

// XXX
inline void
memcpy16_far(u16 d_seg, void *d_far, u16 s_seg, const void *s_far, size_t len)
{
    memcpy_far(d_seg, d_far, s_seg, s_far, len);
}


/****************************************************************
 * Screen scrolling
 ****************************************************************/

static void
vgamem_copy_pl4(u8 xstart, u8 ysrc, u8 ydest, u8 cols, u8 nbcols,
                u8 cheight)
{
    u16 src = ysrc * cheight * nbcols + xstart;
    u16 dest = ydest * cheight * nbcols + xstart;
    outw(0x0105, VGAREG_GRDC_ADDRESS);
    u8 i;
    for (i = 0; i < cheight; i++)
        memcpy_far(SEG_GRAPH, (void*)(dest + i * nbcols)
                   , SEG_GRAPH, (void*)(src + i * nbcols), cols);
    outw(0x0005, VGAREG_GRDC_ADDRESS);
}

static void
vgamem_fill_pl4(u8 xstart, u8 ystart, u8 cols, u8 nbcols, u8 cheight,
                u8 attr)
{
    u16 dest = ystart * cheight * nbcols + xstart;
    outw(0x0205, VGAREG_GRDC_ADDRESS);
    u8 i;
    for (i = 0; i < cheight; i++)
        memset_far(SEG_GRAPH, (void*)(dest + i * nbcols), attr, cols);
    outw(0x0005, VGAREG_GRDC_ADDRESS);
}

static void
vgamem_copy_cga(u8 xstart, u8 ysrc, u8 ydest, u8 cols, u8 nbcols,
                u8 cheight)
{
    u16 src = ((ysrc * cheight * nbcols) >> 1) + xstart;
    u16 dest = ((ydest * cheight * nbcols) >> 1) + xstart;
    u8 i;
    for (i = 0; i < cheight; i++)
        if (i & 1)
            memcpy_far(SEG_CTEXT, (void*)(0x2000 + dest + (i >> 1) * nbcols)
                       , SEG_CTEXT, (void*)(0x2000 + src + (i >> 1) * nbcols)
                       , cols);
        else
            memcpy_far(SEG_CTEXT, (void*)(dest + (i >> 1) * nbcols)
                       , SEG_CTEXT, (void*)(src + (i >> 1) * nbcols), cols);
}

static void
vgamem_fill_cga(u8 xstart, u8 ystart, u8 cols, u8 nbcols, u8 cheight,
                u8 attr)
{
    u16 dest = ((ystart * cheight * nbcols) >> 1) + xstart;
    u8 i;
    for (i = 0; i < cheight; i++)
        if (i & 1)
            memset_far(SEG_CTEXT, (void*)(0x2000 + dest + (i >> 1) * nbcols)
                       , attr, cols);
        else
            memset_far(SEG_CTEXT, (void*)(dest + (i >> 1) * nbcols), attr, cols);
}

void
clear_screen(struct vgamode_s *vmode_g)
{
    if (GET_GLOBAL(vmode_g->memmodel) & TEXT) {
        memset16_far(GET_GLOBAL(vmode_g->sstart), 0, 0x0720, 32*1024);
        return;
    }
    if (GET_GLOBAL(vmode_g->svgamode) < 0x0d) {
        memset16_far(GET_GLOBAL(vmode_g->sstart), 0, 0x0000, 32*1024);
        return;
    }
    outb(0x02, VGAREG_SEQU_ADDRESS);
    u8 mmask = inb(VGAREG_SEQU_DATA);
    outb(0x0f, VGAREG_SEQU_DATA);   // all planes
    memset16_far(GET_GLOBAL(vmode_g->sstart), 0, 0x0000, 64*1024);
    outb(mmask, VGAREG_SEQU_DATA);
}

void
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

    if (GET_GLOBAL(vmode_g->memmodel) & TEXT) {
        // Compute the address
        void *address_far = (void*)(SCREEN_MEM_START(nbcols, nbrows, page));
        dprintf(3, "Scroll, address %p (%d %d %02x)\n"
                , address_far, nbrows, nbcols, page);

        if (nblines == 0 && rul == 0 && cul == 0 && rlr == nbrows - 1
            && clr == nbcols - 1) {
            memset16_far(GET_GLOBAL(vmode_g->sstart), address_far
                         , (u16)attr * 0x100 + ' ', nbrows * nbcols * 2);
        } else {                // if Scroll up
            if (dir == SCROLL_UP) {
                u16 i;
                for (i = rul; i <= rlr; i++)
                    if ((i + nblines > rlr) || (nblines == 0))
                        memset16_far(GET_GLOBAL(vmode_g->sstart)
                                     , address_far + (i * nbcols + cul) * 2
                                     , (u16)attr * 0x100 + ' ', cols * 2);
                    else
                        memcpy16_far(GET_GLOBAL(vmode_g->sstart)
                                     , address_far + (i * nbcols + cul) * 2
                                     , GET_GLOBAL(vmode_g->sstart)
                                     , (void*)(((i + nblines) * nbcols + cul) * 2)
                                     , cols * 2);
            } else {
                u16 i;
                for (i = rlr; i >= rul; i--) {
                    if ((i < rul + nblines) || (nblines == 0))
                        memset16_far(GET_GLOBAL(vmode_g->sstart)
                                     , address_far + (i * nbcols + cul) * 2
                                     , (u16)attr * 0x100 + ' ', cols * 2);
                    else
                        memcpy16_far(GET_GLOBAL(vmode_g->sstart)
                                     , address_far + (i * nbcols + cul) * 2
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


/****************************************************************
 * Read/write characters to screen
 ****************************************************************/

static void
write_gfx_char_pl4(struct vgamode_s *vmode_g
                   , struct cursorpos cp, struct carattr ca)
{
    u16 nbcols = GET_BDA(video_cols);
    if (cp.x >= nbcols)
        return;

    struct VideoParam_s *vparam_g = GET_GLOBAL(vmode_g->vparam);
    u8 cheight = GET_GLOBAL(vparam_g->cheight);
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
    u16 addr = cp.x + cp.y * cheight * nbcols;
    u16 src = ca.car * cheight;
    outw(0x0f02, VGAREG_SEQU_ADDRESS);
    outw(0x0205, VGAREG_GRDC_ADDRESS);
    if (ca.attr & 0x80)
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
            GET_FARVAR(SEG_GRAPH, *dest_far);
            if (GET_GLOBAL(fdata_g[src + i]) & mask)
                SET_FARVAR(SEG_GRAPH, *dest_far, ca.attr & 0x0f);
            else
                SET_FARVAR(SEG_GRAPH, *dest_far, 0x00);
        }
    }
    outw(0xff08, VGAREG_GRDC_ADDRESS);
    outw(0x0005, VGAREG_GRDC_ADDRESS);
    outw(0x0003, VGAREG_GRDC_ADDRESS);
}

static void
write_gfx_char_cga(struct vgamode_s *vmode_g
                   , struct cursorpos cp, struct carattr ca)
{
    u16 nbcols = GET_BDA(video_cols);
    if (cp.x >= nbcols)
        return;

    u8 *fdata_g = vgafont8;
    u8 bpp = GET_GLOBAL(vmode_g->pixbits);
    u16 addr = (cp.x * bpp) + cp.y * 320;
    u16 src = ca.car * 8;
    u8 i;
    for (i = 0; i < 8; i++) {
        u8 *dest_far = (void*)(addr + (i >> 1) * 80);
        if (i & 1)
            dest_far += 0x2000;
        u8 mask = 0x80;
        if (bpp == 1) {
            u8 data = 0;
            if (ca.attr & 0x80)
                data = GET_FARVAR(SEG_CTEXT, *dest_far);
            u8 j;
            for (j = 0; j < 8; j++) {
                if (GET_GLOBAL(fdata_g[src + i]) & mask) {
                    if (ca.attr & 0x80)
                        data ^= (ca.attr & 0x01) << (7 - j);
                    else
                        data |= (ca.attr & 0x01) << (7 - j);
                }
                mask >>= 1;
            }
            SET_FARVAR(SEG_CTEXT, *dest_far, data);
        } else {
            while (mask > 0) {
                u8 data = 0;
                if (ca.attr & 0x80)
                    data = GET_FARVAR(SEG_CTEXT, *dest_far);
                u8 j;
                for (j = 0; j < 4; j++) {
                    if (GET_GLOBAL(fdata_g[src + i]) & mask) {
                        if (ca.attr & 0x80)
                            data ^= (ca.attr & 0x03) << ((3 - j) * 2);
                        else
                            data |= (ca.attr & 0x03) << ((3 - j) * 2);
                    }
                    mask >>= 1;
                }
                SET_FARVAR(SEG_CTEXT, *dest_far, data);
                dest_far += 1;
            }
        }
    }
}

static void
write_gfx_char_lin(struct vgamode_s *vmode_g
                   , struct cursorpos cp, struct carattr ca)
{
    // Get the dimensions
    u16 nbcols = GET_BDA(video_cols);
    if (cp.x >= nbcols)
        return;

    u8 *fdata_g = vgafont8;
    u16 addr = cp.x * 8 + cp.y * nbcols * 64;
    u16 src = ca.car * 8;
    u8 i;
    for (i = 0; i < 8; i++) {
        u8 *dest_far = (void*)(addr + i * nbcols * 8);
        u8 mask = 0x80;
        u8 j;
        for (j = 0; j < 8; j++) {
            u8 data = 0x00;
            if (GET_GLOBAL(fdata_g[src + i]) & mask)
                data = ca.attr;
            SET_FARVAR(SEG_GRAPH, dest_far[j], data);
            mask >>= 1;
        }
    }
}

static void
write_text_char(struct vgamode_s *vmode_g
                , struct cursorpos cp, struct carattr ca)
{
    // Get the dimensions
    u16 nbrows = GET_BDA(video_rows) + 1;
    u16 nbcols = GET_BDA(video_cols);

    // Compute the address
    void *address_far = (void*)(SCREEN_MEM_START(nbcols, nbrows, cp.page)
                                + (cp.x + cp.y * nbcols) * 2);

    if (ca.use_attr) {
        u16 dummy = (ca.attr << 8) | ca.car;
        SET_FARVAR(GET_GLOBAL(vmode_g->sstart), *(u16*)address_far, dummy);
    } else {
        SET_FARVAR(GET_GLOBAL(vmode_g->sstart), *(u8*)address_far, ca.car);
    }
}

void
vgafb_write_char(struct cursorpos cp, struct carattr ca)
{
    // Get the mode
    struct vgamode_s *vmode_g = find_vga_entry(GET_BDA(video_mode));
    if (!vmode_g)
        return;

    // FIXME gfx mode not complete
    switch (GET_GLOBAL(vmode_g->memmodel)) {
    case CTEXT:
    case MTEXT:
        write_text_char(vmode_g, cp, ca);
        break;
    case PLANAR4:
    case PLANAR1:
        write_gfx_char_pl4(vmode_g, cp, ca);
        break;
    case CGA:
        write_gfx_char_cga(vmode_g, cp, ca);
        break;
    case LINEAR8:
        write_gfx_char_lin(vmode_g, cp, ca);
        break;
    }
}

struct carattr
vgafb_read_char(struct cursorpos cp)
{
    // Get the mode
    struct vgamode_s *vmode_g = find_vga_entry(GET_BDA(video_mode));
    if (!vmode_g)
        goto fail;

    if (!(GET_GLOBAL(vmode_g->memmodel) & TEXT)) {
        // FIXME gfx mode
        dprintf(1, "Read char in graphics mode\n");
        goto fail;
    }

    // Get the dimensions
    u16 nbrows = GET_BDA(video_rows) + 1;
    u16 nbcols = GET_BDA(video_cols);

    // Compute the address
    u16 *address_far = (void*)(SCREEN_MEM_START(nbcols, nbrows, cp.page)
                               + (cp.x + cp.y * nbcols) * 2);
    u16 v = GET_FARVAR(GET_GLOBAL(vmode_g->sstart), *address_far);
    struct carattr ca = {v, v>>8, 0};
    return ca;

fail: ;
    struct carattr ca2 = {0, 0, 0};
    return ca2;
}


/****************************************************************
 * Read/write pixels
 ****************************************************************/

void
biosfn_write_pixel(u8 BH, u8 AL, u16 CX, u16 DX)
{
    // Get the mode
    struct vgamode_s *vmode_g = find_vga_entry(GET_BDA(video_mode));
    if (!vmode_g)
        return;
    if (GET_GLOBAL(vmode_g->memmodel) & TEXT)
        return;

    u8 *addr_far, mask, attr, data;
    switch (GET_GLOBAL(vmode_g->memmodel)) {
    case PLANAR4:
    case PLANAR1:
        addr_far = (void*)(CX / 8 + DX * GET_BDA(video_cols));
        mask = 0x80 >> (CX & 0x07);
        outw((mask << 8) | 0x08, VGAREG_GRDC_ADDRESS);
        outw(0x0205, VGAREG_GRDC_ADDRESS);
        data = GET_FARVAR(SEG_GRAPH, *addr_far);
        if (AL & 0x80)
            outw(0x1803, VGAREG_GRDC_ADDRESS);
        SET_FARVAR(SEG_GRAPH, *addr_far, AL);
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
        data = GET_FARVAR(SEG_CTEXT, *addr_far);
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
        SET_FARVAR(SEG_CTEXT, *addr_far, data);
        break;
    case LINEAR8:
        addr_far = (void*)(CX + DX * (GET_BDA(video_cols) * 8));
        SET_FARVAR(SEG_GRAPH, *addr_far, AL);
        break;
    }
}

void
biosfn_read_pixel(u8 BH, u16 CX, u16 DX, u16 *AX)
{
    // Get the mode
    struct vgamode_s *vmode_g = find_vga_entry(GET_BDA(video_mode));
    if (!vmode_g)
        return;
    if (GET_GLOBAL(vmode_g->memmodel) & TEXT)
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
            data = GET_FARVAR(SEG_GRAPH, *addr_far) & mask;
            if (data > 0)
                attr |= (0x01 << i);
        }
        break;
    case CGA:
        addr_far = (void*)((CX >> 2) + (DX >> 1) * 80);
        if (DX & 1)
            addr_far += 0x2000;
        data = GET_FARVAR(SEG_CTEXT, *addr_far);
        if (GET_GLOBAL(vmode_g->pixbits) == 2)
            attr = (data >> ((3 - (CX & 0x03)) * 2)) & 0x03;
        else
            attr = (data >> (7 - (CX & 0x07))) & 0x01;
        break;
    case LINEAR8:
        addr_far = (void*)(CX + DX * (GET_BDA(video_cols) * 8));
        attr = GET_FARVAR(SEG_GRAPH, *addr_far);
        break;
    }
    *AX = (*AX & 0xff00) | attr;
}


/****************************************************************
 * Font loading
 ****************************************************************/

void
vgafb_load_font(u16 seg, void *src_far, u16 count
                , u16 start, u8 destflags, u8 fontsize)
{
    get_font_access();
    u16 blockaddr = ((destflags & 0x03) << 14) + ((destflags & 0x04) << 11);
    void *dest_far = (void*)(blockaddr + start*32);
    u16 i;
    for (i = 0; i < count; i++)
        memcpy_far(SEG_GRAPH, dest_far + i*32
                   , seg, src_far + i*fontsize, fontsize);
    release_font_access();
}
