// Low level AHCI disk access
//
// Copyright (C) 2010 Gerd Hoffmann <kraxel@redhat.com>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "types.h" // u8
#include "ioport.h" // inb
#include "util.h" // dprintf
#include "biosvar.h" // GET_EBDA
#include "pci.h" // foreachpci
#include "pci_ids.h" // PCI_CLASS_STORAGE_OTHER
#include "pci_regs.h" // PCI_INTERRUPT_LINE
#include "boot.h" // add_bcv_hd
#include "disk.h" // struct ata_s
#include "ata.h" // ATA_CB_STAT
#include "ahci.h" // CDB_CMD_READ_10
#include "blockcmd.h" // CDB_CMD_READ_10

#define AHCI_MAX_RETRIES 5

/****************************************************************
 * these bits must run in both 16bit and 32bit modes
 ****************************************************************/

// prepare sata command fis
static void sata_prep_simple(struct sata_cmd_fis *fis, u8 command)
{
    memset_fl(fis, 0, sizeof(*fis));
    SET_FLATPTR(fis->command, command);
}

static void sata_prep_readwrite(struct sata_cmd_fis *fis,
                                struct disk_op_s *op, int iswrite)
{
    u64 lba = op->lba;
    u8 command;

    memset_fl(fis, 0, sizeof(*fis));

    if (op->count >= (1<<8) || lba + op->count >= (1<<28)) {
        SET_FLATPTR(fis->sector_count2, op->count >> 8);
        SET_FLATPTR(fis->lba_low2,      lba >> 24);
        SET_FLATPTR(fis->lba_mid2,      lba >> 32);
        SET_FLATPTR(fis->lba_high2,     lba >> 40);
        lba &= 0xffffff;
        command = (iswrite ? ATA_CMD_WRITE_DMA_EXT
                   : ATA_CMD_READ_DMA_EXT);
    } else {
        command = (iswrite ? ATA_CMD_WRITE_DMA
                   : ATA_CMD_READ_DMA);
    }
    SET_FLATPTR(fis->feature,      1); /* dma */
    SET_FLATPTR(fis->command,      command);
    SET_FLATPTR(fis->sector_count, op->count);
    SET_FLATPTR(fis->lba_low,      lba);
    SET_FLATPTR(fis->lba_mid,      lba >> 8);
    SET_FLATPTR(fis->lba_high,     lba >> 16);
    SET_FLATPTR(fis->device,       ((lba >> 24) & 0xf) | ATA_CB_DH_LBA);
}

static void sata_prep_atapi(struct sata_cmd_fis *fis, u16 blocksize)
{
    memset_fl(fis, 0, sizeof(*fis));
    SET_FLATPTR(fis->command,  ATA_CMD_PACKET);
    SET_FLATPTR(fis->feature,  1); /* dma */
    SET_FLATPTR(fis->lba_mid,  blocksize);
    SET_FLATPTR(fis->lba_high, blocksize >> 8);
}

// ahci register access helpers
static u32 ahci_ctrl_readl(struct ahci_ctrl_s *ctrl, u32 reg)
{
    u32 addr = GET_GLOBALFLAT(ctrl->iobase) + reg;
    return pci_readl(addr);
}

static void ahci_ctrl_writel(struct ahci_ctrl_s *ctrl, u32 reg, u32 val)
{
    u32 addr = GET_GLOBALFLAT(ctrl->iobase) + reg;
    pci_writel(addr, val);
}

static u32 ahci_port_to_ctrl(u32 pnr, u32 port_reg)
{
    u32 ctrl_reg = 0x100;
    ctrl_reg += pnr * 0x80;
    ctrl_reg += port_reg;
    return ctrl_reg;
}

static u32 ahci_port_readl(struct ahci_ctrl_s *ctrl, u32 pnr, u32 reg)
{
    u32 ctrl_reg = ahci_port_to_ctrl(pnr, reg);
    return ahci_ctrl_readl(ctrl, ctrl_reg);
}

static void ahci_port_writel(struct ahci_ctrl_s *ctrl, u32 pnr, u32 reg, u32 val)
{
    u32 ctrl_reg = ahci_port_to_ctrl(pnr, reg);
    ahci_ctrl_writel(ctrl, ctrl_reg, val);
}

// submit ahci command + wait for result
static int ahci_command(struct ahci_port_s *port, int iswrite, int isatapi,
                        void *buffer, u32 bsize)
{
    u32 val, status, success, flags;
    struct ahci_ctrl_s *ctrl = GET_GLOBAL(port->ctrl);
    struct ahci_cmd_s  *cmd  = GET_GLOBAL(port->cmd);
    struct ahci_fis_s  *fis  = GET_GLOBAL(port->fis);
    struct ahci_list_s *list = GET_GLOBAL(port->list);
    u32 pnr                  = GET_GLOBAL(port->pnr);

    SET_FLATPTR(cmd->fis.reg,       0x27);
    SET_FLATPTR(cmd->fis.pmp_type,  (1 << 7)); /* cmd fis */
    SET_FLATPTR(cmd->prdt[0].base,  ((u32)buffer));
    SET_FLATPTR(cmd->prdt[0].baseu, 0);
    SET_FLATPTR(cmd->prdt[0].flags, bsize-1);

    val = ahci_port_readl(ctrl, pnr, PORT_CMD);
    ahci_port_writel(ctrl, pnr, PORT_CMD, val | PORT_CMD_START);

    if (ahci_port_readl(ctrl, pnr, PORT_CMD_ISSUE))
        return -1;

    flags = ((1 << 16) | /* one prd entry */
             (1 << 10) | /* clear busy on ok */
             (iswrite ? (1 << 6) : 0) |
             (isatapi ? (1 << 5) : 0) |
             (4 << 0)); /* fis length (dwords) */
    SET_FLATPTR(list[0].flags, flags);
    SET_FLATPTR(list[0].bytes,  bsize);
    SET_FLATPTR(list[0].base,   ((u32)(cmd)));
    SET_FLATPTR(list[0].baseu,  0);

    dprintf(2, "AHCI/%d: send cmd ...\n", pnr);
    SET_FLATPTR(fis->rfis[2], 0);
    ahci_port_writel(ctrl, pnr, PORT_SCR_ACT, 1);
    ahci_port_writel(ctrl, pnr, PORT_CMD_ISSUE, 1);
    while (ahci_port_readl(ctrl, pnr, PORT_CMD_ISSUE)) {
        yield();
    }
    while ((status = GET_FLATPTR(fis->rfis[2])) == 0) {
        yield();
    }

    success = (0x00 == (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_DF |
                                  ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR)) &&
               ATA_CB_STAT_RDY == (status & (ATA_CB_STAT_RDY)));
    dprintf(2, "AHCI/%d: ... finished, status 0x%x, %s\n", pnr,
            status, success ? "OK" : "ERROR");
    return success ? 0 : -1;
}

#define CDROM_CDB_SIZE 12

int ahci_cmd_data(struct disk_op_s *op, void *cdbcmd, u16 blocksize)
{
    if (! CONFIG_AHCI)
        return 0;

    struct ahci_port_s *port = container_of(
        op->drive_g, struct ahci_port_s, drive);
    struct ahci_cmd_s *cmd = GET_GLOBAL(port->cmd);
    u8 *atapi = cdbcmd;
    int i, rc;

    sata_prep_atapi(&cmd->fis, blocksize);
    for (i = 0; i < CDROM_CDB_SIZE; i++) {
        SET_FLATPTR(cmd->atapi[i], atapi[i]);
    }
    rc = ahci_command(port, 0, 1, op->buf_fl,
                      op->count * blocksize);
    if (rc < 0)
        return DISK_RET_EBADTRACK;
    return DISK_RET_SUCCESS;
}

// read/write count blocks from a harddrive.
static int
ahci_disk_readwrite(struct disk_op_s *op, int iswrite)
{
    struct ahci_port_s *port = container_of(
        op->drive_g, struct ahci_port_s, drive);
    struct ahci_cmd_s *cmd = GET_GLOBAL(port->cmd);
    int rc;

    sata_prep_readwrite(&cmd->fis, op, iswrite);
    rc = ahci_command(port, iswrite, 0, op->buf_fl,
                      op->count * DISK_SECTOR_SIZE);
    dprintf(2, "ahci disk %s, lba %6x, count %3x, buf %p, rc %d\n",
            iswrite ? "write" : "read", (u32)op->lba, op->count, op->buf_fl, rc);
    if (rc < 0)
        return DISK_RET_EBADTRACK;
    return DISK_RET_SUCCESS;
}

// command demuxer
int process_ahci_op(struct disk_op_s *op)
{
    struct ahci_port_s *port;
    u32 atapi;

    if (!CONFIG_AHCI)
        return 0;

    port = container_of(op->drive_g, struct ahci_port_s, drive);
    atapi = GET_GLOBAL(port->atapi);

    if (atapi) {
        switch (op->command) {
        case CMD_READ:
            return cdb_read(op);
        case CMD_WRITE:
        case CMD_FORMAT:
            return DISK_RET_EWRITEPROTECT;
        case CMD_RESET:
            /* FIXME: what should we do here? */
        case CMD_VERIFY:
        case CMD_SEEK:
            return DISK_RET_SUCCESS;
        default:
            dprintf(1, "AHCI: unknown cdrom command %d\n", op->command);
            op->count = 0;
            return DISK_RET_EPARAM;
        }
    } else {
        switch (op->command) {
        case CMD_READ:
            return ahci_disk_readwrite(op, 0);
        case CMD_WRITE:
            return ahci_disk_readwrite(op, 1);
        case CMD_RESET:
            /* FIXME: what should we do here? */
        case CMD_FORMAT:
        case CMD_VERIFY:
        case CMD_SEEK:
            return DISK_RET_SUCCESS;
        default:
            dprintf(1, "AHCI: unknown disk command %d\n", op->command);
            op->count = 0;
            return DISK_RET_EPARAM;
        }
    }
}

/****************************************************************
 * everything below is pure 32bit code
 ****************************************************************/

static void
ahci_port_reset(struct ahci_ctrl_s *ctrl, u32 pnr)
{
    u32 val, count = 0;

    /* disable FIS + CMD */
    val = ahci_port_readl(ctrl, pnr, PORT_CMD);
    while (val & (PORT_CMD_FIS_RX | PORT_CMD_START |
                  PORT_CMD_FIS_ON | PORT_CMD_LIST_ON) &&
           count < AHCI_MAX_RETRIES) {
        val &= ~(PORT_CMD_FIS_RX | PORT_CMD_START);
        ahci_port_writel(ctrl, pnr, PORT_CMD, val);
        ndelay(500);
        val = ahci_port_readl(ctrl, pnr, PORT_CMD);
        count++;
    }

    /* clear status */
    val = ahci_port_readl(ctrl, pnr, PORT_SCR_ERR);
    if (val)
        ahci_port_writel(ctrl, pnr, PORT_SCR_ERR, val);

    /* disable + clear IRQs */
    ahci_port_writel(ctrl, pnr, PORT_IRQ_MASK, val);
    val = ahci_port_readl(ctrl, pnr, PORT_IRQ_STAT);
    if (val)
        ahci_port_writel(ctrl, pnr, PORT_IRQ_STAT, val);
}

static int
ahci_port_probe(struct ahci_ctrl_s *ctrl, u32 pnr)
{
    u32 val, count = 0;

    val = ahci_port_readl(ctrl, pnr, PORT_TFDATA);
    while (val & ((1 << 7) /* BSY */ |
                  (1 << 3) /* DRQ */)) {
        ndelay(500);
        val = ahci_port_readl(ctrl, pnr, PORT_TFDATA);
        count++;
        if (count >= AHCI_MAX_RETRIES)
            return -1;
    }

    val = ahci_port_readl(ctrl, pnr, PORT_SCR_STAT);
    if ((val & 0x07) != 0x03)
        return -1;
    return 0;
}

#define MAXMODEL 40

static struct ahci_port_s*
ahci_port_init(struct ahci_ctrl_s *ctrl, u32 pnr)
{
    struct ahci_port_s *port = malloc_fseg(sizeof(*port));
    char model[MAXMODEL+1];
    u16 buffer[256];
    u32 val;
    int rc;

    if (!port) {
        warn_noalloc();
        return NULL;
    }
    port->pnr = pnr;
    port->ctrl = ctrl;
    port->list = memalign_low(1024, 1024);
    port->fis = memalign_low(256, 256);
    port->cmd = memalign_low(256, 256);
    if (port->list == NULL || port->fis == NULL || port->cmd == NULL) {
        warn_noalloc();
        return NULL;
    }
    memset(port->list, 0, 1024);
    memset(port->fis, 0, 256);
    memset(port->cmd, 0, 256);

    ahci_port_writel(ctrl, pnr, PORT_LST_ADDR, (u32)port->list);
    ahci_port_writel(ctrl, pnr, PORT_FIS_ADDR, (u32)port->fis);
    val = ahci_port_readl(ctrl, pnr, PORT_CMD);
    ahci_port_writel(ctrl, pnr, PORT_CMD, val | PORT_CMD_FIS_RX);

    sata_prep_simple(&port->cmd->fis, ATA_CMD_IDENTIFY_PACKET_DEVICE);
    rc = ahci_command(port, 0, 0, buffer, sizeof(buffer));
    if (rc == 0) {
        port->atapi = 1;
    } else {
        port->atapi = 0;
        sata_prep_simple(&port->cmd->fis, ATA_CMD_IDENTIFY_DEVICE);
        rc = ahci_command(port, 0, 0, buffer, sizeof(buffer));
        if (rc < 0)
            goto err;
    }

    port->drive.type = DTYPE_AHCI;
    port->drive.cntl_id = pnr;
    port->drive.removable = (buffer[0] & 0x80) ? 1 : 0;

    if (!port->atapi) {
        // found disk (ata)
        port->drive.blksize = DISK_SECTOR_SIZE;
        port->drive.pchs.cylinders = buffer[1];
        port->drive.pchs.heads = buffer[3];
        port->drive.pchs.spt = buffer[6];

        u64 sectors;
        if (buffer[83] & (1 << 10)) // word 83 - lba48 support
            sectors = *(u64*)&buffer[100]; // word 100-103
        else
            sectors = *(u32*)&buffer[60]; // word 60 and word 61
        port->drive.sectors = sectors;
        u64 adjsize = sectors >> 11;
        char adjprefix = 'M';
        if (adjsize >= (1 << 16)) {
            adjsize >>= 10;
            adjprefix = 'G';
        }
        char *desc = znprintf(MAXDESCSIZE
                              , "AHCI/%d: %s ATA-%d Hard-Disk (%u %ciBytes)"
                              , port->pnr
                              , ata_extract_model(model, MAXMODEL, buffer)
                              , ata_extract_version(buffer)
                              , (u32)adjsize, adjprefix);
        dprintf(1, "%s\n", desc);

        // Register with bcv system.
        boot_add_hd(&port->drive, desc, -1);
    } else {
        // found cdrom (atapi)
        port->drive.blksize = CDROM_SECTOR_SIZE;
        port->drive.sectors = (u64)-1;
        u8 iscd = ((buffer[0] >> 8) & 0x1f) == 0x05;
        char *desc = znprintf(MAXDESCSIZE
                              , "DVD/CD [AHCI/%d: %s ATAPI-%d %s]"
                              , port->pnr
                              , ata_extract_model(model, MAXMODEL, buffer)
                              , ata_extract_version(buffer)
                              , (iscd ? "DVD/CD" : "Device"));
        dprintf(1, "%s\n", desc);

        // fill cdidmap
        if (iscd)
            boot_add_cd(&port->drive, desc, -1);
    }

    return port;

err:
    dprintf(1, "AHCI/%d: init failure, reset\n", port->pnr);
    ahci_port_reset(ctrl, pnr);
    return NULL;
}

// Detect any drives attached to a given controller.
static void
ahci_detect(void *data)
{
    struct ahci_ctrl_s *ctrl = data;
    struct ahci_port_s *port;
    u32 pnr, max;
    int rc;

    max = ctrl->caps & 0x1f;
    for (pnr = 0; pnr <= max; pnr++) {
        if (!(ctrl->ports & (1 << pnr)))
            continue;
        dprintf(2, "AHCI/%d: probing\n", pnr);
        ahci_port_reset(ctrl, pnr);
        rc = ahci_port_probe(ctrl, pnr);
        dprintf(1, "AHCI/%d: link %s\n", pnr, rc == 0 ? "up" : "down");
        if (rc != 0)
            continue;
        port = ahci_port_init(ctrl, pnr);
    }
}

// Initialize an ata controller and detect its drives.
static void
ahci_init_controller(int bdf)
{
    struct ahci_ctrl_s *ctrl = malloc_fseg(sizeof(*ctrl));
    u32 val;

    if (!ctrl) {
        warn_noalloc();
        return;
    }
    ctrl->pci_bdf = bdf;
    ctrl->iobase = pci_config_readl(bdf, PCI_BASE_ADDRESS_5);
    ctrl->irq = pci_config_readb(bdf, PCI_INTERRUPT_LINE);
    dprintf(1, "AHCI controller at %02x.%x, iobase %x, irq %d\n",
            bdf >> 3, bdf & 7, ctrl->iobase, ctrl->irq);

    pci_config_maskw(bdf, PCI_COMMAND, 0,
                     PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    val = ahci_ctrl_readl(ctrl, HOST_CTL);
    ahci_ctrl_writel(ctrl, HOST_CTL, val | HOST_CTL_AHCI_EN);

    ctrl->caps = ahci_ctrl_readl(ctrl, HOST_CAP);
    ctrl->ports = ahci_ctrl_readl(ctrl, HOST_PORTS_IMPL);
    dprintf(2, "AHCI: cap 0x%x, ports_impl 0x%x\n",
            ctrl->caps, ctrl->ports);

    run_thread(ahci_detect, ctrl);
}

// Locate and init ahci controllers.
static void
ahci_init(void)
{
    // Scan PCI bus for ATA adapters
    int bdf, max;
    foreachpci(bdf, max) {
        if (pci_config_readw(bdf, PCI_CLASS_DEVICE) != PCI_CLASS_STORAGE_SATA)
            continue;
        if (pci_config_readb(bdf, PCI_CLASS_PROG) != 1 /* AHCI rev 1 */)
            continue;
        ahci_init_controller(bdf);
    }
}

void
ahci_setup(void)
{
    ASSERT32FLAT();
    if (!CONFIG_AHCI)
        return;

    dprintf(3, "init ahci\n");
    ahci_init();
}
