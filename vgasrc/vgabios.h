#ifndef __VGABIOS_H
#define __VGABIOS_H

#include "types.h" // u8
#include "farptr.h" // struct segoff_s

#define SCREEN_IO_START(x,y,p) (((((x)*(y)) | 0x00ff) + 1) * (p))
#define SCREEN_MEM_START(x,y,p) SCREEN_IO_START(((x)*2),(y),(p))

struct saveBDAstate {
    u8 video_mode;
    u16 video_cols;
    u16 video_pagesize;
    u16 crtc_address;
    u8 video_rows;
    u16 char_height;
    u8 video_ctl;
    u8 video_switches;
    u8 modeset_ctl;
    u16 cursor_type;
    u16 cursor_pos[8];
    u16 video_pagestart;
    u8 video_page;
    /* current font */
    struct segoff_s font0;
    struct segoff_s font1;
};

// Mode flags
#define MF_GRAYSUM    0x0002
#define MF_NOPALETTE  0x0008
#define MF_CUSTOMCRTC 0x0800
#define MF_LINEARFB   0x4000
#define MF_NOCLEARMEM 0x8000

// Memory model types
#define MM_TEXT            0x00
#define MM_CGA             0x01
#define MM_HERCULES        0x02
#define MM_PLANAR          0x03
#define MM_PACKED          0x04
#define MM_NON_CHAIN_4_256 0x05
#define MM_DIRECT          0x06
#define MM_YUV             0x07

// vgatables.c
struct vgamode_s;
struct vgamode_s *find_vga_entry(u8 mode);
void build_video_param(void);
extern struct VideoSavePointer_s video_save_pointer_table;
extern u8 static_functionality[];

// vgafonts.c
extern u8 vgafont8[];
extern u8 vgafont14[];
extern u8 vgafont16[];
extern u8 vgafont14alt[];
extern u8 vgafont16alt[];

// vgabios.c
extern u16 VgaBDF;
#define SET_VGA(var, val) SET_FARVAR(get_global_seg(), (var), (val))
struct carattr {
    u8 car, attr, use_attr;
};
struct cursorpos {
    u8 x, y, page;
};
void modeswitch_set_bda(int mode, int flags, struct vgamode_s *vmode_g);

// vgafb.c
void vgafb_scroll(int nblines, int attr
                  , struct cursorpos ul, struct cursorpos lr);
void vgafb_write_char(struct cursorpos cp, struct carattr ca);
struct carattr vgafb_read_char(struct cursorpos cp);
void vgafb_write_pixel(u8 color, u16 x, u16 y);
u8 vgafb_read_pixel(u16 x, u16 y);

// vbe.c
#define VBE_OEM_STRING "SeaBIOS VBE(C) 2011"
#define VBE_VENDOR_STRING "SeaBIOS Developers"
#define VBE_PRODUCT_STRING "SeaBIOS VBE Adapter"
#define VBE_REVISION_STRING "Rev. 1"

struct vbe_modeinfo
{
    u16 width;
    u16 height;
    u8 depth;
    u16 linesize;
    u32 phys_base;
    u32 vram_size;
};

struct bregs;
void handle_104f(struct bregs *regs);

#endif // vgabios.h
