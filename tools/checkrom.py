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
    start = syms['code32flat_start']
    end = syms['code32flat_end']
    expend = layoutrom.BUILD_BIOS_ADDR + layoutrom.BUILD_BIOS_SIZE
    if end != expend:
        print "Error!  Code does not end at 0x%x (got 0x%x)" % (
            expend, end)
        sys.exit(1)
    if datasize > finalsize:
        print "Error!  Code is too big (0x%x vs 0x%x)" % (
            datasize, finalsize)
        sys.exit(1)
    expdatasize = end - start
    if datasize != expdatasize:
        print "Error!  Unknown extra data (0x%x vs 0x%x)" % (
            datasize, expdatasize)
        sys.exit(1)

    # Print statistics
    print "Total size: %d  Free space: %d  Percent used: %.1f%% (%dKiB rom)" % (
        datasize, finalsize - datasize
        , (datasize / float(finalsize)) * 100.0
        , finalsize / 1024)

    # Write final file
    f = open(outfile, 'wb')
    f.write(("\0" * (finalsize - datasize)) + rawdata)
    f.close()

if __name__ == '__main__':
    main()
