// Definitions for X86 bios disks.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.
#ifndef __DISK_H
#define __DISK_H

#include "ioport.h" // outb
#include "biosvar.h" // struct bregs

#define DISK_RET_SUCCESS     0x00
#define DISK_RET_EPARAM      0x01
#define DISK_RET_ECHANGED    0x06
#define DISK_RET_EBOUNDARY   0x09
#define DISK_RET_ECONTROLLER 0x20
#define DISK_RET_ETIMEOUT    0x80
#define DISK_RET_EMEDIA      0xC0

// floppy.c
void floppy_13(struct bregs *regs, u8 drive);
void floppy_tick();

#endif // disk.h
