// Misc function and variable declarations.
#ifndef __VGAUTIL_H
#define __VGAUTIL_H

#include "types.h" // u8

// stdvgaio.c
u8 stdvga_pelmask_read(void);
void stdvga_pelmask_write(u8 val);
u8 stdvga_misc_read(void);
void stdvga_misc_write(u8 value);
void stdvga_misc_mask(u8 off, u8 on);
u8 stdvga_sequ_read(u8 index);
void stdvga_sequ_write(u8 index, u8 value);
void stdvga_sequ_mask(u8 index, u8 off, u8 on);
u8 stdvga_grdc_read(u8 index);
void stdvga_grdc_write(u8 index, u8 value);
void stdvga_grdc_mask(u8 index, u8 off, u8 on);
u8 stdvga_crtc_read(u16 crtc_addr, u8 index);
void stdvga_crtc_write(u16 crtc_addr, u8 index, u8 value);
void stdvga_crtc_mask(u16 crtc_addr, u8 index, u8 off, u8 on);
u8 stdvga_attr_read(u8 index);
void stdvga_attr_write(u8 index, u8 value);
void stdvga_attr_mask(u8 index, u8 off, u8 on);
u8 stdvga_attrindex_read(void);
void stdvga_attrindex_write(u8 value);
void stdvga_dac_read(u16 seg, u8 *data_far, u8 start, int count);
void stdvga_dac_write(u16 seg, u8 *data_far, u8 start, int count);

// stdvgamodes.c
struct vgamode_s *stdvga_find_mode(int mode);
void stdvga_list_modes(u16 seg, u16 *dest, u16 *last);
void stdvga_build_video_param(void);
void stdvga_override_crtc(int mode, u8 *crtc);
int stdvga_set_mode(struct vgamode_s *vmode_g, int flags);
void stdvga_set_packed_palette(void);

// swcursor.c
struct bregs;
void swcursor_pre_handle10(struct bregs *regs);
void swcursor_check_event(void);

// vbe.c
extern u32 VBE_total_memory;
extern u32 VBE_capabilities;
extern u32 VBE_framebuffer;
extern u16 VBE_win_granularity;
void handle_104f(struct bregs *regs);

// vgafonts.c
extern u8 vgafont8[];
extern u8 vgafont14[];
extern u8 vgafont16[];
extern u8 vgafont14alt[];
extern u8 vgafont16alt[];

// vgainit.c
extern int VgaBDF;
extern int HaveRunInit;

#endif // vgautil.h
