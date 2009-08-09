// Variable layouts of bios.
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.
#ifndef __BIOSVAR_H
#define __BIOSVAR_H

#include "types.h" // u8
#include "farptr.h" // GET_FARVAR
#include "config.h" // CONFIG_*
#include "disk.h" // struct chs_s

struct segoff_s {
    union {
        struct {
            u16 offset;
            u16 seg;
        };
        u32 segoff;
    };
};
#define SEGOFF(s,o) ({struct segoff_s __so; __so.offset=(o); __so.seg=(s); __so;})


/****************************************************************
 * Interupt vector table
 ****************************************************************/

struct rmode_IVT {
    struct segoff_s ivec[256];
};

#define GET_IVT(vector)                                         \
    GET_FARVAR(SEG_IVT, ((struct rmode_IVT *)0)->ivec[vector])
#define SET_IVT(vector, seg, off)                                       \
    SET_FARVAR(SEG_IVT, ((struct rmode_IVT *)0)->ivec[vector].segoff, ((seg) << 16) | (off))


/****************************************************************
 * Bios Data Area (BDA)
 ****************************************************************/

struct bios_data_area_s {
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
    u8 hdcount;
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
    u8 floppy_track[2];
    u8 kbd_mode;
    u8 kbd_led;
    u32 ptr_user_wait_complete_flag;
    u32 user_wait_timeout;
    // 40:A0
    u8 rtc_wait_flag;
    u8 other_a1[7];
    u16 video_savetable_ptr;
    u16 video_savetable_seg;
    u8 other_ac[4];
    // 40:B0
    u8 other_b0[10];
    u16 vbe_mode;
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

// ElTorito Device Emulation data
struct cdemu_s {
    u8  active;
    u8  media;
    u8  emulated_drive;
    u8  controller_index;
    u16 device_spec;
    u32 ilba;
    u16 buffer_segment;
    u16 load_segment;
    u16 sector_count;

    // Virtual device
    struct chs_s lchs;
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

    // El Torito Emulation data
    struct cdemu_s cdemu;

    // Count of transferred sectors and bytes to/from disk
    u16 sector_count;

    // Buffer for disk DPTE table
    struct dpte_s dpte;

    // Locks for removable devices
    u8 cdrom_locks[CONFIG_MAX_ATA_DEVICES];

    u16 boot_sequence;

    // Stack space available for code that needs it.
    u8 extra_stack[512] __aligned(8);

    u8 cdemu_buf[2048 * !!CONFIG_CDROM_EMU];
} PACKED;

// The initial size and location of EBDA
#define EBDA_SIZE_START \
    DIV_ROUND_UP(sizeof(struct extended_bios_data_area_s), 1024)
#define EBDA_SEGMENT_START \
    FLATPTR_TO_SEG((640 - EBDA_SIZE_START) * 1024)
#define EBDA_SEGMENT_MINIMUM \
    FLATPTR_TO_SEG((640 - 256) * 1024)

// Accessor functions
static inline u16 get_ebda_seg() {
    return GET_BDA(ebda_seg);
}
static inline struct extended_bios_data_area_s *
get_ebda_ptr()
{
    ASSERT32();
    return MAKE_FLATPTR(get_ebda_seg(), 0);
}
#define GET_EBDA2(eseg, var)                                            \
    GET_FARVAR(eseg, ((struct extended_bios_data_area_s *)0)->var)
#define SET_EBDA2(eseg, var, val)                                       \
    SET_FARVAR(eseg, ((struct extended_bios_data_area_s *)0)->var, (val))
#define GET_EBDA(var)                           \
    GET_EBDA2(get_ebda_seg(), var)
#define SET_EBDA(var, val)                      \
    SET_EBDA2(get_ebda_seg(), var, (val))

#define EBDA_OFFSET_TOP_STACK                                   \
    offsetof(struct extended_bios_data_area_s, extra_stack[     \
                 FIELD_SIZEOF(struct extended_bios_data_area_s  \
                              , extra_stack)])


/****************************************************************
 * Global variables
 ****************************************************************/

#define GLOBAL_SEGREG CS
static inline u16 get_global_seg() {
    return GET_SEG(GLOBAL_SEGREG);
}
#define GET_GLOBAL(var)                         \
    GET_VAR(GLOBAL_SEGREG, (var))
#define SET_GLOBAL(var, val) do {                                       \
        ASSERT32();                                                     \
        (var) = (val);                                                  \
    } while (0)


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

extern struct bios_config_table_s BIOS_CONFIG_TABLE __aligned(1);

#endif // __BIOSVAR_H
