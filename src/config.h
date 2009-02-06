#ifndef __CONFIG_H
#define __CONFIG_H

// Configuration definitions.

//#define CONFIG_APPNAME  "QEMU"
//#define CONFIG_CPUNAME8 "QEMUCPU "
//#define CONFIG_APPNAME6 "QEMU  "
//#define CONFIG_APPNAME4 "QEMU"
#define CONFIG_APPNAME  "Bochs"
#define CONFIG_CPUNAME8 "BOCHSCPU"
#define CONFIG_APPNAME6 "BOCHS "
#define CONFIG_APPNAME4 "BXPC"

// Configure for use with KVM.
#define CONFIG_KVM 0
// Configure as a coreboot payload.
#define CONFIG_COREBOOT 0

// Control how verbose debug output is.
#define CONFIG_DEBUG_LEVEL 1

// Send debugging information to serial port
#define CONFIG_DEBUG_SERIAL 0

// Support for int13 floppy drive access
#define CONFIG_FLOPPY_SUPPORT 1
// Support for int15c2 mouse calls
#define CONFIG_PS2_MOUSE 1
// Support for IDE disk code
#define CONFIG_ATA 1
// Support calling int155f on each keyboard event
#define CONFIG_KBD_CALL_INT15_4F 1
// Support for booting from a CD
#define CONFIG_CDROM_BOOT 1
// Support for emulating a boot CD as a floppy/harddrive
#define CONFIG_CDROM_EMU 1
// Support int 1a/b1 PCI BIOS calls
#define CONFIG_PCIBIOS 1
// Support int 15/53 APM BIOS calls
#define CONFIG_APMBIOS 1
// Support PnP BIOS entry point.
#define CONFIG_PNPBIOS 1
// Support int 19/18 system bootup support
#define CONFIG_BOOT 1
// Support int 14 serial port calls
#define CONFIG_SERIAL 1
// Support int 17 parallel port calls
#define CONFIG_LPT 1
// Support int 16 keyboard calls
#define CONFIG_KEYBOARD 1
// Support finding and running option roms during post.
#define CONFIG_OPTIONROMS 1
// Set if option roms are already copied to 0xc0000-0xf0000
#define CONFIG_OPTIONROMS_DEPLOYED 1
// When option roms are not pre-deployed, SeaBIOS can copy an optionrom
// from flash for up to 2 devices.
#define OPTIONROM_BDF_1 0x0000
#define OPTIONROM_MEM_1 0x00000000
#define OPTIONROM_BDF_2 0x0000
#define OPTIONROM_MEM_2 0x00000000
// Support an interactive boot menu at end of post.
#define CONFIG_BOOTMENU 1

// Support generation of a PIR table in 0xf000 segment (for emulators)
#define CONFIG_PIRTABLE 1
// Support generation of MPTable (for emulators)
#define CONFIG_MPTABLE 1
// Support generation of SM BIOS tables (for emulators)
#define CONFIG_SMBIOS 1
// Support finding a UUID (for smbios) via "magic" outl sequence.
#define CONFIG_UUID_BACKDOOR 1
// Support generation of ACPI tables (for emulators)
#define CONFIG_ACPI 1
// Support bios callbacks specific to via vgabios.
#define CONFIG_VGAHOOKS 0
// Support S3 resume handler.
#define CONFIG_S3_RESUME 1
// define it if the (emulated) hardware supports SMM mode
#define CONFIG_USE_SMM 1
// Maximum number of map entries in the e820 map
#define CONFIG_MAX_E820 32
// Space to reserve in f-segment for run-time built bios tables.
#define CONFIG_MAX_BIOSTABLE 512

#define CONFIG_MAX_ATA_INTERFACES 4
#define CONFIG_MAX_ATA_DEVICES  (CONFIG_MAX_ATA_INTERFACES*2)

#define CONFIG_ACPI_DATA_SIZE 0x00010000L

#define CONFIG_MODEL_ID      0xFC
#define CONFIG_SUBMODEL_ID   0x00
#define CONFIG_BIOS_REVISION 0x01

// Various memory addresses used by the code.
#define BUILD_STACK_ADDR          0x7c00
#define BUILD_S3RESUME_STACK_ADDR 0x1000
#define BUILD_AP_BOOT_ADDR        0x10000
#define BUILD_BIOS_ADDR           0xf0000
#define BUILD_BIOS_SIZE           0x10000
// 64 KB used to copy the BIOS to shadow RAM
#define BUILD_BIOS_TMP_ADDR       0x30000

#define BUILD_APIC_ADDR           0xfee00000
#define BUILD_IOAPIC_ADDR         0xfec00000

#define BUILD_SMM_INIT_ADDR       0x38000
#define BUILD_SMM_ADDR            0xa8000
#define BUILD_SMM_SIZE            0x8000

// Important real-mode segments
#define SEG_IVT      0x0000
#define SEG_BDA      0x0040
#define SEG_BIOS     0xf000

// Segment definitions in protected mode (see rombios32_gdt in misc.c)
#define SEG32_MODE32_CS    (1 << 3)
#define SEG32_MODE32_DS    (2 << 3)
#define SEG32_MODE16_CS    (3 << 3)
#define SEG32_MODE16_DS    (4 << 3)
#define SEG32_MODE16BIG_CS (5 << 3)
#define SEG32_MODE16BIG_DS (6 << 3)

// Debugging levels.  If non-zero and CONFIG_DEBUG_LEVEL is greater
// than the specified value, then the corresponding irq handler will
// report every enter event.
#define DEBUG_ISR_nmi 1
#define DEBUG_HDL_05 1
#define DEBUG_ISR_08 20
#define DEBUG_ISR_09 9
#define DEBUG_ISR_0e 9
#define DEBUG_HDL_10 20
#define DEBUG_HDL_11 1
#define DEBUG_HDL_12 1
#define DEBUG_HDL_13 10
#define DEBUG_HDL_14 1
#define DEBUG_HDL_15 9
#define DEBUG_HDL_16 9
#define DEBUG_HDL_17 1
#define DEBUG_HDL_18 1
#define DEBUG_HDL_19 1
#define DEBUG_HDL_1a 9
#define DEBUG_HDL_40 1
#define DEBUG_ISR_70 9
#define DEBUG_ISR_74 9
#define DEBUG_ISR_75 1
#define DEBUG_ISR_76 10
#define DEBUG_ISR_hwpic1 5
#define DEBUG_ISR_hwpic2 5
#define DEBUG_HDL_pnp 1

#endif // config.h
