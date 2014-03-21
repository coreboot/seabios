// Code for manipulating VGA framebuffers.
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2001-2008 the LGPL VGABios developers Team
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "biosvar.h" // GET_BDA
#include "byteorder.h" // cpu_to_be16
#include "output.h" // dprintf
#include "stdvga.h" // stdvga_planar4_plane
#include "string.h" // memset_far
#include "vgabios.h" // vgafb_scroll


/****************************************************************
 * Screen scrolling
 ****************************************************************/

static inline void
memmove_stride(u16 seg, void *dst, void *src, int copylen, int stride, int lines)
{
    if (src < dst) {
        dst += stride * (lines - 1);
        src += stride * (lines - 1);
        stride = -stride;
    }
    for (; lines; lines--, dst+=stride, src+=stride)
        memcpy_far(seg, dst, seg, src, copylen);
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
planar_move_chars(struct vgamode_s *vmode_g, struct cursorpos dest
                  , struct cursorpos src, struct cursorpos movesize)
{
    if (!CONFIG_VGA_STDVGA_PORTS)
        return;
    int cheight = GET_BDA(char_height);
    int cwidth = 1;
    int stride = GET_BDA(video_cols) * cwidth;
    void *dest_far = (void*)(dest.y * cheight * stride + dest.x * cwidth);
    void *src_far = (void*)(src.y * cheight * stride + src.x * cwidth);
    int i;
    for (i=0; i<4; i++) {
        stdvga_planar4_plane(i);
        memmove_stride(SEG_GRAPH, dest_far, src_far
                       , movesize.x * cwidth, stride, movesize.y * cheight);
    }
    stdvga_planar4_plane(-1);
}

static void
planar_clear_chars(struct vgamode_s *vmode_g, struct cursorpos dest
                   , struct carattr ca, struct cursorpos clearsize)
{
    if (!CONFIG_VGA_STDVGA_PORTS)
        return;
    int cheight = GET_BDA(char_height);
    int cwidth = 1;
    int stride = GET_BDA(video_cols) * cwidth;
    void *dest_far = (void*)(dest.y * cheight * stride + dest.x * cwidth);
    int i;
    for (i=0; i<4; i++) {
        stdvga_planar4_plane(i);
        u8 attr = (ca.attr & (1<<i)) ? 0xff : 0x00;
        memset_stride(SEG_GRAPH, dest_far, attr
                      , clearsize.x * cwidth, stride, clearsize.y * cheight);
    }
    stdvga_planar4_plane(-1);
}

static void
cga_move_chars(struct vgamode_s *vmode_g, struct cursorpos dest
               , struct cursorpos src, struct cursorpos movesize)
{
    int cheight = GET_BDA(char_height) / 2;
    int cwidth = GET_GLOBAL(vmode_g->depth);
    int stride = GET_BDA(video_cols) * cwidth;
    void *dest_far = (void*)(dest.y * cheight * stride + dest.x * cwidth);
    void *src_far = (void*)(src.y * cheight * stride + src.x * cwidth);
    memmove_stride(SEG_CTEXT, dest_far, src_far
                   , movesize.x * cwidth, stride, movesize.y * cheight);
    memmove_stride(SEG_CTEXT, dest_far + 0x2000, src_far + 0x2000
                   , movesize.x * cwidth, stride, movesize.y * cheight);
}

static void
cga_clear_chars(struct vgamode_s *vmode_g, struct cursorpos dest
                , struct carattr ca, struct cursorpos clearsize)
{
    int cheight = GET_BDA(char_height) / 2;
    int cwidth = GET_GLOBAL(vmode_g->depth);
    int stride = GET_BDA(video_cols) * cwidth;
    void *dest_far = (void*)(dest.y * cheight * stride + dest.x * cwidth);
    u8 attr = ca.attr;
    if (cwidth == 1)
        attr = (attr&1) | ((attr&1)<<1);
    attr &= 3;
    attr |= (attr<<2) | (attr<<4) | (attr<<6);
    memset_stride(SEG_CTEXT, dest_far, attr
                  , clearsize.x * cwidth, stride, clearsize.y * cheight);
    memset_stride(SEG_CTEXT, dest_far + 0x2000, attr
                  , clearsize.x * cwidth, stride, clearsize.y * cheight);
}

static void
packed_move_chars(struct vgamode_s *vmode_g, struct cursorpos dest
                  , struct cursorpos src, struct cursorpos movesize)
{
    int cheight = GET_BDA(char_height);
    int cwidth = 8;
    int stride = GET_BDA(video_cols) * cwidth;
    void *dest_far = (void*)(dest.y * cheight * stride + dest.x * cwidth);
    void *src_far = (void*)(src.y * cheight * stride + src.x * cwidth);
    memmove_stride(SEG_GRAPH, dest_far, src_far
                   , movesize.x * cwidth, stride, movesize.y * cheight);
}

static void
packed_clear_chars(struct vgamode_s *vmode_g, struct cursorpos dest
                   , struct carattr ca, struct cursorpos clearsize)
{
    int cheight = GET_BDA(char_height);
    int cwidth = 8;
    int stride = GET_BDA(video_cols) * cwidth;
    void *dest_far = (void*)(dest.y * cheight * stride + dest.x * cwidth);
    memset_stride(SEG_GRAPH, dest_far, ca.attr
                  , clearsize.x * cwidth, stride, clearsize.y * cheight);
}

static void
text_move_chars(struct vgamode_s *vmode_g, struct cursorpos dest
                , struct cursorpos src, struct cursorpos movesize)
{
    int cheight = 1;
    int cwidth = 2;
    int stride = GET_BDA(video_cols) * cwidth;
    void *dest_far = (void*)(dest.y * cheight * stride + dest.x * cwidth);
    void *src_far = (void*)(src.y * cheight * stride + src.x * cwidth);
    u32 pageoffset = GET_BDA(video_pagesize) * dest.page;
    u16 seg = GET_GLOBAL(vmode_g->sstart);
    memmove_stride(seg, dest_far + pageoffset, src_far + pageoffset
                   , movesize.x * cwidth, stride, movesize.y * cheight);
}

static void
text_clear_chars(struct vgamode_s *vmode_g, struct cursorpos dest
                , struct carattr ca, struct cursorpos clearsize)
{
    int cheight = 1;
    int cwidth = 2;
    int stride = GET_BDA(video_cols) * cwidth;
    void *dest_far = (void*)(dest.y * cheight * stride + dest.x * cwidth);
    u16 attr = ((ca.use_attr ? ca.attr : 0x07) << 8) | ca.car;
    u32 pageoffset = GET_BDA(video_pagesize) * dest.page;
    u16 seg = GET_GLOBAL(vmode_g->sstart);
    memset16_stride(seg, dest_far + pageoffset, attr
                    , clearsize.x * cwidth, stride, clearsize.y * cheight);
}

void
vgafb_move_chars(struct vgamode_s *vmode_g, struct cursorpos dest
                 , struct cursorpos src, struct cursorpos movesize)
{
    switch (GET_GLOBAL(vmode_g->memmodel)) {
    case MM_TEXT:
        text_move_chars(vmode_g, dest, src, movesize);
        break;
    case MM_PLANAR:
        planar_move_chars(vmode_g, dest, src, movesize);
        break;
    case MM_CGA:
        cga_move_chars(vmode_g, dest, src, movesize);
        break;
    case MM_PACKED:
        packed_move_chars(vmode_g, dest, src, movesize);
        break;
    default:
        break;
    }
}

void
vgafb_clear_chars(struct vgamode_s *vmode_g, struct cursorpos dest
                 , struct carattr ca, struct cursorpos movesize)
{
    switch (GET_GLOBAL(vmode_g->memmodel)) {
    case MM_TEXT:
        text_clear_chars(vmode_g, dest, ca, movesize);
        break;
    case MM_PLANAR:
        planar_clear_chars(vmode_g, dest, ca, movesize);
        break;
    case MM_CGA:
        cga_clear_chars(vmode_g, dest, ca, movesize);
        break;
    case MM_PACKED:
        packed_clear_chars(vmode_g, dest, ca, movesize);
        break;
    default:
        break;
    }
}


/****************************************************************
 * Read/write characters to screen
 ****************************************************************/

static struct segoff_s
get_font_data(u8 c)
{
    int char_height = GET_BDA(char_height);
    struct segoff_s font;
    if (char_height == 8 && c >= 128) {
        font = GET_IVT(0x1f);
        c -= 128;
    } else {
        font = GET_IVT(0x43);
    }
    font.offset += c * char_height;
    return font;
}

static void
write_gfx_char_pl4(struct vgamode_s *vmode_g
                   , struct cursorpos cp, struct carattr ca)
{
    if (!CONFIG_VGA_STDVGA_PORTS)
        return;
    u16 nbcols = GET_BDA(video_cols);
    if (cp.x >= nbcols)
        return;

    struct segoff_s font = get_font_data(ca.car);
    int cheight = GET_BDA(char_height);
    int cwidth = 1;
    int stride = nbcols * cwidth;
    int addr = cp.y * cheight * stride + cp.x * cwidth;
    int i;
    for (i=0; i<4; i++) {
        stdvga_planar4_plane(i);
        u8 colors = ((ca.attr & (1<<i)) ? 0xff : 0x00);
        int j;
        for (j = 0; j < cheight; j++) {
            u8 *dest_far = (void*)(addr + j * stride);
            u8 fontline = GET_FARVAR(font.seg, *(u8*)(font.offset+j));
            u8 pixels = colors & fontline;
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

    struct segoff_s font = get_font_data(ca.car);
    int cheight = GET_BDA(char_height) / 2;
    int cwidth = GET_GLOBAL(vmode_g->depth);
    int stride = nbcols * cwidth;
    int addr = cp.y * cheight * stride + cp.x * cwidth;
    int i;
    for (i = 0; i < cheight*2; i++) {
        u8 *dest_far = (void*)(addr + (i >> 1) * stride);
        if (i & 1)
            dest_far += 0x2000;
        u8 fontline = GET_FARVAR(font.seg, *(u8*)(font.offset+i));
        if (cwidth == 1) {
            u8 colors = (ca.attr & 0x01) ? 0xff : 0x00;
            u8 pixels = colors & fontline;
            if (ca.attr & 0x80)
                pixels ^= GET_FARVAR(SEG_GRAPH, *dest_far);
            SET_FARVAR(SEG_CTEXT, *dest_far, pixels);
        } else {
            u16 fontline16 = ((fontline & 0xf0) << 4) | (fontline & 0x0f);
            fontline16 = ((fontline16 & 0x0c0c) << 2) | (fontline16 & 0x0303);
            fontline16 = ((fontline16 & 0x2222) << 1) | (fontline16 & 0x1111);
            fontline16 |= fontline16<<1;
            u16 colors = (((ca.attr & 0x01) ? 0x5555 : 0x0000)
                          | ((ca.attr & 0x02) ? 0xaaaa : 0x0000));
            u16 pixels = cpu_to_be16(colors & fontline16);
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

    struct segoff_s font = get_font_data(ca.car);
    int cheight = GET_BDA(char_height);
    int cwidth = 8;
    int stride = nbcols * cwidth;
    int addr = cp.y * cheight * stride + cp.x * cwidth;
    int i;
    for (i = 0; i < cheight; i++) {
        u8 *dest_far = (void*)(addr + i * stride);
        u8 fontline = GET_FARVAR(font.seg, *(u8*)(font.offset+i));
        int j;
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
    int cheight = 1;
    int cwidth = 2;
    int stride = GET_BDA(video_cols) * cwidth;
    int addr = cp.y * cheight * stride + cp.x * cwidth;
    void *dest_far = (void*)(GET_BDA(video_pagesize) * cp.page + addr);
    if (ca.use_attr) {
        u16 dummy = (ca.attr << 8) | ca.car;
        SET_FARVAR(GET_GLOBAL(vmode_g->sstart), *(u16*)dest_far, dummy);
    } else {
        SET_FARVAR(GET_GLOBAL(vmode_g->sstart), *(u8*)dest_far, ca.car);
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
    int cheight = 1;
    int cwidth = 2;
    int stride = GET_BDA(video_cols) * cwidth;
    int addr = cp.y * cheight * stride + cp.x * cwidth;
    u16 *src_far = (void*)(GET_BDA(video_pagesize) * cp.page + addr);
    u16 v = GET_FARVAR(GET_GLOBAL(vmode_g->sstart), *src_far);
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
        if (!CONFIG_VGA_STDVGA_PORTS)
            return;
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
        if (!CONFIG_VGA_STDVGA_PORTS)
            return 0;
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
