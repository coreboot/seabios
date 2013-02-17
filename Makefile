# SeaBIOS build system
#
# Copyright (C) 2008-2012  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU LGPLv3 license.

# Output directory
OUT=out/

# Source files
SRCBOTH=misc.c stacks.c pmm.c output.c util.c block.c floppy.c ata.c mouse.c \
    kbd.c pci.c serial.c clock.c pic.c cdrom.c ps2port.c smp.c resume.c \
    pnpbios.c pirtable.c vgahooks.c ramdisk.c pcibios.c blockcmd.c \
    usb.c usb-uhci.c usb-ohci.c usb-ehci.c usb-hid.c usb-msc.c \
    virtio-ring.c virtio-pci.c virtio-blk.c virtio-scsi.c apm.c ahci.c \
    usb-uas.c lsi-scsi.c esp-scsi.c megasas.c
SRC16=$(SRCBOTH) system.c disk.c font.c
SRC32FLAT=$(SRCBOTH) post.c shadow.c memmap.c coreboot.c boot.c \
    acpi.c smm.c mptable.c smbios.c pciinit.c optionroms.c mtrr.c \
    lzmadecode.c bootsplash.c jpeg.c usb-hub.c paravirt.c \
    biostables.c xen.c bmp.c romfile.c
SRC32SEG=util.c output.c pci.c pcibios.c apm.c stacks.c

# Default compiler flags
cc-option=$(shell if test -z "`$(1) $(2) -S -o /dev/null -xc /dev/null 2>&1`" \
    ; then echo "$(2)"; else echo "$(3)"; fi ;)

COMMONCFLAGS := -I$(OUT) -Os -MD -g \
    -Wall -Wno-strict-aliasing -Wold-style-definition \
    $(call cc-option,$(CC),-Wtype-limits,) \
    -m32 -march=i386 -mregparm=3 -mpreferred-stack-boundary=2 \
    -minline-all-stringops \
    -freg-struct-return -ffreestanding -fno-delete-null-pointer-checks \
    -ffunction-sections -fdata-sections -fno-common
COMMONCFLAGS += $(call cc-option,$(CC),-nopie,)
COMMONCFLAGS += $(call cc-option,$(CC),-fno-stack-protector,)
COMMONCFLAGS += $(call cc-option,$(CC),-fno-stack-protector-all,)

CFLAGS32FLAT := $(COMMONCFLAGS) -DMODE16=0 -DMODESEGMENT=0 -fomit-frame-pointer
CFLAGSSEG := $(COMMONCFLAGS) -DMODESEGMENT=1 -fno-defer-pop \
    $(call cc-option,$(CC),-fno-jump-tables,-DMANUAL_NO_JUMP_TABLE) \
    $(call cc-option,$(CC),-fno-tree-switch-conversion,)
CFLAGS32SEG := $(CFLAGSSEG) -DMODE16=0 -fomit-frame-pointer
CFLAGS16INC := $(CFLAGSSEG) -DMODE16=1 -Wa,src/code16gcc.s \
    $(call cc-option,$(CC),--param large-stack-frame=4,-fno-inline)
CFLAGS16 := $(CFLAGS16INC) -fomit-frame-pointer

# Run with "make V=1" to see the actual compile commands
ifdef V
Q=
else
Q=@
MAKEFLAGS += --no-print-directory
endif

# Common command definitions
export HOSTCC             := $(CC)
export CONFIG_SHELL       := sh
export KCONFIG_AUTOHEADER := autoconf.h
export KCONFIG_CONFIG     := $(CURDIR)/.config
AS=as
OBJCOPY=objcopy
OBJDUMP=objdump
STRIP=strip
PYTHON=python
IASL:=iasl

# Default targets
-include $(KCONFIG_CONFIG)

target-y = $(OUT) $(OUT)bios.bin
target-$(CONFIG_BUILD_VGABIOS) += $(OUT)vgabios.bin

all: $(target-y)

# Make definitions
.PHONY : all clean distclean FORCE
.DELETE_ON_ERROR:

vpath %.c src vgasrc
vpath %.S src vgasrc


################ Common build rules

# Verify the build environment works.
TESTGCC:=$(shell OUT="$(OUT)" CC="$(CC)" LD="$(LD)" IASL="$(IASL)" tools/test-build.sh)
ifeq "$(TESTGCC)" "-1"
$(error "Please upgrade the build environment")
endif

ifeq "$(TESTGCC)" "0"
# Use -fwhole-program
CFLAGSWHOLE=-fwhole-program -DWHOLE_PROGRAM
endif

# Do a whole file compile by textually including all C code.
define whole-compile
@echo "  Compiling whole program $3"
$(Q)printf '$(foreach i,$2,#include "$(CURDIR)/$i"\n)' > $3.tmp.c
$(Q)$(CC) $1 $(CFLAGSWHOLE) -c $3.tmp.c -o $3
endef

%.strip.o: %.o
	@echo "  Stripping $@"
	$(Q)$(STRIP) $< -o $@

$(OUT)%.s: %.c
	@echo "  Compiling to assembler $@"
	$(Q)$(CC) $(CFLAGS16) -S -c $< -o $@

$(OUT)%.o: %.c $(OUT)autoconf.h
	@echo "  Compile checking $@"
	$(Q)$(CC) $(CFLAGS32FLAT) -c $< -o $@

$(OUT)%.lds: %.lds.S
	@echo "  Precompiling $@"
	$(Q)$(CPP) -P -D__ASSEMBLY__ $< -o $@


################ Main BIOS build rules

$(OUT)asm-offsets.s: $(OUT)autoconf.h

$(OUT)asm-offsets.h: $(OUT)asm-offsets.s
	@echo "  Generating offset file $@"
	$(Q)./tools/gen-offsets.sh $< $@

$(OUT)ccode16.o: $(OUT)autoconf.h $(patsubst %.c, $(OUT)%.o,$(SRC16)) ; $(call whole-compile, $(CFLAGS16), $(addprefix src/, $(SRC16)),$@)

$(OUT)code32seg.o: $(OUT)autoconf.h $(patsubst %.c, $(OUT)%.o,$(SRC32SEG)) ; $(call whole-compile, $(CFLAGS32SEG), $(addprefix src/, $(SRC32SEG)),$@)

$(OUT)ccode32flat.o: $(OUT)autoconf.h $(patsubst %.c, $(OUT)%.o,$(SRC32FLAT)) ; $(call whole-compile, $(CFLAGS32FLAT), $(addprefix src/, $(SRC32FLAT)),$@)

$(OUT)romlayout.o: romlayout.S $(OUT)asm-offsets.h
	@echo "  Compiling (16bit) $@"
	$(Q)$(CC) $(CFLAGS16) -c -D__ASSEMBLY__ $< -o $@

$(OUT)romlayout16.lds: $(OUT)ccode32flat.o $(OUT)code32seg.o $(OUT)ccode16.o $(OUT)romlayout.o tools/layoutrom.py
	@echo "  Building ld scripts"
	$(Q)./tools/buildversion.sh $(OUT)version.c
	$(Q)$(CC) $(CFLAGS32FLAT) -c $(OUT)version.c -o $(OUT)version.o
	$(Q)$(LD) -melf_i386 -r $(OUT)ccode32flat.o $(OUT)version.o -o $(OUT)code32flat.o
	$(Q)$(LD) -melf_i386 -r $(OUT)ccode16.o $(OUT)romlayout.o -o $(OUT)code16.o
	$(Q)$(OBJDUMP) -thr $(OUT)code32flat.o > $(OUT)code32flat.o.objdump
	$(Q)$(OBJDUMP) -thr $(OUT)code32seg.o > $(OUT)code32seg.o.objdump
	$(Q)$(OBJDUMP) -thr $(OUT)code16.o > $(OUT)code16.o.objdump
	$(Q)$(PYTHON) ./tools/layoutrom.py $(OUT)code16.o.objdump $(OUT)code32seg.o.objdump $(OUT)code32flat.o.objdump $(OUT)romlayout16.lds $(OUT)romlayout32seg.lds $(OUT)romlayout32flat.lds

# These are actually built by tools/layoutrom.py above, but by pulling them
# into an extra rule we prevent make -j from spawning layoutrom.py 4 times.
$(OUT)romlayout32seg.lds $(OUT)romlayout32flat.lds $(OUT)code32flat.o $(OUT)code16.o: $(OUT)romlayout16.lds

$(OUT)rom16.o: $(OUT)code16.o $(OUT)romlayout16.lds
	@echo "  Linking $@"
	$(Q)$(LD) -T $(OUT)romlayout16.lds $< -o $@

$(OUT)rom32seg.o: $(OUT)code32seg.o $(OUT)romlayout32seg.lds
	@echo "  Linking $@"
	$(Q)$(LD) -T $(OUT)romlayout32seg.lds $< -o $@

$(OUT)rom.o: $(OUT)rom16.strip.o $(OUT)rom32seg.strip.o $(OUT)code32flat.o $(OUT)romlayout32flat.lds
	@echo "  Linking $@"
	$(Q)$(LD) -T $(OUT)romlayout32flat.lds $(OUT)rom16.strip.o $(OUT)rom32seg.strip.o $(OUT)code32flat.o -o $@

$(OUT)bios.bin.elf $(OUT)bios.bin: $(OUT)rom.o tools/checkrom.py
	@echo "  Prepping $@"
	$(Q)$(OBJDUMP) -thr $< > $<.objdump
	$(Q)$(OBJCOPY) -O binary $< $(OUT)bios.bin.raw
	$(Q)$(PYTHON) ./tools/checkrom.py $<.objdump $(OUT)bios.bin.raw $(OUT)bios.bin
	$(Q)$(STRIP) -R .comment $< -o $(OUT)bios.bin.elf


################ VGA build rules

# VGA src files
SRCVGA=src/output.c src/util.c src/pci.c \
    vgasrc/vgabios.c vgasrc/vgafb.c vgasrc/vgafonts.c vgasrc/vbe.c \
    vgasrc/stdvga.c vgasrc/stdvgamodes.c vgasrc/stdvgaio.c \
    vgasrc/clext.c vgasrc/bochsvga.c vgasrc/geodevga.c

CFLAGS16VGA = $(CFLAGS16INC) -Isrc

$(OUT)vgaccode16.raw.s: $(OUT)autoconf.h ; $(call whole-compile, $(CFLAGS16VGA) -S, $(SRCVGA),$@)

$(OUT)vgaccode16.o: $(OUT)vgaccode16.raw.s
	@echo "  Fixup VGA rom assembler"
	$(Q)$(PYTHON) ./tools/vgafixup.py $< $(OUT)vgaccode16.s
	$(Q)$(AS) --32 src/code16gcc.s $(OUT)vgaccode16.s -o $@

$(OUT)vgaentry.o: vgaentry.S $(OUT)autoconf.h
	@echo "  Compiling (16bit) $@"
	$(Q)$(CC) $(CFLAGS16VGA) -c -D__ASSEMBLY__ $< -o $@

$(OUT)vgarom.o: $(OUT)vgaccode16.o $(OUT)vgaentry.o $(OUT)vgalayout.lds
	@echo "  Linking $@"
	$(Q)./tools/buildversion.sh $(OUT)vgaversion.c VAR16
	$(Q)$(CC) $(CFLAGS16VGA) -c $(OUT)vgaversion.c -o $(OUT)vgaversion.o
	$(Q)$(LD) --gc-sections -T $(OUT)vgalayout.lds $(OUT)vgaccode16.o $(OUT)vgaentry.o $(OUT)vgaversion.o -o $@

$(OUT)vgabios.bin.raw: $(OUT)vgarom.o
	@echo "  Extracting binary $@"
	$(Q)$(OBJCOPY) -O binary $< $@

$(OUT)vgabios.bin: $(OUT)vgabios.bin.raw tools/buildrom.py
	@echo "  Finalizing rom $@"
	$(Q)$(PYTHON) ./tools/buildrom.py $< $@


################ DSDT build rules

iasl-option=$(shell if test -z "`$(1) $(2) 2>&1 > /dev/null`" \
    ; then echo "$(2)"; else echo "$(3)"; fi ;)

$(OUT)%.hex: src/%.dsl ./tools/acpi_extract_preprocess.py ./tools/acpi_extract.py
	@echo "  Compiling IASL $@"
	$(Q)cpp -P $< > $(OUT)$*.dsl.i.orig
	$(Q)$(PYTHON) ./tools/acpi_extract_preprocess.py $(OUT)$*.dsl.i.orig > $(OUT)$*.dsl.i
	$(Q)$(IASL) $(call iasl-option,$(IASL),-Pn,) -vs -l -tc -p $(OUT)$* $(OUT)$*.dsl.i
	$(Q)$(PYTHON) ./tools/acpi_extract.py $(OUT)$*.lst > $(OUT)$*.off
	$(Q)cat $(OUT)$*.off > $@

$(OUT)acpi.o: $(OUT)acpi-dsdt.hex $(OUT)ssdt-proc.hex $(OUT)ssdt-pcihp.hex $(OUT)ssdt-susp.hex $(OUT)q35-acpi-dsdt.hex

################ Kconfig rules

define do-kconfig
$(Q)mkdir -p $(OUT)/tools/kconfig/lxdialog
$(Q)mkdir -p $(OUT)/include/config
$(Q)$(MAKE) -C $(OUT) -f $(CURDIR)/tools/kconfig/Makefile srctree=$(CURDIR) src=tools/kconfig obj=tools/kconfig Q=$(Q) Kconfig=$(CURDIR)/src/Kconfig $1
endef

$(OUT)autoconf.h : $(KCONFIG_CONFIG) ; $(call do-kconfig, silentoldconfig)
$(KCONFIG_CONFIG): ; $(call do-kconfig, defconfig)
%onfig: ; $(call do-kconfig, $@)
help: ; $(call do-kconfig, $@)


################ Generic rules

clean:
	$(Q)rm -rf $(OUT)

distclean: clean
	$(Q)rm -f .config .config.old

$(OUT):
	$(Q)mkdir $@

-include $(OUT)*.d
