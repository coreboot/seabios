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
    if c16s != c32s:
        print "Error!  16bit code moved during linking (0x%x vs 0x%x)" % (
            c32s, c16s)
        sys.exit(1)

    sizefree = syms['freespace1_end'] - syms['freespace1_start']
    size16 = syms['code16_end'] - syms['code16_start'] - sizefree
    size32 = syms['code32_end'] - syms['code32_start']
    totalc = size16+size32
    tablefree = syms['freespace2_end'] - syms['freespace2_start']
    print "16bit size: %d" % size16
    print "32bit size: %d" % size32
    print "Total size: %d  Free space: %d  Percent used: %.1f%%" % (
        totalc, sizefree
        , (totalc / float(size16+size32+sizefree)) * 100.0)
    print "BIOS table space:  %d" % tablefree

if __name__ == '__main__':
    main()
