// Linker definitions for 16bit code
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "config.h"

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH("i386")
SECTIONS
{
        .text16 ( _code32_code16_start - BUILD_BIOS_ADDR ) : {
                *(.text)
                *(.rodata*)
                *(.data)
                }

        .text16.fixed.addr BUILD_START_FIXED : {
                *(.text.fixed.addr)
                }
}
