#!/usr/bin/env python
# Script to merge a rom32.bin file into a rom16.bin file.
#
# Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

import sys
import struct

ROM16='out/rom16.bin'
ROM32='out/rom32.bin'
OFFSETS16='out/rom16.offset.auto.h'
OFFSETS32='out/rom32.offset.auto.h'
OUT='out/rom.bin'

def align(v, a):
    return (v + a - 1) // a * a

def scanconfig(file):
    f = open(file, 'rb')
    opts = {}
    for l in f.readlines():
        parts = l.split()
        if len(parts) != 3:
            continue
        if parts[0] != '#define':
            continue
        opts[parts[1]] = parts[2]
    return opts

def alteraddr(data, offset, ptr):
    rel = struct.pack("<i", ptr)
    return data[:offset] + rel + data[offset+4:]


def main():
    # Read in files
    f = open(ROM16, 'rb')
    data16 = f.read()
    f = open(ROM32, 'rb')
    data32 = f.read()

    if len(data16) != 65536:
        print "16bit code is not 65536 bytes long"
        sys.exit(1)

    # Get config options
    o16 = scanconfig(OFFSETS16)
    o32 = scanconfig(OFFSETS32)

    # Inject 32bit code
    spos = align(int(o16['OFFSET_bios16c_end'], 16), 16)
    epos = int(o16['OFFSET_post16'], 16)
    size32 = len(data32)
    freespace = epos - spos
    if size32 > freespace:
        print "32bit code too large (%d vs %d)" % (size32, freespace)
        sys.exit(1)
    if data16[spos:spos+size32] != '\0'*size32:
        print "Non zero data in 16bit freespace (%d to %d)" % (
            spos, spos+size32)
        sys.exit(1)
    outrom = data16[:spos] + data32 + data16[spos+size32:]

    # Fixup initial jump to 32 bit code
    jmppos = int(o16['OFFSET_set_entry32'], 16)
    start32 = int(o32['OFFSET__start'], 16)
    outrom = alteraddr(outrom, jmppos+2, start32)

    print "Writing output rom %s" % OUT
    print " 16bit C-code size: %d" % spos
    print " 32bit C-code size: %d" % size32
    print " Total C-code size: %d" % (spos+size32)

    # Write output rom
    f = open(OUT, 'wb')
    f.write(outrom)
    f.close()

if __name__ == '__main__':
    main()
