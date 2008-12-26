// Hooks for via vgabios calls into main bios.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "bregs.h" // set_code_fail
#include "biosvar.h" // GET_GLOBAL
#include "pci.h" // pci_find_device
#include "pci_ids.h" // PCI_VENDOR_ID_VIA
#include "util.h" // handle_155f
#include "config.h" // CONFIG_*

static void
handle_155f01(struct bregs *regs)
{
    regs->eax = 0x5f;
    regs->cl = 2; // panel type =  2 = 1024 * 768
    set_success(regs);
    dprintf(1, "Warning: VGA panel type is hardcoded\n");
}

static void
handle_155f02(struct bregs *regs)
{
    regs->eax = 0x5f;
    regs->bx = 2;
    regs->cx = 0x401;  // PAL + crt only
    regs->dx = 0;  // TV Layout - default
    set_success(regs);
    dprintf(1, "Warning: VGA TV/CRT output type is hardcoded\n");
}

static int
getFBSize()
{
    /* Find K8M890 */
    int bdf = pci_find_device(PCI_VENDOR_ID_VIA, 0x3336);
    if (bdf < 0)
        goto err;

    /* FB config */
    u8 reg = pci_config_readb(bdf, 0xa1);

    /* GFX disabled ? */
    if (!(reg & 0x80))
        goto err;

    static u8 mem_power[] = {0, 3, 4, 5, 6, 7, 8, 9};
    return GET_GLOBAL(mem_power[(reg >> 4) & 0x7]);
err:
    dprintf(1, "Warning: VGA memory size is hardcoded\n");
    return 5; // 32M frame buffer
}

static int
getRamSpeed()
{
    int bdf = pci_find_device(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_K8_NB_MEMCTL);
    if (bdf < 0)
        goto err;

    /* mem clk 0 = DDR2 400 */
    u8 reg = pci_config_readb(bdf, 0x94) & 0x7;
    return reg + 6;
err:
    dprintf(1, "Warning: VGA memory clock speed is hardcoded\n");
    return 4; // MCLK = DDR266
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

    K8M890 BIOS wants only this call (Desktop NoTv)
*/

static void
handle_155f18(struct bregs *regs)
{
    regs->eax = 0x5f;
    u32 ramspeed = getRamSpeed();
    u32 fbsize = getFBSize();
    regs->ebx = 0x500 | (ramspeed << 4) | fbsize;
    regs->ecx = 0x060;
    set_success(regs);
}

static void
handle_155f19(struct bregs *regs)
{
    set_fail_silent(regs);
}

static void
handle_155fXX(struct bregs *regs)
{
    set_code_fail(regs, RET_EUNSUPPORTED);
}

void
handle_155f(struct bregs *regs)
{
    if (! CONFIG_VGAHOOKS) {
        handle_155fXX(regs);
        return;
    }

    switch (regs->al) {
    case 0x01: handle_155f01(regs); break;
    case 0x02: handle_155f02(regs); break;
    case 0x18: handle_155f18(regs); break;
    case 0x19: handle_155f19(regs); break;
    default:   handle_155fXX(regs); break;
    }
}
