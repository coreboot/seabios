// Linker definitions for 32bit segmented code
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH("i386")
SECTIONS
{
        .text32seg code32seg_start : {
                *(.text32seg)
        }
        /DISCARD/ : { *(.discard*) }
}
