#ifndef __CLEXT_H
#define __CLEXT_H

#include "types.h" // u16

struct vgamode_s *clext_find_mode(int mode);
int clext_get_window(struct vgamode_s *vmode_g, int window);
int clext_set_window(struct vgamode_s *vmode_g, int window, int val);
int clext_get_linelength(struct vgamode_s *vmode_g);
int clext_set_linelength(struct vgamode_s *vmode_g, int val);
int clext_set_mode(struct vgamode_s *vmode_g, int flags);
void clext_list_modes(u16 seg, u16 *dest, u16 *last);
int clext_init(void);
struct bregs;
void clext_1012(struct bregs *regs);

#endif // clext.h
