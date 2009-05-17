#ifndef __VGATABLES_H
#define __VGATABLES_H

#include "types.h" // u8

/*
 *
 * VGA registers
 *
 */
#define VGAREG_ACTL_ADDRESS            0x3c0
#define VGAREG_ACTL_WRITE_DATA         0x3c0
#define VGAREG_ACTL_READ_DATA          0x3c1

#define VGAREG_INPUT_STATUS            0x3c2
#define VGAREG_WRITE_MISC_OUTPUT       0x3c2
#define VGAREG_VIDEO_ENABLE            0x3c3
#define VGAREG_SEQU_ADDRESS            0x3c4
#define VGAREG_SEQU_DATA               0x3c5

#define VGAREG_PEL_MASK                0x3c6
#define VGAREG_DAC_STATE               0x3c7
#define VGAREG_DAC_READ_ADDRESS        0x3c7
#define VGAREG_DAC_WRITE_ADDRESS       0x3c8
#define VGAREG_DAC_DATA                0x3c9

#define VGAREG_READ_FEATURE_CTL        0x3ca
#define VGAREG_READ_MISC_OUTPUT        0x3cc

#define VGAREG_GRDC_ADDRESS            0x3ce
#define VGAREG_GRDC_DATA               0x3cf

#define VGAREG_MDA_CRTC_ADDRESS        0x3b4
#define VGAREG_MDA_CRTC_DATA           0x3b5
#define VGAREG_VGA_CRTC_ADDRESS        0x3d4
#define VGAREG_VGA_CRTC_DATA           0x3d5

#define VGAREG_MDA_WRITE_FEATURE_CTL   0x3ba
#define VGAREG_VGA_WRITE_FEATURE_CTL   0x3da
#define VGAREG_ACTL_RESET              0x3da

#define VGAREG_MDA_MODECTL             0x3b8
#define VGAREG_CGA_MODECTL             0x3d8
#define VGAREG_CGA_PALETTE             0x3d9

/* Video memory */
#define SEG_GRAPH 0xA000
#define SEG_CTEXT 0xB800
#define SEG_MTEXT 0xB000

/*
 *
 * Tables of default values for each mode
 *
 */
#define TEXT       0x00
#define GRAPH      0x01

#define CTEXT      0x00
#define MTEXT      0x01
#define CGA        0x02
#define PLANAR1    0x03
#define PLANAR4    0x04
#define LINEAR8    0x05

// for SVGA
#define LINEAR15   0x10
#define LINEAR16   0x11
#define LINEAR24   0x12
#define LINEAR32   0x13

#define SCROLL_DOWN 0
#define SCROLL_UP   1
#define NO_ATTR     2
#define WITH_ATTR   3

#define SCREEN_SIZE(x,y) (((x*y*2)|0x00ff)+1)
#define SCREEN_MEM_START(x,y,p) ((((x*y*2)|0x00ff)+1)*p)
#define SCREEN_IO_START(x,y,p) ((((x*y)|0x00ff)+1)*p)

/* standard BIOS Video Parameter Table */
struct VideoParam_s {
    u8 twidth;
    u8 theightm1;
    u8 cheight;
    u16 slength;
    u8 sequ_regs[4];
    u8 miscreg;
    u8 crtc_regs[25];
    u8 actl_regs[20];
    u8 grdc_regs[9];
} PACKED;

struct vgamode_s {
    u8 svgamode;
    struct VideoParam_s *vparam;
    u8 class;       /* TEXT, GRAPH */
    u8 memmodel;    /* CTEXT,MTEXT,CGA,PL1,PL2,PL4,P8,P15,P16,P24,P32 */
    u8 pixbits;
    u16 sstart;
    u8 pelmask;
    u8 *dac;
    u16 dacsize;
};

// vgatables.c
struct vgamode_s *find_vga_entry(u8 mode);
extern u16 video_save_pointer_table[];
extern struct VideoParam_s video_param_table[];
extern u8 static_functionality[];

// vgafonts.c
extern u8 vgafont8[];
extern u8 vgafont14[];
extern u8 vgafont16[];
extern u8 vgafont14alt[];
extern u8 vgafont16alt[];

// vga.c
void biosfn_set_single_palette_reg(u8 reg, u8 val);
u8 biosfn_get_single_palette_reg(u8 reg);

// clext.c
void cirrus_set_video_mode(u8 mode);
void cirrus_init();

#endif // vgatables.h
