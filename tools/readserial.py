#!/usr/bin/env python
# Script that can read from a serial device and show timestamps.
#
# Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

# Usage:
#   tools/readserial.py /dev/ttyUSB0 115200

import sys
import time
import select
import serial

# Reset time counter after this much idle time.
RESTARTINTERVAL = 60
# Alter timing reports based on how much time would be spent writing
# to serial.
ADJUSTBAUD = 1

def readserial(infile, logfile, baudrate):
    starttime = 0
    isnewline = 1
    while 1:
        # Read data
        try:
            res = select.select([infile, sys.stdin], [], [])
        except KeyboardInterrupt:
            sys.stdout.write("\n")
            break
        if sys.stdin in res[0]:
            # Got keyboard input - force reset on next serial input
            sys.stdin.read(1)
            starttime = 0
            if len(res[0]) == 1:
                continue
        curtime = time.time()
        d = infile.read(4096)

        # Reset start time if no data for some time
        if curtime - starttime > RESTARTINTERVAL:
            starttime = curtime
            charcount = 0
            isnewline = 1
            sys.stdout.write("\n")
            logfile.write("\n")

        # Translate unprintable chars; add timestamps
        out = ""
        for c in d:
            if isnewline:
                delta = curtime - starttime
                if ADJUSTBAUD:
                    delta -= float(charcount * 9) / baudrate
                out += "%06.3f: " % delta
                isnewline = 0
            oc = ord(c)
            charcount += 1
            if oc == 0x0d:
                continue
            if oc == 0x00:
                out += "<00>\n"
                isnewline = 1
                continue
            if oc == 0x0a:
                out += "\n"
                isnewline = 1
                continue
            if oc < 0x20 or oc >= 0x7f and oc != 0x09:
                out += "<%02x>" % oc
                continue
            out += c

        sys.stdout.write(out)
        sys.stdout.flush()
        logfile.write(out)
        logfile.flush()

def printUsage():
    print "Usage:\n   %s [<serialdevice> [<baud>]]" % (sys.argv[0],)
    sys.exit(1)

def main():
    serialport = 0
    baud = 115200
    if len(sys.argv) > 3:
        printUsage()
    if len(sys.argv) > 1:
        serialport = sys.argv[1]
    if len(sys.argv) > 2:
        baud = int(sys.argv[2])

    ser = serial.Serial(serialport, baud, timeout=0)

    logname = time.strftime("seriallog-%Y%m%d_%H%M%S.log")
    f = open(logname, 'wb')
    readserial(ser, f, baud)

if __name__ == '__main__':
    main()
