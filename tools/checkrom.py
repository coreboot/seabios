#!/usr/bin/env python
# Script to check a bios image and report info on it.
#
# Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

import sys
import layoutrom

def main():
    # Get args
    objinfo, rawfile, outfile = sys.argv[1:]

    # Read in symbols
    objinfofile = open(objinfo, 'rb')
    symbols = layoutrom.parseObjDump(objinfofile)[1]
    syms = {}
    for name, (addr, section) in symbols.items():
        syms[name] = addr

    # Read in raw file
    f = open(rawfile, 'rb')
    rawdata = f.read()
    f.close()
    datasize = len(rawdata)
    finalsize = 64*1024
    if datasize > 64*1024:
        finalsize = 128*1024

    # Sanity checks
    c16e = syms['code16_end'] + 0xf0000
    f16e = syms['final_code16_end']
    if c16e != f16e:
        print "Error!  16bit code moved during linking (0x%x vs 0x%x)" % (
            c16e, f16e)
        sys.exit(1)
    if datasize > finalsize:
        print "Error!  Code is too big (0x%x vs 0x%x)" % (
            datasize, finalsize)
        sys.exit(1)

    # Print statistics
    sizefree = syms['freespace_end'] - syms['freespace_start']
    size16 = syms['code16_end'] - syms['code16_start']
    size32 = syms['code32_end'] - syms['code32_start']
    totalc = size16+size32
    print "16bit size: %d" % size16
    print "32bit size: %d" % size32
    print "Total size: %d  Free space: %d  Percent used: %.1f%% (%dKiB rom)" % (
        totalc, sizefree + finalsize - datasize
        , (totalc / float(finalsize)) * 100.0
        , finalsize / 1024)

    # Write final file
    f = open(outfile, 'wb')
    f.write(("\0" * (finalsize - datasize)) + rawdata)
    f.close()

if __name__ == '__main__':
    main()
