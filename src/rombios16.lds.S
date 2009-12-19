// Linker definitions for 16bit code
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH("i386")
SECTIONS
{
        .data16 data16_start : {
                *(.data16)
                }
        .text16 text16_start : {
                *(.text16)
                }
        /DISCARD/ : { *(.discard*) }
}
