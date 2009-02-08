# Legacy Bios build system
#
# Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU LGPLv3 license.

# Output directory
OUT=out/

# Source files
SRCBOTH=output.c util.c floppy.c ata.c misc.c mouse.c kbd.c pci.c \
        serial.c clock.c pic.c cdrom.c ps2port.c smpdetect.c resume.c \
        pnpbios.c pirtable.c
SRC16=$(SRCBOTH) system.c disk.c apm.c pcibios.c vgahooks.c font.c
SRC32=$(SRCBOTH) post.c shadow.c memmap.c coreboot.c boot.c \
      acpi.c smm.c mptable.c smbios.c pciinit.c optionroms.c mtrr.c

cc-option = $(shell if test -z "`$(1) $(2) -S -o /dev/null -xc \
              /dev/null 2>&1`"; then echo "$(2)"; else echo "$(3)"; fi ;)

# Default compiler flags
COMMONCFLAGS = -Wall -Os -MD -m32 -march=i386 -mregparm=3 \
               -mpreferred-stack-boundary=2 -mrtd \
               -ffreestanding -fwhole-program -fomit-frame-pointer \
               -fno-delete-null-pointer-checks -Wno-strict-aliasing
COMMONCFLAGS += $(call cc-option,$(CC),-nopie,)
COMMONCFLAGS += $(call cc-option,$(CC),-fno-stack-protector,)
COMMONCFLAGS += $(call cc-option,$(CC),-fno-stack-protector-all,)

override CFLAGS = $(COMMONCFLAGS) -g -DMODE16=0
CFLAGS16INC = $(COMMONCFLAGS) -DMODE16=1 -fno-jump-tables -fno-defer-pop \
              $(call cc-option,$(CC),--param large-stack-frame=4,)
CFLAGS16INC += -ffunction-sections -fdata-sections
CFLAGS16 = $(CFLAGS16INC) -g

all: $(OUT) $(OUT)bios.bin

# Run with "make V=1" to see the actual compile commands
ifdef V
Q=
else
Q=@
endif

OBJCOPY=objcopy
OBJDUMP=objdump
NM=nm
STRIP=strip

.PHONY : all FORCE

vpath %.c src
vpath %.S src

################ Build rules

ifndef AVOIDCOMBINE
AVOIDCOMBINE=$(shell CC=$(CC) tools/test-combine.sh)
endif

# Do a whole file compile - two methods are supported.  The first
# involves including all the content textually via #include
# directives.  The second method uses gcc's "-combine" option.
ifeq "$(AVOIDCOMBINE)" "1"
define whole-compile
@echo "  Compiling whole program $3"
$(Q)printf '$(foreach i,$2,#include "../$i"\n)' > $3.tmp.c
$(Q)$(CC) $1 -c $3.tmp.c -o $3
endef
else
define whole-compile
@echo "  Compiling whole program $3"
$(Q)$(CC) $1 -combine -c $2 -o $3
endef
endif


$(OUT)%.s: %.c
	@echo "  Compiling to assembler $@"
	$(Q)$(CC) $(CFLAGS16INC) -S -c $< -o $@

$(OUT)%.lds: %.lds.S
	@echo "  Precompiling $@"
	$(Q)$(CPP) -P -D__ASSEMBLY__ $< -o $@

$(OUT)asm-offsets.h: $(OUT)asm-offsets.s
	@echo "  Generating offset file $@"
	$(Q)./tools/gen-offsets.sh $< $@

$(OUT)ccode.16.s: ; $(call whole-compile, $(CFLAGS16) -S, $(addprefix src/, $(SRC16)),$@)

$(OUT)romlayout16.o: romlayout.S $(OUT)ccode.16.s $(OUT)asm-offsets.h
	@echo "  Compiling (16bit) $@"
	$(Q)$(CC) $(CFLAGS16INC) -c -D__ASSEMBLY__ $< -o $@

$(OUT)ccode32.o: ; $(call whole-compile, $(CFLAGS), $(addprefix src/, $(SRC32)),$@)

$(OUT)rom32.o: $(OUT)ccode32.o $(OUT)rombios32.lds
	@echo "  Linking (no relocs) $@"
	$(Q)$(LD) -r -d -T $(OUT)rombios32.lds $< -o $@

$(OUT)romlayout.lds: $(OUT)romlayout16.o
	@echo "  Building layout information $@"
	$(Q)$(OBJDUMP) -h $< | ./tools/layoutrom.py $@

$(OUT)rombios16.lds: $(OUT)romlayout.lds

$(OUT)rom16.o: $(OUT)romlayout16.o $(OUT)rom32.o $(OUT)rombios16.lds
	@echo "  Linking (16bit) $@"
	$(Q)$(OBJCOPY) --prefix-symbols=_code32_ $(OUT)rom32.o $(OUT)rom32.rename.o
	$(Q)$(LD) -T $(OUT)rombios16.lds -R $(OUT)rom32.rename.o $< -o $@

$(OUT)rom.o: $(OUT)rom16.o $(OUT)rom32.o $(OUT)rombios.lds
	@echo "  Linking $@"
	$(Q)$(LD) -T $(OUT)rombios.lds $(OUT)rom16.o $(OUT)rom32.o -o $@

$(OUT)bios.bin.elf: $(OUT)rom.o
	@echo "  Prepping $@"
	$(Q)$(NM) $< | ./tools/checkrom.py
	$(Q)$(STRIP) $< -o $@

$(OUT)bios.bin: $(OUT)bios.bin.elf
	@echo "  Extracting binary $@"
	$(Q)$(OBJCOPY) -O binary $< $@


####### Generic rules
clean:
	$(Q)rm -rf $(OUT)

$(OUT):
	$(Q)mkdir $@

-include $(OUT)*.d
