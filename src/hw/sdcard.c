// PCI SD Host Controller Interface
//
// Copyright (C) 2014  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "block.h" // struct drive_s
#include "fw/paravirt.h" // runningOnQEMU
#include "malloc.h" // malloc_fseg
#include "output.h" // znprintf
#include "pci.h" // pci_config_readl
#include "pci_ids.h" // PCI_CLASS_SYSTEM_SDHCI
#include "pci_regs.h" // PCI_BASE_ADDRESS_0
#include "stacks.h" // wait_preempt
#include "std/disk.h" // DISK_RET_SUCCESS
#include "string.h" // memset
#include "util.h" // boot_add_hd
#include "x86.h" // writel

// SDHCI MMIO registers
struct sdhci_s {
    u32 sdma_addr;
    u16 block_size;
    u16 block_count;
    u32 arg;
    u16 transfer_mode;
    u16 cmd;
    u32 response[4];
    u32 data;
    u32 present_state;
    u8 host_control;
    u8 power_control;
    u8 block_gap_control;
    u8 wakeup_control;
    u16 clock_control;
    u8 timeout_control;
    u8 software_reset;
    u16 irq_status;
    u16 error_irq_status;
    u16 irq_enable;
    u16 error_irq_enable;
    u16 irq_signal;
    u16 error_signal;
    u16 auto_cmd12;
    u8 pad_3E[2];
    u32 cap_lo, cap_hi;
    u64 max_current;
    u16 force_auto_cmd12;
    u16 force_error;
    u8 adma_error;
    u8 pad_55[3];
    u64 adma_addr;
    u8 pad_60[156];
    u16 slot_irq;
    u16 controller_version;
} PACKED;

// SDHCI commands
#define SC_GO_IDLE_STATE        ((0<<8) | 0x20)
#define SC_ALL_SEND_CID         ((2<<8) | 0x21)
#define SC_SEND_RELATIVE_ADDR   ((3<<8) | 0x22)
#define SC_SELECT_DESELECT_CARD ((7<<8) | 0x23)
#define SC_SEND_IF_COND         ((8<<8) | 0x22)
#define SC_READ_SINGLE          ((17<<8) | 0x22)
#define SC_READ_MULTIPLE        ((18<<8) | 0x22)
#define SC_WRITE_SINGLE         ((24<<8) | 0x22)
#define SC_WRITE_MULTIPLE       ((25<<8) | 0x22)
#define SC_APP_CMD              ((55<<8) | 0x22)
#define SC_APP_SEND_OP_COND ((41<<8) | 0x22)

// SDHCI irqs
#define SI_CMD_COMPLETE (1<<0)
#define SI_TRANS_DONE   (1<<1)
#define SI_WRITE_READY  (1<<4)
#define SI_READ_READY   (1<<5)

// SDHCI present_state flags
#define SP_CMD_INHIBIT   (1<<0)
#define SP_DAT_INHIBIT   (1<<1)
#define SP_CARD_INSERTED (1<<16)

// SDHCI transfer_mode flags
#define ST_BLOCKCOUNT (1<<1)
#define ST_AUTO_CMD12 (1<<2)
#define ST_READ       (1<<4)
#define ST_MULTIPLE   (1<<5)

// SDHCI capabilities flags
#define SD_CAPLO_V33             (1<<24)
#define SD_CAPLO_BASECLOCK_SHIFT 8
#define SD_CAPLO_BASECLOCK_MASK  0x3f

// SDHCI clock control flags
#define SCC_INTERNAL_ENABLE (1<<0)
#define SCC_STABLE          (1<<1)
#define SCC_CLOCK_ENABLE    (1<<2)
#define SCC_SDCLK_SHIFT     8

// SDHCI power control flags
#define SPC_POWER_ON (1<<0)
#define SPC_V33      0x0e

// SDHCI result flags
#define SR_OCR_CCS     (1<<30)
#define SR_OCR_NOTBUSY (1<<31)

// SDHCI timeouts
#define SDHCI_POWER_OFF_TIME   1
#define SDHCI_POWER_ON_TIME    1
#define SDHCI_CLOCK_ON_TIME    1 // 74 clock cycles
#define SDHCI_POWERUP_TIMEOUT  1000
#define SDHCI_PIO_TIMEOUT      1000  // XXX - these are just made up
#define SDHCI_TRANSFER_TIMEOUT 10000

// Internal 'struct drive_s' storage for a detected card
struct sddrive_s {
    struct drive_s drive;
    struct sdhci_s *regs;
    int card_type;
};

// SD card types
#define SF_MMC  0
#define SF_SDSC 1
#define SF_SDHC 2

// Repeatedly read a u16 register until the specific value is found
static int
waitw(u16 *reg, u16 mask, u16 value, u32 end)
{
    for (;;) {
        u16 v = readw(reg);
        if ((v & mask) == value)
            return 0;
        if (timer_check(end)) {
            warn_timeout();
            return -1;
        }
        yield();
    }
}

// Send a command to the card.
static int
sdcard_pio(struct sdhci_s *regs, int cmd, u32 *param)
{
    u32 end = timer_calc(SDHCI_PIO_TIMEOUT);
    u16 busyf = SP_CMD_INHIBIT | ((cmd & 0x03) == 0x03 ? SP_DAT_INHIBIT : 0);
    int ret = waitw((u16*)&regs->present_state, busyf, 0, end);
    if (ret)
        return ret;
    // Send command
    writel(&regs->arg, *param);
    writew(&regs->cmd, cmd);
    ret = waitw(&regs->irq_status, SI_CMD_COMPLETE, SI_CMD_COMPLETE, end);
    if (ret)
        return ret;
    writew(&regs->irq_status, SI_CMD_COMPLETE);
    // Read response
    memcpy(param, regs->response, sizeof(regs->response));
    dprintf(9, "sdcard cmd %x response %x %x %x %x\n"
            , cmd, param[0], param[1], param[2], param[3]);
    return 0;
}

// Send an "app specific" command to the card.
static int
sdcard_pio_app(struct sdhci_s *regs, int cmd, u32 *param)
{
    u32 aparam[4] = {};
    int ret = sdcard_pio(regs, SC_APP_CMD, aparam);
    if (ret)
        return ret;
    return sdcard_pio(regs, cmd, param);
}

// Send a command to the card which transfers data.
static int
sdcard_pio_transfer(struct sddrive_s *drive, int cmd, u32 addr
                    , void *data, int count)
{
    // Send command
    writel(&drive->regs->block_size, DISK_SECTOR_SIZE);
    writew(&drive->regs->block_count, count); // XXX - SC_SET_BLOCK_COUNT?
    int isread = cmd != SC_WRITE_SINGLE && cmd != SC_WRITE_MULTIPLE;
    u16 tmode = ((count > 1 ? ST_MULTIPLE|ST_AUTO_CMD12|ST_BLOCKCOUNT : 0)
                 | (isread ? ST_READ : 0));
    writew(&drive->regs->transfer_mode, tmode);
    if (drive->card_type < SF_SDHC)
        addr *= DISK_SECTOR_SIZE;
    u32 param[4] = { addr };
    int ret = sdcard_pio(drive->regs, cmd, param);
    if (ret)
        return ret;
    // Read/write data
    u32 end = timer_calc(SDHCI_TRANSFER_TIMEOUT);
    u16 cbit = isread ? SI_READ_READY : SI_WRITE_READY;
    while (count--) {
        ret = waitw(&drive->regs->irq_status, cbit, cbit, end);
        if (ret)
            return ret;
        writew(&drive->regs->irq_status, cbit);
        int i;
        for (i=0; i<DISK_SECTOR_SIZE/4; i++) {
            if (isread)
                *(u32*)data = readl(&drive->regs->data);
            else
                writel(&drive->regs->data, *(u32*)data);
            data += 4;
        }
    }
    // Complete command
    // XXX - SC_STOP_TRANSMISSION?
    ret = waitw(&drive->regs->irq_status, SI_TRANS_DONE, SI_TRANS_DONE, end);
    if (ret)
        return ret;
    writew(&drive->regs->irq_status, SI_TRANS_DONE);
    return 0;
}

// Read/write a block of data to/from the card.
static int
sdcard_readwrite(struct disk_op_s *op, int iswrite)
{
    struct sddrive_s *drive = container_of(
        op->drive_gf, struct sddrive_s, drive);
    int cmd = iswrite ? SC_WRITE_SINGLE : SC_READ_SINGLE;
    if (op->count > 1)
        cmd = iswrite ? SC_WRITE_MULTIPLE : SC_READ_MULTIPLE;
    int ret = sdcard_pio_transfer(drive, cmd, op->lba, op->buf_fl, op->count);
    if (ret)
        return DISK_RET_EBADTRACK;
    return DISK_RET_SUCCESS;
}

int
sdcard_process_op(struct disk_op_s *op)
{
    if (!CONFIG_SDCARD)
        return 0;
    switch (op->command) {
    case CMD_READ:
        return sdcard_readwrite(op, 0);
    case CMD_WRITE:
        return sdcard_readwrite(op, 1);
    default:
        return default_process_op(op);
    }
}


/****************************************************************
 * Setup
 ****************************************************************/

// Initialize an SD card
static int
sdcard_card_setup(struct sdhci_s *regs)
{
    // XXX - works on QEMU; probably wont on real hardware!

    // Reset card
    u32 param[4] = { };
    int ret = sdcard_pio_app(regs, SC_GO_IDLE_STATE, param);
    if (ret)
        return ret;
    // Let card know SDHC/SDXC is supported and confirm voltage
    param[0] = 0x1aa;
    ret = sdcard_pio_app(regs, SC_SEND_IF_COND, param);
    if (ret)
        return ret;
    u32 hcs = 0;
    if (param[0] == 0x1aa)
        hcs = 0x40000000;
    // Verify SD card (instead of MMC or SDIO)
    param[0] = 0x00;
    ret = sdcard_pio_app(regs, SC_APP_SEND_OP_COND, param);
    if (ret)
        return ret;
    // Init card
    u32 end = timer_calc(SDHCI_POWERUP_TIMEOUT);
    for (;;) {
        param[0] = hcs | (1<<20); // SDHC support and voltage level
        ret = sdcard_pio_app(regs, SC_APP_SEND_OP_COND, param);
        if (ret)
            return ret;
        if (param[0] & SR_OCR_NOTBUSY)
            break;
        if (timer_check(end)) {
            warn_timeout();
            return -1;
        }
    }
    int card_type = (param[0] & SR_OCR_CCS) ? SF_SDHC : SF_SDSC;
    param[0] = 0x00;
    // Select card
    ret = sdcard_pio(regs, SC_ALL_SEND_CID, param);
    if (ret)
        return ret;
    param[0] = 0x00;
    ret = sdcard_pio(regs, SC_SEND_RELATIVE_ADDR, param);
    if (ret)
        return ret;
    u16 rca = param[0] >> 16;
    param[0] = rca << 16;
    ret = sdcard_pio(regs, SC_SELECT_DESELECT_CARD, param);
    if (ret)
        return ret;
    return card_type;
}

static int
sdcard_set_frequency(struct sdhci_s *regs, u32 khz)
{
    u32 cap = readl(&regs->cap_lo);
    u32 base_freq = (cap >> SD_CAPLO_BASECLOCK_SHIFT) & SD_CAPLO_BASECLOCK_MASK;
    if (!base_freq) {
        dprintf(1, "Unknown base frequency for SD controller\n");
        return -1;
    }
    if (!(cap & SD_CAPLO_V33)) {
        dprintf(1, "SD controller does not support 3.3V power\n");
        return -1;
    }
    // Set new frequency
    u32 divisor = DIV_ROUND_UP(base_freq * 1000, khz);
    divisor = divisor > 1 ? 1 << __ffs(divisor-1) : 0;
    u16 creg = (divisor << SCC_SDCLK_SHIFT) | SCC_INTERNAL_ENABLE;
    writew(&regs->clock_control, 0);
    writew(&regs->clock_control, creg);
    // Wait for frequency to become active
    u32 end = timer_calc(SDHCI_PIO_TIMEOUT);
    int ret = waitw((u16*)&regs->clock_control, SCC_STABLE, SCC_STABLE, end);
    if (ret)
        return ret;
    // Enable SD clock
    writew(&regs->clock_control, creg | SCC_CLOCK_ENABLE);
    return 0;
}

// Setup and configure an SD card controller
static void
sdcard_controller_setup(void *data)
{
    struct pci_device *pci = data;
    u16 bdf = pci->bdf;
    wait_preempt();  // Avoid pci_config_readl when preempting
    struct sdhci_s *regs = (void*)pci_config_readl(bdf, PCI_BASE_ADDRESS_0);
    pci_config_maskw(bdf, PCI_COMMAND, 0,
                     PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    // Initialize controller
    if (!runningOnQEMU())
        // XXX - this init logic will probably only work on qemu!
        return;
    u32 present_state = readl(&regs->present_state);
    if (!(present_state & SP_CARD_INSERTED))
        // No card present
        return;
    writew(&regs->irq_signal, 0);
    writew(&regs->irq_enable, 0xffff);
    writew(&regs->error_signal, 0);
    writeb(&regs->timeout_control, 0x0e); // Set to max timeout
    writeb(&regs->power_control, 0);
    msleep(SDHCI_POWER_OFF_TIME);
    writeb(&regs->power_control, SPC_V33 | SPC_POWER_ON);
    msleep(SDHCI_POWER_ON_TIME);
    int ret = sdcard_set_frequency(regs, 400);
    if (ret)
        return;
    msleep(SDHCI_CLOCK_ON_TIME);

    // Initialize card
    int card_type = sdcard_card_setup(regs);
    if (card_type < 0)
        return;
    ret = sdcard_set_frequency(regs, 25000);
    if (ret)
        return;

    // Register drive
    struct sddrive_s *drive = malloc_fseg(sizeof(*drive));
    if (!drive) {
        warn_noalloc();
        return;
    }
    memset(drive, 0, sizeof(*drive));
    drive->drive.type = DTYPE_SDCARD;
    drive->drive.blksize = DISK_SECTOR_SIZE;
    drive->drive.sectors = (u64)-1; // XXX
    drive->regs = regs;
    drive->card_type = card_type;

    dprintf(1, "Found SD Card at %02x:%02x.%x\n"
            , pci_bdf_to_bus(bdf), pci_bdf_to_dev(bdf), pci_bdf_to_fn(bdf));
    char *desc = znprintf(MAXDESCSIZE, "SD Card"); // XXX
    boot_add_hd(&drive->drive, desc, bootprio_find_pci_device(pci));
}

void
sdcard_setup(void)
{
    if (!CONFIG_SDCARD)
        return;

    struct pci_device *pci;
    foreachpci(pci) {
        if (pci->class != PCI_CLASS_SYSTEM_SDHCI || pci->prog_if >= 2)
            // Not an SDHCI controller following SDHCI spec
            continue;
        run_thread(sdcard_controller_setup, pci);
    }
}
