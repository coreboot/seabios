// Definitions for X86 CMOS non-volatile memory access.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.
#ifndef __CMOS_H
#define __CMOS_H

#include "ioport.h" // inb, outb

#define CMOS_RTC_SECONDS         0x00
#define CMOS_RTC_SECONDS_ALARM   0x01
#define CMOS_RTC_MINUTES         0x02
#define CMOS_RTC_MINUTES_ALARM   0x03
#define CMOS_RTC_HOURS           0x04
#define CMOS_RTC_HOURS_ALARM     0x05
#define CMOS_RTC_DAY_WEEK        0x06
#define CMOS_RTC_DAY_MONTH       0x07
#define CMOS_RTC_MONTH           0x08
#define CMOS_RTC_YEAR            0x09
#define CMOS_STATUS_A            0x0a
#define CMOS_STATUS_B            0x0b
#define CMOS_STATUS_C            0x0c
#define CMOS_STATUS_D            0x0d
#define CMOS_RESET_CODE          0x0f
#define CMOS_FLOPPY_DRIVE_TYPE   0x10
#define CMOS_EQUIPMENT_INFO      0x14
#define CMOS_EXTMEM_LOW          0x30
#define CMOS_EXTMEM_HIGH         0x31
#define CMOS_CENTURY             0x32
#define CMOS_EXTMEM2_LOW         0x34
#define CMOS_EXTMEM2_HIGH        0x35

// CMOS_STATUS_B bitdefs
#define CSB_EN_ALARM_IRQ (1<<5)

// CMOS_FLOPPY_DRIVE_TYPE bitdefs
#define CFD_NO_DRIVE 0
#define CFD_360KB    1
#define CFD_12MB     2
#define CFD_720KB    3
#define CFD_144MB    4
#define CFD_288MB    5

static inline u8
inb_cmos(u8 reg)
{
    outb(reg, PORT_CMOS_INDEX);
    return inb(PORT_CMOS_DATA);
}

static inline void
outb_cmos(u8 val, u8 reg)
{
    outb(reg, PORT_CMOS_INDEX);
    outb(val, PORT_CMOS_DATA);
}

#endif // cmos.h
