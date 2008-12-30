// Variable layouts of bios.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.
#ifndef __BIOSVAR_H
#define __BIOSVAR_H

#include "types.h" // u8
#include "farptr.h" // GET_FARVAR
#include "config.h" // CONFIG_*


/****************************************************************
 * Bios Data Area (BDA)
 ****************************************************************/

struct ivec {
    u16 offset;
    u16 seg;
};

struct bios_data_area_s {
    // 00:00
    struct ivec ivecs[256];
    // 30:00
//    u8 stack[256];
    // 40:00
    u16 port_com[4];
    u16 port_lpt[3];
    u16 ebda_seg;
    // 40:10
    u16 equipment_list_flags;
    u8 pad1;
    u16 mem_size_kb;
    u8 pad2;
    u8 ps2_ctrl_flag;
    u8 kbd_flag0;
    u8 kbd_flag1;
    u8 alt_keypad;
    u16 kbd_buf_head;
    u16 kbd_buf_tail;
    // 40:1e
    u8 kbd_buf[32];
    u8 floppy_recalibration_status;
    u8 floppy_motor_status;
    // 40:40
    u8 floppy_motor_counter;
    u8 floppy_last_status;
    u8 floppy_return_status[7];
    u8 video_mode;
    u16 video_cols;
    u16 video_pagesize;
    u16 video_pagestart;
    // 40:50
    u16 cursor_pos[8];
    // 40:60
    u16 cursor_type;
    u8 video_page;
    u16 crtc_address;
    u8 video_msr;
    u8 video_pal;
    u16 jump_ip;
    u16 jump_cs;
    u8 other_6b;
    u32 timer_counter;
    // 40:70
    u8 timer_rollover;
    u8 break_flag;
    u16 soft_reset_flag;
    u8 disk_last_status;
    u8 disk_count;
    u8 disk_control_byte;
    u8 port_disk;
    u8 lpt_timeout[4];
    u8 com_timeout[4];
    // 40:80
    u16 kbd_buf_start_offset;
    u16 kbd_buf_end_offset;
    u8 video_rows;
    u16 char_height;
    u8 video_ctl;
    u8 video_switches;
    u8 modeset_ctl;
    u8 dcc_index;
    u8 floppy_last_data_rate;
    u8 disk_status_controller;
    u8 disk_error_controller;
    u8 disk_interrupt_flag;
    u8 floppy_harddisk_info;
    // 40:90
    u8 floppy_media_state[4];
    u8 floppy_track0;
    u8 floppy_track1;
    u8 kbd_mode;
    u8 kbd_led;
    u32 ptr_user_wait_complete_flag;
    u32 user_wait_timeout;
    // 40:A0
    u8 rtc_wait_flag;
} PACKED;

// BDA floppy_recalibration_status bitdefs
#define FRS_TIMEOUT (1<<7)

// BDA rtc_wait_flag bitdefs
#define RWS_WAIT_PENDING (1<<0)
#define RWS_WAIT_ELAPSED (1<<7)

// BDA floppy_media_state bitdefs
#define FMS_DRIVE_STATE_MASK        (0x07)
#define FMS_MEDIA_DRIVE_ESTABLISHED (1<<4)
#define FMS_DOUBLE_STEPPING         (1<<5)
#define FMS_DATA_RATE_MASK          (0xc0)

// Accessor functions
#define GET_BDA(var) \
    GET_FARVAR(SEG_BDA, ((struct bios_data_area_s *)0)->var)
#define SET_BDA(var, val) \
    SET_FARVAR(SEG_BDA, ((struct bios_data_area_s *)0)->var, (val))
#define CLEARBITS_BDA(var, val) do {                                    \
        typeof(((struct bios_data_area_s *)0)->var) __val = GET_BDA(var); \
        SET_BDA(var, (__val & ~(val)));                                 \
    } while (0)
#define SETBITS_BDA(var, val) do {                                      \
        typeof(((struct bios_data_area_s *)0)->var) __val = GET_BDA(var); \
        SET_BDA(var, (__val | (val)));                                  \
    } while (0)


/****************************************************************
 * Extended Bios Data Area (EBDA)
 ****************************************************************/

// DPTE definition
struct dpte_s {
    u16 iobase1;
    u16 iobase2;
    u8  prefix;
    u8  unused;
    u8  irq;
    u8  blkcount;
    u8  dma;
    u8  pio;
    u16 options;
    u16 reserved;
    u8  revision;
    u8  checksum;
};

struct fdpt_s {
    u16 cylinders;
    u8 heads;
    u8 a0h_signature;
    u8 phys_sectors;
    u16 precompensation;
    u8 reserved;
    u8 drive_control_byte;
    u16 phys_cylinders;
    u8 phys_heads;
    u16 landing_zone;
    u8 sectors;
    u8 checksum;
} PACKED;

struct extended_bios_data_area_s {
    u8 size;
    u8 reserved1[0x21];
    u32 far_call_pointer;
    u8 mouse_flag1;
    u8 mouse_flag2;
    u8 mouse_data[0x08];
    // 0x30
    u8 other1[0x0d];

    // 0x3d
    struct fdpt_s fdpt[2];

    // 0x5d
    u8 other2[0xC4];

    // 0x121 - Begin custom storage.
    u8 ps2ctr;

    u8 cdemu_active;

    // Count of transferred sectors and bytes to/from disk
    u16 sector_count;

    // Buffer for disk DPTE table
    struct dpte_s dpte;

    // Locks for removable devices
    u8 cdrom_locks[CONFIG_MAX_ATA_DEVICES];

    u16 boot_sequence;

    // Resume stack
    u8 resume_stack[128] __aligned(8);
} PACKED;

// Accessor functions
static inline u16 get_ebda_seg() {
    return GET_BDA(ebda_seg);
}
static inline struct extended_bios_data_area_s *
get_ebda_ptr()
{
    extern void *__force_link_error__get_ebda_ptr_only_in_32bit();
    if (MODE16)
        return __force_link_error__get_ebda_ptr_only_in_32bit();
    return (void*)MAKE_FARPTR(get_ebda_seg(), 0);
}
#define GET_EBDA2(eseg, var)                                            \
    GET_FARVAR(eseg, ((struct extended_bios_data_area_s *)0)->var)
#define SET_EBDA2(eseg, var, val)                                       \
    SET_FARVAR(eseg, ((struct extended_bios_data_area_s *)0)->var, (val))
#define GET_EBDA(var)                                            \
    GET_EBDA2(get_ebda_seg(), var)
#define SET_EBDA(var, val)                                       \
    SET_EBDA2(get_ebda_seg(), var, (val))


/****************************************************************
 * Global variables
 ****************************************************************/

#define GET_GLOBAL(var) \
    GET_VAR(CS, (var))
#if MODE16
extern void __force_link_error__set_global_only_in_32bit();
#define SET_GLOBAL(var, val) do {                       \
    (void)(val);                                        \
    __force_link_error__set_global_only_in_32bit();     \
    } while (0)
#else
#define SET_GLOBAL(var, val)                    \
    do { (var) = (val); } while (0)
#endif


/****************************************************************
 * Bios Config Table
 ****************************************************************/

struct bios_config_table_s {
    u16 size;
    u8 model;
    u8 submodel;
    u8 biosrev;
    u8 feature1, feature2, feature3, feature4, feature5;
} PACKED;

extern struct bios_config_table_s BIOS_CONFIG_TABLE;

#endif // __BIOSVAR_H
