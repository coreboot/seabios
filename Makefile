# Legacy Bios build system
#
# Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

# Output directory
OUT=out/

# Source files
SRCBOTH=output.c util.c floppy.c ata.c kbd.c pci.c boot.c serial.c clock.c
SRC16=$(SRCBOTH) disk.c system.c mouse.c cdrom.c apm.c pcibios.c
SRC32=$(SRCBOTH) post.c rombios32.c post_menu.c
TABLESRC=font.c cbt.c floppy_dbt.c

cc-option = $(shell if test -z "`$(1) $(2) -S -o /dev/null -xc \
              /dev/null 2>&1`"; then echo "$(2)"; else echo "$(3)"; fi ;)

# Default compiler flags
COMMONCFLAGS = -Wall -Os -MD -m32 -march=i386 -mregparm=3 \
               -mpreferred-stack-boundary=2 -mrtd \
               -ffreestanding -fwhole-program -fomit-frame-pointer \
               -fno-delete-null-pointer-checks
COMMONCFLAGS += $(call cc-option,$(CC),-nopie,)
COMMONCFLAGS += $(call cc-option,$(CC),-fno-stack-protector,)
COMMONCFLAGS += $(call cc-option,$(CC),-fno-stack-protector-all,)

CFLAGS = $(COMMONCFLAGS) -g
CFLAGS16INC = $(COMMONCFLAGS) -DMODE16 -fno-jump-tables
CFLAGS16 = $(CFLAGS16INC) -g

TABLETMP=$(addprefix $(OUT), $(patsubst %.c,%.16.s,$(TABLESRC)))
all: $(OUT) $(OUT)rom.bin $(TABLETMP)

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

# Do a whole file compile - two methods are supported.  The first
# involves including all the content textually via #include
# directives.  The second method uses gcc's "-combine" option.
ifdef AVOIDCOMBINE
DEPHACK=
define whole-compile
@echo "  Compiling whole program $3"
$(Q)/bin/echo -e '$(foreach i,$2,#include "../$i"\n)' > $3.tmp.c
$(Q)$(CC) $1 -c $3.tmp.c -o $3
endef
else
# Ugly way to get gcc to generate .d files correctly.
DEPHACK=-combine src/null.c
define whole-compile
@echo "  Compiling whole program $3"
$(Q)$(CC) $1 -combine -c $2 -o $3
endef
endif


$(OUT)%.proc.16.s: $(OUT)%.16.s
	@echo "  Moving data sections to text in $<"
	$(Q)sed 's/\t\.section\t\.rodata.*// ; s/\t\.data//' < $< > $@

$(OUT)%.16.s: %.c
	@echo "  Generating assembler for $<"
	$(Q)$(CC) $(CFLAGS16INC) $(DEPHACK) -S -c $< -o $@

$(OUT)%.lds: %.lds.S
	@echo "  Precompiling $<"
	$(Q)$(CPP) -P $< -o $@

$(OUT)%.bin: $(OUT)%.o
	@echo "  Extracting binary $@"
	$(Q)objcopy -O binary $< $@

$(OUT)%.offset.auto.h: $(OUT)%.o
	@echo "  Generating symbol offset header $@"
	$(Q)nm $< | ./tools/defsyms.py $@

$(OUT)blob.16.s: ; $(call whole-compile, $(CFLAGS16) -S, $(addprefix src/, $(SRC16)),$@)

TABLEASM=$(addprefix $(OUT), $(patsubst %.c,%.proc.16.s,$(TABLESRC)))
$(OUT)romlayout16.o: romlayout.S $(OUT)blob.proc.16.s $(TABLEASM)
	@echo "  Generating 16bit layout of $@"
	$(Q)$(CC) $(CFLAGS16) -c $< -o $@

$(OUT)rom16.o: $(OUT)romlayout16.o
	@echo "  Linking $@"
	$(Q)ld -melf_i386 -e post16 -Ttext 0 $< -o $@

$(OUT)romlayout32.o: $(OUT)rom16.offset.auto.h ; $(call whole-compile, $(CFLAGS), $(addprefix src/, $(SRC32)),$@)

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
