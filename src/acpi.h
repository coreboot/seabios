/*
 * acpi.h
 * ACPI support.
 * 
 * Copyright (C) 2008  Nguyen Anh Quynh <aquynh@gmail.com>
 * Copyright (C) 2002  MandrakeSoft S.A.
 * 
 * This file may be distributed under the terms of the GNU GPLv3 license.
 */

#ifndef __ACPI_H
#define __ACPI_H

#define ACPI_DATA_SIZE    0x00010000L

extern int acpi_enabled;

void acpi_bios_init(void);

#endif
