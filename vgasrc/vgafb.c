// Code for manipulating VGA framebuffers.
//
// Copyright (C) 2009-2014  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2001-2008 the LGPL VGABios developers Team
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "biosvar.h" // GET_BDA
#include "byteorder.h" // cpu_to_be16
#include "output.h" // dprintf
#include "stdvga.h" // stdvga_planar4_plane
#include "string.h" // memset_far
#include "vgabios.h" // vgafb_scroll
#include "vgahw.h" // vgahw_get_linelength

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


/****************************************************************
 * Basic stdvga graphic manipulation
 ****************************************************************/

static void
gfx_planar(struct gfx_op *op)
{
    if (!CONFIG_VGA_STDVGA_PORTS)
        return;
    void *dest_far = (void*)(op->y * op->linelength + op->x / 8);
    int plane;
    switch (op->op) {
    default:
    case GO_READ8:
        memset(op->pixels, 0, sizeof(op->pixels));
        for (plane = 0; plane < 4; plane++) {
            stdvga_planar4_plane(plane);
            u8 data = GET_FARVAR(SEG_GRAPH, *(u8*)dest_far);
            int pixel;
            for (pixel=0; pixel<8; pixel++)
                op->pixels[pixel] |= ((data>>(7-pixel)) & 1) << plane;
        }
        break;
    case GO_WRITE8:
        for (plane = 0; plane<4; plane++) {
            stdvga_planar4_plane(plane);
            u8 data = 0;
            int pixel;
            for (pixel=0; pixel<8; pixel++)
                data |= ((op->pixels[pixel]>>plane) & 1) << (7-pixel);
            SET_FARVAR(SEG_GRAPH, *(u8*)dest_far, data);
        }
        break;
    case GO_MEMSET:
        for (plane = 0; plane < 4; plane++) {
            stdvga_planar4_plane(plane);
            u8 data = (op->pixels[0] & (1<<plane)) ? 0xff : 0x00;
            memset_stride(SEG_GRAPH, dest_far, data
                          , op->xlen / 8, op->linelength, op->ylen);
        }
        break;
    case GO_MEMMOVE: ;
        void *src_far = (void*)(op->srcy * op->linelength + op->x / 8);
        for (plane = 0; plane < 4; plane++) {
            stdvga_planar4_plane(plane);
            memmove_stride(SEG_GRAPH, dest_far, src_far
                           , op->xlen / 8, op->linelength, op->ylen);
        }
        break;
    }
    stdvga_planar4_plane(-1);
}

static void
gfx_cga(struct gfx_op *op)
{
    int bpp = GET_GLOBAL(op->vmode_g->depth);
    void *dest_far = (void*)(op->y / 2 * op->linelength + op->x / 8 * bpp);
    switch (op->op) {
    default:
    case GO_READ8:
        if (op->y & 1)
            dest_far += 0x2000;
        if (bpp == 1) {
            u8 data = GET_FARVAR(SEG_CTEXT, *(u8*)dest_far);
            int pixel;
            for (pixel=0; pixel<8; pixel++)
                op->pixels[pixel] = (data >> (7-pixel)) & 1;
        } else {
            u16 data = GET_FARVAR(SEG_CTEXT, *(u16*)dest_far);
            data = be16_to_cpu(data);
            int pixel;
            for (pixel=0; pixel<8; pixel++)
                op->pixels[pixel] = (data >> ((7-pixel)*2)) & 3;
        }
        break;
    case GO_WRITE8:
        if (op->y & 1)
            dest_far += 0x2000;
        if (bpp == 1) {
            u8 data = 0;
            int pixel;
            for (pixel=0; pixel<8; pixel++)
                data |= (op->pixels[pixel] & 1) << (7-pixel);
            SET_FARVAR(SEG_CTEXT, *(u8*)dest_far, data);
        } else {
            u16 data = 0;
            int pixel;
            for (pixel=0; pixel<8; pixel++)
                data |= (op->pixels[pixel] & 3) << ((7-pixel) * 2);
            data = cpu_to_be16(data);
            SET_FARVAR(SEG_CTEXT, *(u16*)dest_far, data);
        }
        break;
    case GO_MEMSET: ;
        u8 data = op->pixels[0];
        if (bpp == 1)
            data = (data&1) | ((data&1)<<1);
        data &= 3;
        data |= (data<<2) | (data<<4) | (data<<6);
        memset_stride(SEG_CTEXT, dest_far, data
                      , op->xlen / 8 * bpp, op->linelength, op->ylen / 2);
        memset_stride(SEG_CTEXT, dest_far + 0x2000, data
                      , op->xlen / 8 * bpp, op->linelength, op->ylen / 2);
        break;
    case GO_MEMMOVE: ;
        void *src_far = (void*)(op->srcy / 2 * op->linelength + op->x / 8 * bpp);
        memmove_stride(SEG_CTEXT, dest_far, src_far
                       , op->xlen / 8 * bpp, op->linelength, op->ylen / 2);
        memmove_stride(SEG_CTEXT, dest_far + 0x2000, src_far + 0x2000
                       , op->xlen / 8 * bpp, op->linelength, op->ylen / 2);
        break;
    }
}

static void
gfx_packed(struct gfx_op *op)
{
    void *dest_far = (void*)(op->y * op->linelength + op->x);
    switch (op->op) {
    default:
    case GO_READ8:
        memcpy_far(GET_SEG(SS), op->pixels, SEG_GRAPH, dest_far, 8);
        break;
    case GO_WRITE8:
        memcpy_far(SEG_GRAPH, dest_far, GET_SEG(SS), op->pixels, 8);
        break;
    case GO_MEMSET:
        memset_stride(SEG_GRAPH, dest_far, op->pixels[0]
                      , op->xlen, op->linelength, op->ylen);
        break;
    case GO_MEMMOVE: ;
        void *src_far = (void*)(op->srcy * op->linelength + op->x);
        memmove_stride(SEG_GRAPH, dest_far, src_far
                       , op->xlen, op->linelength, op->ylen);
        break;
    }
}


/****************************************************************
 * Gfx interface
 ****************************************************************/

// Prepare a struct gfx_op for use.
static void
init_gfx_op(struct gfx_op *op, struct vgamode_s *vmode_g)
{
    memset(op, 0, sizeof(*op));
    op->vmode_g = vmode_g;
    op->linelength = vgahw_get_linelength(vmode_g);
}

// Issue a graphics operation.
static void
handle_gfx_op(struct gfx_op *op)
{
    switch (GET_GLOBAL(op->vmode_g->memmodel)) {
    case MM_PLANAR:
        gfx_planar(op);
        break;
    case MM_CGA:
        gfx_cga(op);
        break;
    case MM_PACKED:
        gfx_packed(op);
        break;
    default:
        break;
    }
}

// Move characters when in graphics mode.
static void
gfx_move_chars(struct vgamode_s *vmode_g, struct cursorpos dest
               , struct cursorpos src, struct cursorpos movesize)
{
    struct gfx_op op;
    init_gfx_op(&op, vmode_g);
    op.x = dest.x * 8;
    op.xlen = movesize.x * 8;
    int cheight = GET_BDA(char_height);
    op.y = dest.y * cheight;
    op.ylen = movesize.y * cheight;
    op.srcy = src.y * cheight;
    op.op = GO_MEMMOVE;
    handle_gfx_op(&op);
}

// Clear are of screen in graphics mode.
static void
gfx_clear_chars(struct vgamode_s *vmode_g, struct cursorpos dest
                , struct carattr ca, struct cursorpos clearsize)
{
    struct gfx_op op;
    init_gfx_op(&op, vmode_g);
    op.x = dest.x * 8;
    op.xlen = clearsize.x * 8;
    int cheight = GET_BDA(char_height);
    op.y = dest.y * cheight;
    op.ylen = clearsize.y * cheight;
    op.pixels[0] = ca.attr;
    op.op = GO_MEMSET;
    handle_gfx_op(&op);
}

// Return the font for a given character
struct segoff_s
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

// Write a character to the screen in graphics mode.
static void
gfx_write_char(struct vgamode_s *vmode_g
                , struct cursorpos cp, struct carattr ca)
{
    if (cp.x >= GET_BDA(video_cols))
        return;

    struct segoff_s font = get_font_data(ca.car);
    struct gfx_op op;
    init_gfx_op(&op, vmode_g);
    op.x = cp.x * 8;
    int cheight = GET_BDA(char_height);
    op.y = cp.y * cheight;
    int usexor = ca.attr & 0x80 && GET_GLOBAL(vmode_g->depth) < 8;
    int i;
    for (i = 0; i < cheight; i++, op.y++) {
        u8 fontline = GET_FARVAR(font.seg, *(u8*)(font.offset+i));
        if (usexor) {
            op.op = GO_READ8;
            handle_gfx_op(&op);
            int j;
            for (j = 0; j < 8; j++)
                op.pixels[j] ^= (fontline & (0x80>>j)) ? (ca.attr & 0x7f) : 0x00;
        } else {
            int j;
            for (j = 0; j < 8; j++)
                op.pixels[j] = (fontline & (0x80>>j)) ? ca.attr : 0x00;
        }
        op.op = GO_WRITE8;
        handle_gfx_op(&op);
    }
}

// Set the pixel at the given position.
void
vgafb_write_pixel(u8 color, u16 x, u16 y)
{
    struct vgamode_s *vmode_g = get_current_mode();
    if (!vmode_g)
        return;

    struct gfx_op op;
    init_gfx_op(&op, vmode_g);
    op.x = ALIGN_DOWN(x, 8);
    op.y = y;
    op.op = GO_READ8;
    handle_gfx_op(&op);

    int usexor = color & 0x80 && GET_GLOBAL(vmode_g->depth) < 8;
    if (usexor)
        op.pixels[x & 0x07] ^= color & 0x7f;
    else
        op.pixels[x & 0x07] = color;
    op.op = GO_WRITE8;
    handle_gfx_op(&op);
}

// Return the pixel at the given position.
u8
vgafb_read_pixel(u16 x, u16 y)
{
    struct vgamode_s *vmode_g = get_current_mode();
    if (!vmode_g)
        return 0;

    struct gfx_op op;
    init_gfx_op(&op, vmode_g);
    op.x = ALIGN_DOWN(x, 8);
    op.y = y;
    op.op = GO_READ8;
    handle_gfx_op(&op);

    return op.pixels[x & 0x07];
}


/****************************************************************
 * Text ops
 ****************************************************************/

// Return the fb offset for the given character address when in text mode.
void *
text_address(struct cursorpos cp)
{
    int stride = GET_BDA(video_cols) * 2;
    u32 pageoffset = GET_BDA(video_pagesize) * cp.page;
    return (void*)pageoffset + cp.y * stride + cp.x * 2;
}

// Move characters on screen.
void
vgafb_move_chars(struct vgamode_s *vmode_g, struct cursorpos dest
                 , struct cursorpos src, struct cursorpos movesize)
{
    if (GET_GLOBAL(vmode_g->memmodel) != MM_TEXT) {
        gfx_move_chars(vmode_g, dest, src, movesize);
        return;
    }

    int stride = GET_BDA(video_cols) * 2;
    memmove_stride(GET_GLOBAL(vmode_g->sstart)
                   , text_address(dest), text_address(src)
                   , movesize.x * 2, stride, movesize.y);
}

// Clear are of screen.
void
vgafb_clear_chars(struct vgamode_s *vmode_g, struct cursorpos dest
                  , struct carattr ca, struct cursorpos clearsize)
{
    if (GET_GLOBAL(vmode_g->memmodel) != MM_TEXT) {
        gfx_clear_chars(vmode_g, dest, ca, clearsize);
        return;
    }

    int stride = GET_BDA(video_cols) * 2;
    u16 attr = ((ca.use_attr ? ca.attr : 0x07) << 8) | ca.car;
    memset16_stride(GET_GLOBAL(vmode_g->sstart), text_address(dest), attr
                    , clearsize.x * 2, stride, clearsize.y);
}

// Write a character to the screen.
void
vgafb_write_char(struct cursorpos cp, struct carattr ca)
{
    struct vgamode_s *vmode_g = get_current_mode();
    if (!vmode_g)
        return;

    if (GET_GLOBAL(vmode_g->memmodel) != MM_TEXT) {
        gfx_write_char(vmode_g, cp, ca);
        return;
    }

    void *dest_far = text_address(cp);
    if (ca.use_attr) {
        u16 dummy = (ca.attr << 8) | ca.car;
        SET_FARVAR(GET_GLOBAL(vmode_g->sstart), *(u16*)dest_far, dummy);
    } else {
        SET_FARVAR(GET_GLOBAL(vmode_g->sstart), *(u8*)dest_far, ca.car);
    }
}

// Return the character at the given position on the screen.
struct carattr
vgafb_read_char(struct cursorpos cp)
{
    struct vgamode_s *vmode_g = get_current_mode();
    if (!vmode_g)
        goto fail;

    if (GET_GLOBAL(vmode_g->memmodel) != MM_TEXT) {
        // FIXME gfx mode
        dprintf(1, "Read char in graphics mode\n");
        goto fail;
    }

    u16 *dest_far = text_address(cp);
    u16 v = GET_FARVAR(GET_GLOBAL(vmode_g->sstart), *dest_far);
    struct carattr ca = {v, v>>8, 0};
    return ca;

fail: ;
    struct carattr ca2 = {0, 0, 0};
    return ca2;
}
