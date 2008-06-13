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

// Configure as a coreboot payload.
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
// Support generation of a PIR table in 0xf000 segment (for emulators)
#define CONFIG_PIRTABLE 1
// Support generation of ACPI PIR tables (for emulators)
#define CONFIG_ACPI 1
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

#define CONFIG_ACPI_DATA_SIZE 0x00010000L

#define CONFIG_MODEL_ID      0xFC
#define CONFIG_SUBMODEL_ID   0x00
#define CONFIG_BIOS_REVISION 0x01

// Various memory addresses used by the code.
#define BUILD_STACK_ADDR        0xfffe
#define BUILD_CPU_COUNT_ADDR    0xf000
#define BUILD_AP_BOOT_ADDR      0x10000
#define BUILD_BSS_ADDR          0x40000
 /* 64 KB used to copy the BIOS to shadow RAM */
#define BUILD_BIOS_TMP_ADDR     0x30000

#define BUILD_PM_IO_BASE        0xb000
#define BUILD_SMB_IO_BASE       0xb100
#define BUILD_SMI_CMD_IO_ADDR   0xb2

// Start of fixed addresses in 0xf0000 segment.
#define BUILD_START_FIXED       0xe050

// Debugging levels.  If non-zero and CONFIG_DEBUG_LEVEL is greater
// than the specified value, then the corresponding irq handler will
// report every enter event.
#define DEBUG_HDL_05 1
#define DEBUG_HDL_10 1
#define DEBUG_HDL_11 1
#define DEBUG_HDL_12 1
#define DEBUG_HDL_15 9
#define DEBUG_ISR_nmi 1
#define DEBUG_ISR_75 1
#define DEBUG_HDL_16 9
#define DEBUG_ISR_09 9
#define DEBUG_HDL_18 1
#define DEBUG_HDL_19 1
#define DEBUG_HDL_1a 9
#define DEBUG_ISR_1c 9
#define DEBUG_ISR_08 9
#define DEBUG_ISR_70 9
#define DEBUG_HDL_40 1
#define DEBUG_HDL_13 9
#define DEBUG_ISR_76 9
#define DEBUG_HDL_14 1
#define DEBUG_HDL_17 1
#define DEBUG_ISR_74 9
#define DEBUG_ISR_0e 9

#endif // config.h
