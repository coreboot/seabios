#!/bin/sh
# Script to test if gcc "-fwhole-program" works properly.

mkdir -p out
TMPFILE1=out/tmp_testcompile1.c
TMPFILE1o=out/tmp_testcompile1.o
TMPFILE2=out/tmp_testcompile2.c
TMPFILE2o=out/tmp_testcompile2.o
TMPFILE3o=out/tmp_testcompile3.o

# Test for "-fwhole-program"
$CC -fwhole-program -S -o /dev/null -xc /dev/null > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "  Working around no -fwhole-program" > /dev/fd/2
    echo 2
    exit 0
fi

# Test if "visible" variables and functions are marked global.
cat - > $TMPFILE1 <<EOF
void __attribute__((externally_visible)) t1() { }
unsigned char v1 __attribute__((section(".data16.foo.19"))) __attribute__((externally_visible));
EOF
$CC -Os -c -fwhole-program $TMPFILE1 -o $TMPFILE1o > /dev/null 2>&1
cat - > $TMPFILE2 <<EOF
void t1();
extern unsigned char v1;
int __attribute__((externally_visible)) main() { t1(); return v1; }
EOF
$CC -Os -c -fwhole-program $TMPFILE2 -o $TMPFILE2o > /dev/null 2>&1
$CC -nostdlib -Os $TMPFILE1o $TMPFILE2o -o $TMPFILE3o > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "  Working around non-functional -fwhole-program" > /dev/fd/2
    echo 2
    exit 0
fi

# Test if "-combine" works
mkdir -p out
cat - > $TMPFILE1 <<EOF
struct ts { union { int u1; struct { int u2; }; }; };
void t1(struct ts *r);
EOF
$CC -c -fwhole-program -combine $TMPFILE1 $TMPFILE1 -o $TMPFILE1o > /dev/null 2>&1
if [ $? -eq 0 ]; then
    #echo "  Setting AVOIDCOMBINE=0" > /dev/fd/2
    echo 0
else
    echo "  Enabling AVOIDCOMBINE=1" > /dev/fd/2
    echo 1
fi

rm -f $TMPFILE1 $TMPFILE1o $TMPFILE2 $TMPFILE2o $TMPFILE3o
