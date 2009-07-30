# SeaBIOS build system
#
# Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU LGPLv3 license.

# Program version
VERSION=pre-0.4.2-$(shell date +"%Y%m%d_%H%M%S")-$(shell hostname)

# Output directory
OUT=out/

# Source files
SRCBOTH=output.c util.c floppy.c ata.c misc.c mouse.c kbd.c pci.c \
        serial.c clock.c pic.c cdrom.c ps2port.c smp.c resume.c \
        pnpbios.c pirtable.c vgahooks.c pmm.c
SRC16=$(SRCBOTH) system.c disk.c apm.c pcibios.c font.c
SRC32=$(SRCBOTH) post.c shadow.c memmap.c coreboot.c boot.c \
      acpi.c smm.c mptable.c smbios.c pciinit.c optionroms.c mtrr.c \
      lzmadecode.c

cc-option = $(shell if test -z "`$(1) $(2) -S -o /dev/null -xc \
              /dev/null 2>&1`"; then echo "$(2)"; else echo "$(3)"; fi ;)

# Default compiler flags
COMMONCFLAGS = -Wall -Os -MD -m32 -march=i386 -mregparm=3 \
               -mpreferred-stack-boundary=2 -mrtd -freg-struct-return \
               -ffreestanding -fomit-frame-pointer \
               -fno-delete-null-pointer-checks -Wno-strict-aliasing \
               -ffunction-sections -fdata-sections -fno-common \
               -minline-all-stringops
COMMONCFLAGS += $(call cc-option,$(CC),-nopie,)
COMMONCFLAGS += $(call cc-option,$(CC),-fno-stack-protector,)
COMMONCFLAGS += $(call cc-option,$(CC),-fno-stack-protector-all,)

override CFLAGS = $(COMMONCFLAGS) -g -DMODE16=0
CFLAGS16INC = $(COMMONCFLAGS) -DMODE16=1 -fno-defer-pop \
              $(call cc-option,$(CC),-fno-jump-tables,-DMANUAL_NO_JUMP_TABLE) \
              $(call cc-option,$(CC),-fno-tree-switch-conversion,) \
              $(call cc-option,$(CC),--param large-stack-frame=4,)
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

vpath %.c src vgasrc
vpath %.S src vgasrc

################ Build rules

# Verify the gcc configuration and test if -fwhole-program works.
TESTGCC:=$(shell CC=$(CC) tools/test-gcc.sh)
ifeq "$(TESTGCC)" "-1"
$(error "Please upgrade GCC")
endif

ifndef COMPSTRAT
COMPSTRAT=$(TESTGCC)
endif

# Do a whole file compile - three methods are supported.
ifeq "$(COMPSTRAT)" "1"
# First method - use -fwhole-program without -combine.
define whole-compile
@echo "  Compiling whole program $3"
$(Q)printf '$(foreach i,$2,#include "../$i"\n)' > $3.tmp.c
$(Q)$(CC) $1 -fwhole-program -DWHOLE_PROGRAM -c $3.tmp.c -o $3
endef
else
ifeq "$(COMPSTRAT)" "2"
# Second menthod - don't use -fwhole-program at all.
define whole-compile
@echo "  Compiling whole program $3"
$(Q)printf '$(foreach i,$2,#include "../$i"\n)' > $3.tmp.c
$(Q)$(CC) $1 -c $3.tmp.c -o $3
endef
else
# Third (and preferred) method - use -fwhole-program with -combine
define whole-compile
@echo "  Compiling whole program $3"
$(Q)$(CC) $1 -fwhole-program -DWHOLE_PROGRAM -combine -c $2 -o $3
endef
endif
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

$(OUT)code16.o: romlayout.S $(OUT)ccode.16.s $(OUT)asm-offsets.h
	@echo "  Compiling (16bit) $@"
	$(Q)$(CC) $(CFLAGS16INC) -c -D__ASSEMBLY__ $< -o $@

$(OUT)code32.o: ; $(call whole-compile, $(CFLAGS), $(addprefix src/, $(SRC32)),$@)

$(OUT)romlayout16.lds $(OUT)romlayout32.lds: $(OUT)code32.o $(OUT)code16.o
	@echo "  Building layout information $@"
	$(Q)$(OBJDUMP) -thr $(OUT)code32.o > $(OUT)code32.o.objdump
	$(Q)$(OBJDUMP) -thr $(OUT)code16.o > $(OUT)code16.o.objdump
	$(Q)./tools/layoutrom.py $(OUT)code16.o.objdump $(OUT)code32.o.objdump $(OUT)romlayout16.lds $(OUT)romlayout32.lds

$(OUT)layout16.lds: $(OUT)romlayout16.lds
$(OUT)rombios32.lds: $(OUT)romlayout32.lds


$(OUT)rom16.o: $(OUT)code16.o $(OUT)rom32.o $(OUT)layout16.lds
	@echo "  Linking (no relocs) $@"
	$(Q)$(LD) -r -T $(OUT)layout16.lds $< -o $@

$(OUT)rom32.o: $(OUT)code32.o $(OUT)rombios32.lds
	@echo "  Linking (no relocs) $@"
	$(Q)$(LD) -r -T $(OUT)rombios32.lds $< -o $@

$(OUT)rom.o: $(OUT)rom16.o $(OUT)rom32.o $(OUT)rombios16.lds $(OUT)rombios.lds
	@echo "  Linking $@ (version \"$(VERSION)\")"
	$(Q)echo 'const char VERSION[] __attribute__((section(".data32.version"))) = "$(VERSION)";' > $(OUT)version.c
	$(Q)$(CC) $(CFLAGS) -c $(OUT)version.c -o $(OUT)version.o
	$(Q)$(LD) -T $(OUT)rombios16.lds $(OUT)rom16.o -R $(OUT)rom32.o -o $(OUT)rom16.reloc.o
	$(Q)$(STRIP) $(OUT)rom16.reloc.o -o $(OUT)rom16.final.o
	$(Q)$(OBJCOPY) --adjust-vma 0xf0000 $(OUT)rom16.o $(OUT)rom16.moved.o
	$(Q)$(LD) -T $(OUT)rombios.lds $(OUT)rom16.final.o $(OUT)rom32.o $(OUT)version.o -R $(OUT)rom16.moved.o -o $@

$(OUT)bios.bin.elf: $(OUT)rom.o
	@echo "  Prepping $@"
	$(Q)$(NM) $< | ./tools/checkrom.py
	$(Q)$(STRIP) $< -o $@

$(OUT)bios.bin: $(OUT)bios.bin.elf
	@echo "  Extracting binary $@"
	$(Q)$(OBJCOPY) -O binary $< $@


################ VGA build rules

# VGA src files
SRCVGA=src/output.c src/util.c vgasrc/vga.c vgasrc/vgafb.c vgasrc/vgaio.c \
       vgasrc/vgatables.c vgasrc/vgafonts.c vgasrc/clext.c

$(OUT)vgaccode.16.s: ; $(call whole-compile, $(CFLAGS16) -S -Isrc, $(SRCVGA),$@)

$(OUT)vgalayout16.o: vgaentry.S $(OUT)vgaccode.16.s $(OUT)asm-offsets.h
	@echo "  Compiling (16bit) $@"
	$(Q)$(CC) $(CFLAGS16INC) -c -D__ASSEMBLY__ -Isrc $< -o $@

$(OUT)vgarom.o: $(OUT)vgalayout16.o $(OUT)vgalayout.lds
	@echo "  Linking $@"
	$(Q)$(LD) --gc-sections -T $(OUT)vgalayout.lds $(OUT)vgalayout16.o -o $@

$(OUT)vgabios.bin.raw: $(OUT)vgarom.o
	@echo "  Extracting binary $@"
	$(Q)$(OBJCOPY) -O binary $< $@

$(OUT)vgabios.bin: $(OUT)vgabios.bin.raw
	@echo "  Finalizing rom $@"
	$(Q)./tools/buildrom.py $< $@

####### Generic rules
clean:
	$(Q)rm -rf $(OUT)

$(OUT):
	$(Q)mkdir $@

-include $(OUT)*.d
