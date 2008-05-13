#!/usr/bin/env python
# Script that tries to find how much stack space each function in an
# object is using.
#
# Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

# Usage:
#   objdump -m i386 -M i8086 -M suffix -d out/rom16.o | tools/checkstack.py

import sys
import re

#IGNORE = ['screenc', 'bvprintf']
IGNORE = ['screenc']

# Find out maximum stack usage for a function
def calcmaxstack(funcs, func):
    info = funcs[func]
    if func.split('.')[0] in IGNORE:
        # Function is hardcoded to report 0 stack usage
        info[1] = 0
        return
    # Find max of all nested calls.
    max = info[0]
    for addr, callfname, usage in info[2]:
        callinfo = funcs[callfname]
        if callinfo[1] is None:
            calcmaxstack(funcs, callfname)
        totusage = usage + callinfo[1]
        if totusage > max:
            max = totusage
    info[1] = max

hex_s = r'[0-9a-f]+'
re_func = re.compile(r'^' + hex_s + r' <(?P<func>.*)>:$')
re_asm = re.compile(
    r'^[ ]*(?P<addr>' + hex_s + r'):\t.*\t'
    r'(?P<insn>[a-z0-9]+ [^<]*)( <(?P<ref>.*)>)?$')
re_usestack = re.compile(
    r'^(push.*)|(sub.* [$](?P<num>0x' + hex_s + r'),%esp)$')

def calc():
    # funcs = {funcname: [basicstackusage, maxstackusage
    #                     , [(addr, callfname, stackusage), ...]] }
    funcs = {}
    cur = None
    atstart = 0
    stackusage = 0

    # Parse input lines
    for line in sys.stdin.readlines():
        m = re_func.match(line)
        if m is not None:
            # Found function
            cur = [0, None, []]
            funcs[m.group('func')] = cur
            stackusage = 0
            atstart = 1
            continue
        m = re_asm.match(line)
        if m is not None:
            insn = m.group('insn')

            im = re_usestack.match(insn)
            if im is not None:
                if insn[:4] == 'push':
                    stackusage += 4
                    continue
                stackusage += int(im.group('num'), 16)

            if atstart:
                cur[0] = stackusage
                atstart = 0

            ref = m.group('ref')
            if ref is not None and '+' not in ref:
                if insn[:1] == 'j':
                    # Tail call
                    stackusage = 0
                elif insn[:4] == 'call':
                    stackusage += 4
                else:
                    print "unknown call", ref
                cur[2].append((m.group('addr'), ref, stackusage))
                # Reset stack usage to preamble usage
                stackusage = cur[0]

            continue

        #print "other", repr(line)

    # Calculate maxstackusage
    for func, info in funcs.items():
        if info[1] is not None:
            continue
        calcmaxstack(funcs, func)

    # Show all functions
    funcnames = funcs.keys()
    funcnames.sort()
    for func in funcnames:
        basicusage, maxusage, calls = funcs[func]
        if maxusage == 0:
            continue
        print "\n%s[%d,%d]:" % (func, basicusage, maxusage)
        for addr, callfname, stackusage in calls:
            callinfo = funcs[callfname]
            print "    %04s:%-30s[%d+%d,%d]" % (
                addr, callfname, stackusage, callinfo[0], stackusage+callinfo[1])

def main():
    calc()

if __name__ == '__main__':
    main()
