#ifndef __VGAHW_H
#define __VGAHW_H

#include "types.h" // u8
#include "config.h" // CONFIG_*

#include "clext.h" // clext_set_mode
#include "bochsvga.h" // bochsvga_set_mode
#include "stdvga.h" // stdvga_set_mode
#include "geodevga.h" // geodevga_init

static inline struct vgamode_s *vgahw_find_mode(int mode) {
    if (CONFIG_VGA_CIRRUS)
        return clext_find_mode(mode);
    if (CONFIG_VGA_BOCHS)
        return bochsvga_find_mode(mode);
    return stdvga_find_mode(mode);
}

static inline int vgahw_set_mode(int mode, int flags) {
    if (CONFIG_VGA_CIRRUS)
        return clext_set_mode(mode, flags);
    if (CONFIG_VGA_BOCHS)
        return bochsvga_set_mode(mode, flags);
    return stdvga_set_mode(mode, flags);
}

static inline void vgahw_list_modes(u16 seg, u16 *dest, u16 *last) {
    if (CONFIG_VGA_CIRRUS)
        clext_list_modes(seg, dest, last);
    else if (CONFIG_VGA_BOCHS)
        bochsvga_list_modes(seg, dest, last);
    else
        stdvga_list_modes(seg, dest, last);
}

static inline int vgahw_init(void) {
    if (CONFIG_VGA_CIRRUS)
        return clext_init();
    if (CONFIG_VGA_BOCHS)
        return bochsvga_init();
    if (CONFIG_VGA_GEODEGX2 || CONFIG_VGA_GEODELX)
        return geodevga_init();
    return stdvga_init();
}

#endif // vgahw.h
