// Code for manipulating VGA framebuffers.
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2001-2008 the LGPL VGABios developers Team
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "vgabios.h" // vgafb_scroll
#include "biosvar.h" // GET_BDA
#include "util.h" // memset_far
#include "byteorder.h" // cpu_to_be16
#include "stdvga.h" // stdvga_planar4_plane


/****************************************************************
 * Screen scrolling
 ****************************************************************/

static inline void *
memcpy_stride(u16 seg, void *dst, void *src, int copylen, int stride, int lines)
{
    for (; lines; lines--, dst+=stride, src+=stride)
        memcpy_far(seg, dst, seg, src, copylen);
    return dst;
}

static inline void
memset_stride(u16 seg, void *dst, u8 val, int setlen, int stride, int lines)
{
    for (; lines; lines--, dst+=stride)
        memset_far(seg, dst, val, setlen);
}

static inline void
memset16_stride(u16 seg, void *dst, u16 val, int setlen, int stride, int lines)
{
    for (; lines; lines--, dst+=stride)
        memset16_far(seg, dst, val, setlen);
}

static void
scroll_pl4(struct vgamode_s *vmode_g, int nblines, int attr
           , struct cursorpos ul, struct cursorpos lr)
{
    int cheight = GET_GLOBAL(vmode_g->cheight);
    int cwidth = 1;
    int stride = GET_BDA(video_cols) * cwidth;
    void *src_far, *dest_far;
    if (nblines >= 0) {
        dest_far = (void*)(ul.y * cheight * stride + ul.x * cwidth);
        src_far = dest_far + nblines * cheight * stride;
    } else {
        // Scroll down
        nblines = -nblines;
        dest_far = (void*)(lr.y * cheight * stride + ul.x * cwidth);
        src_far = dest_far - nblines * cheight * stride;
        stride = -stride;
    }
    if (attr < 0)
        attr = 0;
    int cols = lr.x - ul.x + 1;
    int rows = lr.y - ul.y + 1;
    int i;
    for (i=0; i<4; i++) {
        stdvga_planar4_plane(i);
        void *dest = dest_far;
        if (nblines < rows)
            dest = memcpy_stride(SEG_GRAPH, dest, src_far, cols * cwidth
                                 , stride, (rows - nblines) * cheight);
        u8 pixels = (attr & (1<<i)) ? 0xff : 0x00;
        memset_stride(SEG_GRAPH, dest, pixels, cols * cwidth
                      , stride, nblines * cheight);
    }
    stdvga_planar4_plane(-1);
}

static void
scroll_cga(struct vgamode_s *vmode_g, int nblines, int attr
            , struct cursorpos ul, struct cursorpos lr)
{
    int cheight = GET_GLOBAL(vmode_g->cheight) / 2;
    int cwidth = GET_GLOBAL(vmode_g->depth);
    int stride = GET_BDA(video_cols) * cwidth;
    void *src_far, *dest_far;
    if (nblines >= 0) {
        dest_far = (void*)(ul.y * cheight * stride + ul.x * cwidth);
        src_far = dest_far + nblines * cheight * stride;
    } else {
        // Scroll down
        nblines = -nblines;
        dest_far = (void*)(lr.y * cheight * stride + ul.x * cwidth);
        src_far = dest_far - nblines * cheight * stride;
        stride = -stride;
    }
    if (attr < 0)
        attr = 0;
    if (cwidth == 1)
        attr = (attr&1) | ((attr&1)<<1);
    attr &= 3;
    attr |= (attr<<2) | (attr<<4) | (attr<<6);
    int cols = lr.x - ul.x + 1;
    int rows = lr.y - ul.y + 1;
    if (nblines < rows) {
        memcpy_stride(SEG_CTEXT, dest_far+0x2000, src_far+0x2000, cols * cwidth
                      , stride, (rows - nblines) * cheight);
        dest_far = memcpy_stride(SEG_CTEXT, dest_far, src_far, cols * cwidth
                                 , stride, (rows - nblines) * cheight);
    }
    memset_stride(SEG_CTEXT, dest_far + 0x2000, attr, cols * cwidth
                  , stride, nblines * cheight);
    memset_stride(SEG_CTEXT, dest_far, attr, cols * cwidth
                  , stride, nblines * cheight);
}

static void
scroll_lin(struct vgamode_s *vmode_g, int nblines, int attr
           , struct cursorpos ul, struct cursorpos lr)
{
    int cheight = 8;
    int cwidth = 8;
    int stride = GET_BDA(video_cols) * cwidth;
    void *src_far, *dest_far;
    if (nblines >= 0) {
        dest_far = (void*)(ul.y * cheight * stride + ul.x * cwidth);
        src_far = dest_far + nblines * cheight * stride;
    } else {
        // Scroll down
        nblines = -nblines;
        dest_far = (void*)(lr.y * cheight * stride + ul.x * cwidth);
        src_far = dest_far - nblines * cheight * stride;
        stride = -stride;
    }
    if (attr < 0)
        attr = 0;
    int cols = lr.x - ul.x + 1;
    int rows = lr.y - ul.y + 1;
    if (nblines < rows)
        dest_far = memcpy_stride(SEG_GRAPH, dest_far, src_far, cols * cwidth
                                 , stride, (rows - nblines) * cheight);
    memset_stride(SEG_GRAPH, dest_far, attr, cols * cwidth
                  , stride, nblines * cheight);
}

static void
scroll_text(struct vgamode_s *vmode_g, int nblines, int attr
            , struct cursorpos ul, struct cursorpos lr)
{
    int cheight = 1;
    int cwidth = 2;
    int stride = GET_BDA(video_cols) * cwidth;
    void *src_far, *dest_far = (void*)(GET_BDA(video_pagesize) * ul.page);
    if (nblines >= 0) {
        dest_far += ul.y * cheight * stride + ul.x * cwidth;
        src_far = dest_far + nblines * cheight * stride;
    } else {
        // Scroll down
        nblines = -nblines;
        dest_far += lr.y * cheight * stride + ul.x * cwidth;
        src_far = dest_far - nblines * cheight * stride;
        stride = -stride;
    }
    if (attr < 0)
        attr = 0x07;
    attr = (attr << 8) | ' ';
    int cols = lr.x - ul.x + 1;
    int rows = lr.y - ul.y + 1;
    u16 seg = GET_GLOBAL(vmode_g->sstart);
    if (nblines < rows)
        dest_far = memcpy_stride(seg, dest_far, src_far, cols * cwidth
                                 , stride, (rows - nblines) * cheight);
    memset16_stride(seg, dest_far, attr, cols * cwidth
                    , stride, nblines * cheight);
}

void
vgafb_scroll(int nblines, int attr, struct cursorpos ul, struct cursorpos lr)
{
    // Get the mode
    struct vgamode_s *vmode_g = get_current_mode();
    if (!vmode_g)
        return;

    // FIXME gfx mode not complete
    switch (GET_GLOBAL(vmode_g->memmodel)) {
    case MM_TEXT:
        scroll_text(vmode_g, nblines, attr, ul, lr);
        break;
    case MM_PLANAR:
        scroll_pl4(vmode_g, nblines, attr, ul, lr);
        break;
    case MM_CGA:
        scroll_cga(vmode_g, nblines, attr, ul, lr);
        break;
    case MM_DIRECT:
    case MM_PACKED:
        scroll_lin(vmode_g, nblines, attr, ul, lr);
        break;
    default:
        break;
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

    u8 cheight = GET_GLOBAL(vmode_g->cheight);
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
    int i;
    for (i=0; i<4; i++) {
        stdvga_planar4_plane(i);
        u8 colors = ((ca.attr & (1<<i)) ? 0xff : 0x00);
        int j;
        for (j = 0; j < cheight; j++) {
            u8 *dest_far = (void*)(addr + j * nbcols);
            u8 pixels = colors & GET_GLOBAL(fdata_g[src + j]);
            if (ca.attr & 0x80)
                pixels ^= GET_FARVAR(SEG_GRAPH, *dest_far);
            SET_FARVAR(SEG_GRAPH, *dest_far, pixels);
        }
    }
    stdvga_planar4_plane(-1);
}

static void
write_gfx_char_cga(struct vgamode_s *vmode_g
                   , struct cursorpos cp, struct carattr ca)
{
    u16 nbcols = GET_BDA(video_cols);
    if (cp.x >= nbcols)
        return;

    u8 *fdata_g = vgafont8;
    u8 bpp = GET_GLOBAL(vmode_g->depth);
    u16 addr = (cp.x * bpp) + cp.y * 320;
    u16 src = ca.car * 8;
    u8 i;
    for (i = 0; i < 8; i++) {
        u8 *dest_far = (void*)(addr + (i >> 1) * 80);
        if (i & 1)
            dest_far += 0x2000;
        if (bpp == 1) {
            u8 colors = (ca.attr & 0x01) ? 0xff : 0x00;
            u8 pixels = colors & GET_GLOBAL(fdata_g[src + i]);
            if (ca.attr & 0x80)
                pixels ^= GET_FARVAR(SEG_GRAPH, *dest_far);
            SET_FARVAR(SEG_CTEXT, *dest_far, pixels);
        } else {
            u16 pixels = 0;
            u8 fontline = GET_GLOBAL(fdata_g[src + i]);
            int j;
            for (j = 0; j < 8; j++)
                if (fontline & (1<<j))
                    pixels |= (ca.attr & 0x03) << (j*2);
            pixels = cpu_to_be16(pixels);
            if (ca.attr & 0x80)
                pixels ^= GET_FARVAR(SEG_GRAPH, *(u16*)dest_far);
            SET_FARVAR(SEG_CTEXT, *(u16*)dest_far, pixels);
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
        u8 fontline = GET_GLOBAL(fdata_g[src + i]);
        u8 j;
        for (j = 0; j < 8; j++) {
            u8 pixel = (fontline & (0x80>>j)) ? ca.attr : 0x00;
            SET_FARVAR(SEG_GRAPH, dest_far[j], pixel);
        }
    }
}

static void
write_text_char(struct vgamode_s *vmode_g
                , struct cursorpos cp, struct carattr ca)
{
    // Compute the address
    u16 nbcols = GET_BDA(video_cols);
    void *address_far = (void*)(GET_BDA(video_pagesize) * cp.page
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
    struct vgamode_s *vmode_g = get_current_mode();
    if (!vmode_g)
        return;

    // FIXME gfx mode not complete
    switch (GET_GLOBAL(vmode_g->memmodel)) {
    case MM_TEXT:
        write_text_char(vmode_g, cp, ca);
        break;
    case MM_PLANAR:
        write_gfx_char_pl4(vmode_g, cp, ca);
        break;
    case MM_CGA:
        write_gfx_char_cga(vmode_g, cp, ca);
        break;
    case MM_DIRECT:
    case MM_PACKED:
        write_gfx_char_lin(vmode_g, cp, ca);
        break;
    default:
        break;
    }
}

struct carattr
vgafb_read_char(struct cursorpos cp)
{
    // Get the mode
    struct vgamode_s *vmode_g = get_current_mode();
    if (!vmode_g)
        goto fail;

    if (GET_GLOBAL(vmode_g->memmodel) != MM_TEXT) {
        // FIXME gfx mode
        dprintf(1, "Read char in graphics mode\n");
        goto fail;
    }

    // Compute the address
    u16 nbcols = GET_BDA(video_cols);
    u16 *address_far = (void*)(GET_BDA(video_pagesize) * cp.page
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
vgafb_write_pixel(u8 color, u16 x, u16 y)
{
    // Get the mode
    struct vgamode_s *vmode_g = get_current_mode();
    if (!vmode_g)
        return;

    u8 *addr_far, mask, attr, data, i;
    switch (GET_GLOBAL(vmode_g->memmodel)) {
    case MM_PLANAR:
        addr_far = (void*)(x / 8 + y * GET_BDA(video_cols));
        mask = 0x80 >> (x & 0x07);
        for (i=0; i<4; i++) {
            stdvga_planar4_plane(i);
            u8 colors = (color & (1<<i)) ? 0xff : 0x00;
            u8 orig = GET_FARVAR(SEG_GRAPH, *addr_far);
            if (color & 0x80)
                colors ^= orig;
            SET_FARVAR(SEG_GRAPH, *addr_far, (colors & mask) | (orig & ~mask));
        }
        stdvga_planar4_plane(-1);
        break;
    case MM_CGA:
        if (GET_GLOBAL(vmode_g->depth) == 2)
            addr_far = (void*)((x >> 2) + (y >> 1) * 80);
        else
            addr_far = (void*)((x >> 3) + (y >> 1) * 80);
        if (y & 1)
            addr_far += 0x2000;
        data = GET_FARVAR(SEG_CTEXT, *addr_far);
        if (GET_GLOBAL(vmode_g->depth) == 2) {
            attr = (color & 0x03) << ((3 - (x & 0x03)) * 2);
            mask = 0x03 << ((3 - (x & 0x03)) * 2);
        } else {
            attr = (color & 0x01) << (7 - (x & 0x07));
            mask = 0x01 << (7 - (x & 0x07));
        }
        if (color & 0x80) {
            data ^= attr;
        } else {
            data &= ~mask;
            data |= attr;
        }
        SET_FARVAR(SEG_CTEXT, *addr_far, data);
        break;
    case MM_DIRECT:
    case MM_PACKED:
        addr_far = (void*)(x + y * (GET_BDA(video_cols) * 8));
        SET_FARVAR(SEG_GRAPH, *addr_far, color);
        break;
    default:
    case MM_TEXT:
        return;
    }
}

u8
vgafb_read_pixel(u16 x, u16 y)
{
    // Get the mode
    struct vgamode_s *vmode_g = get_current_mode();
    if (!vmode_g)
        return 0;

    u8 *addr_far, mask, attr=0, data, i;
    switch (GET_GLOBAL(vmode_g->memmodel)) {
    case MM_PLANAR:
        addr_far = (void*)(x / 8 + y * GET_BDA(video_cols));
        mask = 0x80 >> (x & 0x07);
        attr = 0x00;
        for (i = 0; i < 4; i++) {
            stdvga_planar4_plane(i);
            data = GET_FARVAR(SEG_GRAPH, *addr_far) & mask;
            if (data > 0)
                attr |= (0x01 << i);
        }
        stdvga_planar4_plane(-1);
        break;
    case MM_CGA:
        addr_far = (void*)((x >> 2) + (y >> 1) * 80);
        if (y & 1)
            addr_far += 0x2000;
        data = GET_FARVAR(SEG_CTEXT, *addr_far);
        if (GET_GLOBAL(vmode_g->depth) == 2)
            attr = (data >> ((3 - (x & 0x03)) * 2)) & 0x03;
        else
            attr = (data >> (7 - (x & 0x07))) & 0x01;
        break;
    case MM_DIRECT:
    case MM_PACKED:
        addr_far = (void*)(x + y * (GET_BDA(video_cols) * 8));
        attr = GET_FARVAR(SEG_GRAPH, *addr_far);
        break;
    default:
    case MM_TEXT:
        return 0;
    }
    return attr;
}
