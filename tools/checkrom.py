#!/usr/bin/env python
# Script to check a bios image and report info on it.
#
# Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

import sys

def main():
    # Read in symbols (that are valid)
    syms = {}
    for line in sys.stdin.readlines():
        try:
            addr, type, sym = line.split()
            syms[sym] = int(addr, 16)
        except:
            pass

    c16s = syms['code16_start'] + 0xf0000
    c32s = syms['final_code16_start']
    f16s = syms['code16_fixed_start'] + 0xf0000
    f32s = syms['final_code16_fixed_start']
    if c16s != c32s or f16s != f32s:
        print ("Error!  16bit code moved during linking"
               " (0x%x vs 0x%x, 0x%x vs 0x%x)" % (
                c32s, c16s, f16s, f32s))
        sys.exit(1)

    size16 = syms['code16_end'] - syms['code16_start']
    size32 = syms['code32_end'] - syms['code32_start']
    sizefree = syms['freespace1_end'] - syms['freespace1_start']
    tablefree = syms['freespace2_end'] - syms['freespace2_start']
    print "16bit C-code size: %d" % size16
    print "32bit C-code size: %d" % size32
    print "Total C-code size: %d" % (size16+size32)
    print "Free C-code space: %d" % sizefree
    print "BIOS table space:  %d" % tablefree

if __name__ == '__main__':
    main()
