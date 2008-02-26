// Variable layouts of bios.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "types.h" // u8
#include "farptr.h" // SET_SEG


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
    u16 port_com1, port_com2, port_com3, port_com4;
    u16 port_lpt1, port_lpt2, port_lpt3;
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
    u8 other1[0x7];
    // 40:50
    u8 other2[0x1c];
    // 40:6c
    u32 timer_counter;
    // 40:70
    u8 timer_rollover;
    u8 other4[0x0f];
    // 40:80
    u16 kbd_buf_start_offset;
    u16 kbd_buf_end_offset;
    u8 other5[7];
    u8 floppy_last_data_rate;
    u8 other6[3];
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
} __attribute__((packed));

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
    GET_FARVAR(0x0000, ((struct bios_data_area_s *)0)->var)
#define SET_BDA(var, val) \
    SET_FARVAR(0x0000, ((struct bios_data_area_s *)0)->var, (val))
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

struct extended_bios_data_area_s {
    u8 size;
    u8 other1[0x3c];

    // FDPT - Can be splitted in data members if needed
    u8 fdpt0[0x10];
    u8 fdpt1[0x10];

    u8 other2[0xC4];

    // ATA Driver data
    //ata_t   ata;

#if BX_ELTORITO_BOOT
    // El Torito Emulation data
    cdemu_t cdemu;
#endif // BX_ELTORITO_BOOT
};


/****************************************************************
 * Extended Bios Data Area (EBDA)
 ****************************************************************/

#define UREG(ER, R, RH, RL) union { u32 ER; struct { u16 R; u16 R ## _hi; }; struct { u8 RL; u8 RH; u8 R ## _hilo; u8 R ## _hihi; }; }

struct bregs {
    u16 ds;
    u16 es;
    UREG(edi, di, di_hi, di_lo);
    UREG(esi, si, si_hi, si_lo);
    UREG(ebp, bp, bp_hi, bp_lo);
    UREG(esp, sp, sp_hi, sp_lo);
    UREG(ebx, bx, bh, bl);
    UREG(edx, dx, dh, dl);
    UREG(ecx, cx, ch, cl);
    UREG(eax, ax, ah, al);
    u16 ip;
    u16 cs;
    u16 flags;
} __attribute__((packed));

// bregs flags bitdefs
#define F_ZF (1<<6)
#define F_CF (1<<0)

static inline void
set_cf(struct bregs *regs, int cond)
{
    if (cond)
        regs->flags |= F_CF;
    else
        regs->flags &= ~F_CF;
}


/****************************************************************
 * Bios Config Table
 ****************************************************************/

struct bios_config_table_s {
    // XXX
    u8 x;
};

extern struct bios_config_table_s BIOS_CONFIG_TABLE;


/****************************************************************
 * Memory layout info
 ****************************************************************/

#define SEG_BIOS     0xf000

#define EBDA_SEG           0x9FC0
#define EBDA_SIZE          1              // In KiB
#define BASE_MEM_IN_K   (640 - EBDA_SIZE)
