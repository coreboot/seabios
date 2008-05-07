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

#define CONFIG_FLOPPY_SUPPORT 1
#define CONFIG_PS2_MOUSE 1
#define CONFIG_ATA 1
#define CONFIG_KBD_CALL_INT15_4F 1
#define CONFIG_CDROM_BOOT 1
#define CONFIG_CDROM_EMU 1
#define CONFIG_PCIBIOS 1

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

#endif // config.h
