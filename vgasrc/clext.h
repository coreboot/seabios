#ifndef __CLEXT_H
#define __CLEXT_H

#include "types.h" // u8

struct vgamode_s *clext_find_mode(int mode);
int clext_set_mode(int mode, int flags);
void clext_list_modes(u16 seg, u16 *dest, u16 *last);
int clext_init(void);

#endif // clext.h
