#ifndef __CONFIG_H
#define __CONFIG_H

// Configuration definitions.

/* Dont support QEMU BIOS by default.
 * Change CONFIG_QEMU to 1 to support QEMU. */
#define CONFIG_QEMU 0

#if (QEMU_SUPPORT == 1)
#define CONFIG_APPNAME "QEMU"
#else
#define CONFIG_APPNAME "Bochs"
#endif

// Configure as a payload coreboot payload.
#define CONFIG_COREBOOT 0

// Control how verbose debug output is.
#define CONFIG_DEBUG_LEVEL 1

// Send debugging information to serial port
#define CONFIG_DEBUG_SERIAL 0

#define CONFIG_FLOPPY_SUPPORT 1
#define CONFIG_PS2_MOUSE 1
#define CONFIG_ATA 1
#define CONFIG_KBD_CALL_INT15_4F 1
#define CONFIG_CDROM_BOOT 1
#define CONFIG_CDROM_EMU 1
// Support built-in PIR table in 0xf000 segment
#define CONFIG_PIRTABLE 1
// Support int 1a/b1 PCI BIOS calls
#define CONFIG_PCIBIOS 1
// Support int 15/53 APM BIOS calls
#define CONFIG_APMBIOS 1

/* define it if the (emulated) hardware supports SMM mode */
#define CONFIG_USE_SMM 1

/* if true, put the MP float table and ACPI RSDT in EBDA and the MP
   table in RAM. Unfortunately, Linux has bugs with that, so we prefer
   to modify the BIOS in shadow RAM */
#define CONFIG_USE_EBDA_TABLES 0

#define CONFIG_MAX_ATA_INTERFACES 4
#define CONFIG_MAX_ATA_DEVICES  (CONFIG_MAX_ATA_INTERFACES*2)

#define CONFIG_STACK_SEGMENT 0x00
#define CONFIG_STACK_OFFSET  0xfffe

#define CONFIG_ACPI_DATA_SIZE 0x00010000L

#define CONFIG_MODEL_ID      0xFC
#define CONFIG_SUBMODEL_ID   0x00
#define CONFIG_BIOS_REVISION 0x01

// Start of fixed addresses in 0xf0000 segment.
#define CONFIG_START_FIXED 0xe050

#endif // config.h
