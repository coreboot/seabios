// BIOS configuration table.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "biosvar.h" // CONFIG_BIOS_TABLE

// DMA channel 3 used by hard disk BIOS
#define CBT_F1_DMA3USED (1<<7)
// 2nd interrupt controller (8259) installed
#define CBT_F1_2NDPIC   (1<<6)
// Real-Time Clock installed
#define CBT_F1_RTC      (1<<5)
// INT 15/AH=4Fh called upon INT 09h
#define CBT_F1_INT154F  (1<<4)
// wait for external event (INT 15/AH=41h) supported
#define CBT_F1_WAITEXT  (1<<3)
// extended BIOS area allocated (usually at top of RAM)
#define CBT_F1_EBDA     (1<<2)
// bus is Micro Channel instead of ISA
#define CBT_F1_MCA      (1<<1)
// system has dual bus (Micro Channel + ISA)
#define CBT_F1_MCAISA   (1<<0)

// INT 16/AH=09h (keyboard functionality) supported
#define CBT_F2_INT1609  (1<<6)

struct bios_config_table_s BIOS_CONFIG_TABLE = {
    .size     = sizeof(BIOS_CONFIG_TABLE) - 2,
    .model    = CONFIG_MODEL_ID,
    .submodel = CONFIG_SUBMODEL_ID,
    .biosrev  = CONFIG_BIOS_REVISION,
    .feature1 = (
        CBT_F1_2NDPIC | CBT_F1_RTC | CBT_F1_EBDA
        | (CONFIG_KBD_CALL_INT15_4F ? CBT_F1_INT154F : 0)),
    .feature2 = CBT_F2_INT1609,
    .feature3 = 0,
    .feature4 = 0,
    .feature5 = 0,
};
