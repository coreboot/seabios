// Linker definitions for 32 bit code
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "config.h" // BUILD_BIOS_ADDR

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH("i386")
SECTIONS
{
        .text32 BUILD_BIOS_ADDR : {
                code32_start = ABSOLUTE(.) ;

// The actual sections kept is determined by the script tools/layoutrom.py
#include "../out/romlayout32.lds"

                freespace_start = . ;
                code32_end = ABSOLUTE(.) ;
                }
}
