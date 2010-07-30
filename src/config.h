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

// Configure as a coreboot payload.
#define CONFIG_COREBOOT 0

// Control how verbose debug output is.
#define CONFIG_DEBUG_LEVEL 1
// Send debugging information to serial port
#define CONFIG_DEBUG_SERIAL 0
// Screen writes are also sent to debug ports.
#define CONFIG_SCREEN_AND_DEBUG 1

// Support running hardware initialization in parallel
#define CONFIG_THREADS 1
// Allow hardware init to run in parallel with optionrom execution
#define CONFIG_THREAD_OPTIONROMS 0
// Support int13 disk/floppy drive functions
#define CONFIG_DRIVES 1
// Support floppy drive access
#define CONFIG_FLOPPY 1
// Support USB devices
#define CONFIG_USB 1
// Support USB UHCI controllers
#define CONFIG_USB_UHCI 1
// Support USB OHCI controllers
#define CONFIG_USB_OHCI 1
// Support USB EHCI controllers
#define CONFIG_USB_EHCI 1
// Support USB disks
#define CONFIG_USB_MSC 1
// Support USB hubs
#define CONFIG_USB_HUB 1
// Support USB keyboards
#define CONFIG_USB_KEYBOARD 1
// Support USB mice
#define CONFIG_USB_MOUSE 1
// Support PS2 ports (keyboard and mouse)
#define CONFIG_PS2PORT 1
// Support for IDE disk code
#define CONFIG_ATA 1
// Detect and try to use ATA bus mastering DMA controllers.
#define CONFIG_ATA_DMA 0
// Use 32bit PIO accesses on ATA (minor optimization on PCI transfers)
#define CONFIG_ATA_PIO32 0
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
// Support Post Memory Manager (PMM) entry point.
#define CONFIG_PMM 1
// Support int 19/18 system bootup support
#define CONFIG_BOOT 1
// Support an interactive boot menu at end of post.
#define CONFIG_BOOTMENU 1
// Amount of time (in ms) to wait at menu before selecting normal boot.
#define CONFIG_BOOTMENU_WAIT 2500
// Support int 14 serial port calls
#define CONFIG_SERIAL 1
// Support int 17 parallel port calls
#define CONFIG_LPT 1
// Support int 16 keyboard calls
#define CONFIG_KEYBOARD 1
// Support calling int155f on each keyboard event
#define CONFIG_KBD_CALL_INT15_4F 1
// Disable A20 on 16bit boot
#define CONFIG_DISABLE_A20 0
// Support for int15c2 mouse calls
#define CONFIG_MOUSE 1
// If the target machine has multiple independent root buses, the
// extra buses may be specified here.
#define CONFIG_PCI_ROOT1 0x00
#define CONFIG_PCI_ROOT2 0x00
// Support searching coreboot flash format.
#define CONFIG_COREBOOT_FLASH 1
// Support floppy images in the coreboot flash.
#define CONFIG_FLASH_FLOPPY 1
// Support the lzma decompression algorighm.
#define CONFIG_LZMA 1
// Support finding and running option roms during post.
#define CONFIG_OPTIONROMS 1
// Set if option roms are already copied to 0xc0000-0xf0000
#define CONFIG_OPTIONROMS_DEPLOYED 0
// When option roms are not pre-deployed, SeaBIOS can copy an optionrom
// from flash for up to 2 devices.
#define OPTIONROM_VENDEV_1 0x00000000
#define OPTIONROM_MEM_1 0x00000000
#define OPTIONROM_VENDEV_2 0x00000000
#define OPTIONROM_MEM_2 0x00000000

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
// Run the vga rom during S3 resume.
#define CONFIG_S3_RESUME_VGA_INIT 0
// Support boot splash
#define CONFIG_BOOTSPLASH 1
// define it if the (emulated) hardware supports SMM mode
#define CONFIG_USE_SMM 1
// Maximum number of map entries in the e820 map
#define CONFIG_MAX_E820 32
// Space to reserve in f-segment for dynamic allocations
#define CONFIG_MAX_BIOSTABLE 2048
// Space to reserve in high-memory for tables
#define CONFIG_MAX_HIGHTABLE (64*1024)
// Largest supported externaly facing drive id
#define CONFIG_MAX_EXTDRIVE 16

#define CONFIG_MODEL_ID      0xFC
#define CONFIG_SUBMODEL_ID   0x00
#define CONFIG_BIOS_REVISION 0x01

// Support boot from virtio storage
#define CONFIG_VIRTIO_BLK 1

// Various memory addresses used by the code.
#define BUILD_STACK_ADDR          0x7000
#define BUILD_S3RESUME_STACK_ADDR 0x1000
#define BUILD_AP_BOOT_ADDR        0x10000
#define BUILD_EBDA_MINIMUM        0x90000
#define BUILD_LOWRAM_END          0xa0000
#define BUILD_ROM_START           0xc0000
#define BUILD_BIOS_ADDR           0xf0000
#define BUILD_BIOS_SIZE           0x10000
// 32KB for shadow ram copying (works around emulator deficiencies)
#define BUILD_BIOS_TMP_ADDR       0x30000
#define BUILD_MAX_HIGHMEM         0xe0000000

// Support old pci mem assignment behaviour
//#define CONFIG_OLD_PCIMEM_ASSIGNMENT    1
#if CONFIG_OLD_PCIMEM_ASSIGNMENT
#define BUILD_PCIMEM_START        0xf0000000
#define BUILD_PCIMEM_SIZE         (BUILD_PCIMEM_END - BUILD_PCIMEM_START)
#define BUILD_PCIMEM_END          0xfec00000    /* IOAPIC is mapped at */
#define BUILD_PCIPREFMEM_START    0
#define BUILD_PCIPREFMEM_SIZE     0
#define BUILD_PCIPREFMEM_END      0
#else
#define BUILD_PCIMEM_START        0xf0000000
#define BUILD_PCIMEM_SIZE         0x08000000    /* half- of pci window */
#define BUILD_PCIMEM_END          (BUILD_PCIMEM_START + BUILD_PCIMEM_SIZE)
#define BUILD_PCIPREFMEM_START    BUILD_PCIMEM_END
#define BUILD_PCIPREFMEM_SIZE     (BUILD_PCIPREFMEM_END - BUILD_PCIPREFMEM_START)
#define BUILD_PCIPREFMEM_END      0xfec00000    /* IOAPIC is mapped at */
#endif

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
#define DEBUG_ISR_02 1
#define DEBUG_HDL_05 1
#define DEBUG_ISR_08 20
#define DEBUG_ISR_09 9
#define DEBUG_ISR_0e 9
#define DEBUG_HDL_10 20
#define DEBUG_HDL_11 2
#define DEBUG_HDL_12 2
#define DEBUG_HDL_13 10
#define DEBUG_HDL_14 2
#define DEBUG_HDL_15 9
#define DEBUG_HDL_16 9
#define DEBUG_HDL_17 2
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
#define DEBUG_HDL_pmm 1
#define DEBUG_HDL_pcibios32 9
#define DEBUG_HDL_apm 9

#define DEBUG_unimplemented 2
#define DEBUG_invalid 3
#define DEBUG_thread 2

#endif // config.h
