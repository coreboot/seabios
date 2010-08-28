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
import optparse

# Reset time counter after this much idle time.
RESTARTINTERVAL = 60
# Alter timing reports based on how much time would be spent writing
# to serial.
ADJUSTBAUD = 1
# Number of bits in a transmitted byte - 8N1 is 1 start bit + 8 data
# bits + 1 stop bit.
BITSPERBYTE = 10

def readserial(infile, logfile, baudrate):
    lasttime = 0
    byteadjust = 0.0
    if ADJUSTBAUD:
        byteadjust = float(BITSPERBYTE) / baudrate
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
            lasttime = 0
            if len(res[0]) == 1:
                continue
        d = infile.read(4096)
        if not d:
            break
        datatime = time.time()

        datatime -= len(d) * byteadjust

        # Reset start time if no data for some time
        if datatime - lasttime > RESTARTINTERVAL:
            starttime = datatime
            charcount = 0
            isnewline = 1
            msg = "\n\n======= %s (adjust=%d)\n" % (
                time.asctime(time.localtime(datatime)), ADJUSTBAUD)
            sys.stdout.write(msg)
            logfile.write(msg)
        lasttime = datatime

        # Translate unprintable chars; add timestamps
        out = ""
        for c in d:
            if isnewline:
                delta = datatime - starttime - (charcount * byteadjust)
                out += "%06.3f: " % delta
                isnewline = 0
            oc = ord(c)
            charcount += 1
            datatime += byteadjust
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

def main():
    usage = "%prog [options] [<serialdevice> [<baud>]]"
    opts = optparse.OptionParser(usage)
    opts.add_option("-f", "--file",
                    action="store_false", dest="serial", default=True,
                    help="read from file instead of serialdevice")
    opts.add_option("-n", "--no-adjust",
                    action="store_false", dest="adjustbaud", default=True,
                    help="don't adjust times by serial rate")
    options, args = opts.parse_args()
    serialport = 0
    baud = 115200
    if len(args) > 2:
        opts.error("Too many arguments")
    if len(args) > 0:
        serialport = args[0]
    if len(args) > 1:
        baud = int(args[1])
    global ADJUSTBAUD
    ADJUSTBAUD=options.adjustbaud

    if options.serial:
        # Read from serial port
        import serial
        ser = serial.Serial(serialport, baud, timeout=0)
    else:
        # Read from a file
        ser = open(serialport, 'rb')
        import fcntl
        import os
        fcntl.fcntl(ser, fcntl.F_SETFL
                    , fcntl.fcntl(ser, fcntl.F_GETFL) | os.O_NONBLOCK)

    logname = time.strftime("seriallog-%Y%m%d_%H%M%S.log")
    f = open(logname, 'wb')
    readserial(ser, f, baud)

if __name__ == '__main__':
    main()
