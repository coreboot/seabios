#ifndef __CLEXT_H
#define __CLEXT_H

#include "types.h" // u8

struct vgamode_s *clext_find_mode(int mode);
int clext_set_mode(int mode, int flags);
int clext_init(void);

#endif // clext.h
