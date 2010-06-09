// Hooks for via vgabios calls into main bios.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "bregs.h" // set_code_invalid
#include "biosvar.h" // GET_GLOBAL
#include "pci.h" // pci_find_device
#include "pci_regs.h" // PCI_VENDOR_ID
#include "pci_ids.h" // PCI_VENDOR_ID_VIA
#include "util.h" // handle_155f
#include "config.h" // CONFIG_*

// The Bus/Dev/Fn of the primary VGA device.
int VGAbdf VAR16VISIBLE;
// Coreboot board detected.
int CBmainboard VAR16VISIBLE;

#define MAINBOARD_DEFAULT	0
#define KONTRON_986LCD_M	1
#define GETAC_P470		2
#define RODA_RK886EX		3

struct mainboards {
	char *vendor;
	char *device;
	int type;
};

struct mainboards mainboard_list[] = {
	{ "KONTRON",	"986LCD-M",	KONTRON_986LCD_M },
	{ "GETAC",	"P470",		GETAC_P470 },
	{ "RODA",	"RK886EX",	RODA_RK886EX },
};

static void
handle_155fXX(struct bregs *regs)
{
    set_code_unimplemented(regs, RET_EUNSUPPORTED);
}


/****************************************************************
 * Via hooks
 ****************************************************************/

static void
via_155f01(struct bregs *regs)
{
    regs->eax = 0x5f;
    regs->cl = 2; // panel type =  2 = 1024 * 768
    set_success(regs);
    dprintf(1, "Warning: VGA panel type is hardcoded\n");
}

static void
via_155f02(struct bregs *regs)
{
    regs->eax = 0x5f;
    regs->bx = 2;
    regs->cx = 0x401;  // PAL + crt only
    regs->dx = 0;  // TV Layout - default
    set_success(regs);
    dprintf(1, "Warning: VGA TV/CRT output type is hardcoded\n");
}

static int
getFBSize(u16 bdf)
{
    /* FB config */
    u8 reg = pci_config_readb(bdf, 0xa1);

    /* GFX disabled ? */
    if (!(reg & 0x80))
        return -1;

    static u8 mem_power[] VAR16 = {0, 3, 4, 5, 6, 7, 8, 9};
    return GET_GLOBAL(mem_power[(reg >> 4) & 0x7]);
}

static int
getViaRamSpeed(u16 bdf)
{
    return (pci_config_readb(bdf, 0x90) & 0x07) + 3;
}

static int
getAMDRamSpeed(void)
{
    int bdf = pci_find_device(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_K8_NB_MEMCTL);
    if (bdf < 0)
        return -1;

    /* mem clk 0 = DDR2 400 */
    return (pci_config_readb(bdf, 0x94) & 0x7) + 6;
}

/* int 0x15 - 5f18

   ECX = unknown/dont care
   EBX[3..0] Frame Buffer Size 2^N MiB
   EBX[7..4] Memory speed:
       0: SDR  66Mhz
       1: SDR 100Mhz
       2: SDR 133Mhz
       3: DDR 100Mhz (PC1600 or DDR200)
       4: DDR 133Mhz (PC2100 or DDR266)
       5: DDR 166Mhz (PC2700 or DDR333)
       6: DDR 200Mhz (PC3200 or DDR400)
       7: DDR2 133Mhz (DDR2 533)
       8: DDR2 166Mhz (DDR2 667)
       9: DDR2 200Mhz (DDR2 800)
       A: DDR2 233Mhz (DDR2 1066)
       B: and above: Unknown
   EBX[?..8] Total memory size?
   EAX = 0x5f for success
*/

#define PCI_DEVICE_ID_VIA_K8M890CE_3    0x3336
#define PCI_DEVICE_ID_VIA_VX855_MEMCTRL 0x3409

static void
via_155f18(struct bregs *regs)
{
    int ramspeed, fbsize;

    int bdf = pci_find_device(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_K8M890CE_3);
    if (bdf >= 0) {
        fbsize = getFBSize(bdf);
        ramspeed = getAMDRamSpeed();
        goto done;
    }
    bdf = pci_find_device(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_VX855_MEMCTRL);
    if (bdf >= 0) {
        fbsize = getFBSize(bdf);
        ramspeed = getViaRamSpeed(bdf);
        goto done;
    }

    dprintf(1, "Warning: VGA memory size and speed is hardcoded\n");
    fbsize = 5; // 32M frame buffer
    ramspeed = 4; // MCLK = DDR266

done:
    if (fbsize < 0 || ramspeed < 0) {
        set_code_invalid(regs, RET_EUNSUPPORTED);
        return;
    }
    regs->eax = 0x5f;
    regs->ebx = 0x500 | (ramspeed << 4) | fbsize;
    regs->ecx = 0x060;
    set_success(regs);
}

static void
via_155f19(struct bregs *regs)
{
    set_invalid_silent(regs);
}

static void
via_155f(struct bregs *regs)
{
    switch (regs->al) {
    case 0x01: via_155f01(regs); break;
    case 0x02: via_155f02(regs); break;
    case 0x18: via_155f18(regs); break;
    case 0x19: via_155f19(regs); break;
    default:   handle_155fXX(regs); break;
    }
}

/****************************************************************
 * Intel VGA hooks
 ****************************************************************/
#define BOOT_DISPLAY_DEFAULT	(0)
#define BOOT_DISPLAY_CRT        (1 << 0)
#define BOOT_DISPLAY_TV         (1 << 1)
#define BOOT_DISPLAY_EFP        (1 << 2)
#define BOOT_DISPLAY_LCD        (1 << 3)
#define BOOT_DISPLAY_CRT2       (1 << 4)
#define BOOT_DISPLAY_TV2        (1 << 5)
#define BOOT_DISPLAY_EFP2       (1 << 6)
#define BOOT_DISPLAY_LCD2       (1 << 7)
 
static void
roda_155f35(struct bregs *regs)
{
    regs->ax = 0x005f;
    // regs->cl = BOOT_DISPLAY_DEFAULT;
    regs->cl = BOOT_DISPLAY_LCD;
    set_success(regs);
}

static void
roda_155f40(struct bregs *regs)
{
    u8 display_id;
    //display_id = inb(0x60f) & 0x0f; // Correct according to Crete
    display_id = 3; // Correct according to empirical studies

    regs->ax = 0x005f;
    regs->cl = display_id;
    set_success(regs);
}

static void
roda_155f(struct bregs *regs)
{
    dprintf(1, "Executing RODA specific interrupt %02x.\n", regs->al);
    switch (regs->al) {
    case 0x35: roda_155f35(regs); break;
    case 0x40: roda_155f40(regs); break;
    default:   handle_155fXX(regs); break;
    }
}

static void
kontron_155f35(struct bregs *regs)
{
    regs->ax = 0x005f;
    regs->cl = BOOT_DISPLAY_CRT;
    set_success(regs);
}

static void
kontron_155f40(struct bregs *regs)
{
    u8 display_id;
    display_id = 3;

    regs->ax = 0x005f;
    regs->cl = display_id;
    set_success(regs);
}

static void
kontron_155f(struct bregs *regs)
{
    dprintf(1, "Executing Kontron specific interrupt %02x.\n", regs->al);
    switch (regs->al) {
    case 0x35: kontron_155f35(regs); break;
    case 0x40: kontron_155f40(regs); break;
    default:   handle_155fXX(regs); break;
    }
}

static void
getac_155f(struct bregs *regs)
{
    dprintf(1, "Executing Getac specific interrupt %02x.\n", regs->al);
    switch (regs->al) {
    default:   handle_155fXX(regs); break;
    }
}

/****************************************************************
 * Entry and setup
 ****************************************************************/

// Main 16bit entry point
void
handle_155f(struct bregs *regs)
{
    int bdf, cbmb;

    if (! CONFIG_VGAHOOKS)
        goto fail;

    cbmb = GET_GLOBAL(CBmainboard);

    switch (cbmb) {
    case KONTRON_986LCD_M:
        kontron_155f(regs);
	return;
    case RODA_RK886EX:
        roda_155f(regs);
	return;
    case GETAC_P470:
        getac_155f(regs);
	return;
    case MAINBOARD_DEFAULT:
        bdf = GET_GLOBAL(VGAbdf);
        if (bdf < 0)
            goto fail;

        u16 vendor = pci_config_readw(bdf, PCI_VENDOR_ID);
        if (vendor == PCI_VENDOR_ID_VIA) {
            via_155f(regs);
            return;
        }
    }

fail:
    handle_155fXX(regs);
}

// Setup
void
vgahook_setup(const char *vendor, const char *part)
{
    int i;

    if (! CONFIG_VGAHOOKS)
        return;

    CBmainboard = 0;
    for (i=0; i<(sizeof(mainboard_list) / sizeof(mainboard_list[0])); i++) {
        if (!strcmp(vendor, mainboard_list[i].vendor) &&
            !strcmp(part, mainboard_list[i].device)) {
            printf("Found mainboard %s %s\n", vendor, part); 
            CBmainboard = mainboard_list[i].type;
            break;
        }
    }
}
