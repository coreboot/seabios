/*
 * hardware.h
 * Hardware specification.
 * 
 * Copyright (C) 2008  Nguyen Anh Quynh <aquynh@gmail.com>
 * Copyright (C) 2002  MandrakeSoft S.A.
 * 
 * This file may be distributed under the terms of the GNU GPLv3 license.
 */

#ifndef __HARDWARE_H 
#define __HARDWARE_H

#include "types.h" // u32

extern u32 cpuid_signature;
extern u32 cpuid_features;
extern u32 cpuid_ext_features;
extern unsigned long ram_size;
extern unsigned long bios_table_cur_addr;
extern unsigned long bios_table_end_addr;

#ifdef CONFIG_USE_EBDA_TABLES
extern unsigned long ebda_cur_addr;
#endif

extern u32 pm_io_base, smb_io_base;
extern int pm_sci_int;

#endif /* __HARDWARE_H */

