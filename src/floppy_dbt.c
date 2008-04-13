// Floppy controller parameter table.
//
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "disk.h" // diskette_param_table

// Since no provisions are made for multiple drive types, most
// values in this table are ignored.  I set parameters for 1.44M
// floppy here
struct floppy_dbt_s diskette_param_table __attribute__((aligned (1))) VISIBLE16 = {
    .specify1       = 0xAF,
    .specify2       = 0x02, // head load time 0000001, DMA used
    .shutoff_ticks  = 0x25,
    .bps_code       = 0x02,
    .sectors        = 18,
    .interblock_len = 0x1B,
    .data_len       = 0xFF,
    .gap_len        = 0x6C,
    .fill_byte      = 0xF6,
    .settle_time    = 0x0F,
    .startup_time   = 0x08,
};
