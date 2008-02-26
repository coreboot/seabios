# Legacy Bios build system
#
# Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

# Output directory
OUT=out/

# Source files
SRC16=floppy.c disk.c system.c clock.c serial.c kbd.c output.c boot.c
SRC32=post.c output.c

# Default compiler flags (note -march=armv4 is needed for 16 bit insns)
CFLAGS = -Wall -Os -MD -m32 -march=i386 -mregparm=2 -ffreestanding
CFLAGS16 = -Wall -Os -MD -m32 -DMODE16 -march=i386 -mregparm=2 -ffreestanding -fno-jump-tables

all: $(OUT) $(OUT)rom.bin

# Run with "make V=1" to see the actual compile commands
ifdef V
Q=
else
Q=@
endif

.PHONY : all FORCE

vpath %.c src
vpath %.S src

################ Build rules
$(OUT)%.proc.16.s: $(OUT)%.16.s
	@echo "  Moving data sections to text in $<"
	$(Q)sed 's/\t.section\t.rodata.*// ; s/\t.data//' < $< > $@

$(OUT)%.16.s: %.c
	@echo "  Generating assembler for $<"
	$(Q)$(CC) $(CFLAGS16) -fwhole-program -S -combine -c $< -o $@

$(OUT)%.lds: %.lds.S
	@echo "  Precompiling $<"
	$(Q)$(CPP) -P $< -o $@

$(OUT)%.bin: $(OUT)%.o
	@echo "  Extracting binary $@"
	$(Q)objcopy -O binary $< $@

$(OUT)%.offset.auto.h: $(OUT)%.o
	@echo "  Generating symbol offset header $@"
	$(Q)nm $< | ./tools/defsyms.py > $@

$(OUT)blob.16.s:
	@echo "  Generating whole program assembler $@"
	$(Q)$(CC) $(CFLAGS16) -fwhole-program -S -combine -c $(addprefix src/, $(SRC16)) -o $@

$(OUT)romlayout16.o: romlayout.S $(OUT)blob.proc.16.s $(OUT)font.proc.16.s $(OUT)cbt.proc.16.s
	@echo "  Generating 16bit layout of $@"
	$(Q)$(CC) $(CFLAGS16) -c $< -o $@

$(OUT)rom16.o: $(OUT)romlayout16.o
	@echo "  Linking $@"
	$(Q)ld -melf_i386 -Ttext 0 $< -o $@

$(OUT)rom16.bin: $(OUT)rom16.o
	@echo "  Extracting binary $@"
	$(Q)objcopy -O binary $< $@

$(OUT)romlayout32.o: $(OUT)rom16.offset.auto.h
	@echo "  Compiling whole program $@"
	$(Q)$(CC) $(CFLAGS) -fwhole-program -combine -c $(addprefix src/, $(SRC32)) -o $@

$(OUT)rom32.o: $(OUT)romlayout32.o $(OUT)rombios32.lds
	@echo "  Linking $@"
	$(Q)ld -T $(OUT)rombios32.lds $< -o $@

$(OUT)rom.bin: $(OUT)rom16.bin $(OUT)rom32.bin $(OUT)rom16.offset.auto.h $(OUT)rom32.offset.auto.h
	@echo "  Building $@"
	$(Q)./tools/buildrom.py

####### Generic rules
clean:
	rm -rf $(OUT)

$(OUT):
	mkdir $@

-include $(OUT)*.d
