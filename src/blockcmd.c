// Support for several common scsi like command data block requests
//
// Copyright (C) 2010  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "biosvar.h" // GET_GLOBAL
#include "util.h" // dprintf
#include "byteorder.h" // be32_to_cpu
#include "disk.h" // struct disk_op_s
#include "blockcmd.h" // struct cdb_request_sense
#include "ata.h" // atapi_cmd_data
#include "ahci.h" // atapi_cmd_data
#include "usb-msc.h" // usb_cmd_data
#include "usb-uas.h" // usb_cmd_data
#include "virtio-scsi.h" // virtio_scsi_cmd_data
#include "lsi-scsi.h" // lsi_scsi_cmd_data
#include "esp-scsi.h" // esp_scsi_cmd_data
#include "megasas.h" // megasas_cmd_data
#include "boot.h" // boot_add_hd

// Route command to low-level handler.
static int
cdb_cmd_data(struct disk_op_s *op, void *cdbcmd, u16 blocksize)
{
    u8 type = GET_GLOBAL(op->drive_g->type);
    switch (type) {
    case DTYPE_ATA_ATAPI:
        return atapi_cmd_data(op, cdbcmd, blocksize);
    case DTYPE_USB:
        return usb_cmd_data(op, cdbcmd, blocksize);
    case DTYPE_UAS:
        return uas_cmd_data(op, cdbcmd, blocksize);
    case DTYPE_AHCI_ATAPI:
        return ahci_cmd_data(op, cdbcmd, blocksize);
    case DTYPE_VIRTIO_SCSI:
        return virtio_scsi_cmd_data(op, cdbcmd, blocksize);
    case DTYPE_LSI_SCSI:
        return lsi_scsi_cmd_data(op, cdbcmd, blocksize);
    case DTYPE_ESP_SCSI:
        return esp_scsi_cmd_data(op, cdbcmd, blocksize);
    case DTYPE_MEGASAS:
        return megasas_cmd_data(op, cdbcmd, blocksize);
    default:
        op->count = 0;
        return DISK_RET_EPARAM;
    }
}

// Determine if the command is a request to pull data from the device
int
cdb_is_read(u8 *cdbcmd, u16 blocksize)
{
    return blocksize && cdbcmd[0] != CDB_CMD_WRITE_10;
}

int
scsi_is_ready(struct disk_op_s *op)
{
    dprintf(6, "scsi_is_ready (drive=%p)\n", op->drive_g);

    /* Retry TEST UNIT READY for 5 seconds unless MEDIUM NOT PRESENT is
     * reported by the device.  If the device reports "IN PROGRESS",
     * 30 seconds is added. */
    int in_progress = 0;
    u64 end = calc_future_tsc(5000);
    for (;;) {
        if (check_tsc(end)) {
            dprintf(1, "test unit ready failed\n");
            return -1;
        }

        int ret = cdb_test_unit_ready(op);
        if (!ret)
            // Success
            break;

        struct cdbres_request_sense sense;
        ret = cdb_get_sense(op, &sense);
        if (ret)
            // Error - retry.
            continue;

        // Sense succeeded.
        if (sense.asc == 0x3a) { /* MEDIUM NOT PRESENT */
            dprintf(1, "Device reports MEDIUM NOT PRESENT\n");
            return -1;
        }

        if (sense.asc == 0x04 && sense.ascq == 0x01 && !in_progress) {
            /* IN PROGRESS OF BECOMING READY */
            printf("Waiting for device to detect medium... ");
            /* Allow 30 seconds more */
            end = calc_future_tsc(30000);
            in_progress = 1;
        }
    }
    return 0;
}

// Validate drive, find block size / sector count, and register drive.
int
scsi_init_drive(struct drive_s *drive, const char *s, int prio)
{
    struct disk_op_s dop;
    memset(&dop, 0, sizeof(dop));
    dop.drive_g = drive;
    struct cdbres_inquiry data;
    int ret = cdb_get_inquiry(&dop, &data);
    if (ret)
        return ret;
    char vendor[sizeof(data.vendor)+1], product[sizeof(data.product)+1];
    char rev[sizeof(data.rev)+1];
    strtcpy(vendor, data.vendor, sizeof(vendor));
    nullTrailingSpace(vendor);
    strtcpy(product, data.product, sizeof(product));
    nullTrailingSpace(product);
    strtcpy(rev, data.rev, sizeof(rev));
    nullTrailingSpace(rev);
    int pdt = data.pdt & 0x1f;
    int removable = !!(data.removable & 0x80);
    dprintf(1, "%s vendor='%s' product='%s' rev='%s' type=%d removable=%d\n"
            , s, vendor, product, rev, pdt, removable);
    drive->removable = removable;

    if (pdt == SCSI_TYPE_CDROM) {
        drive->blksize = CDROM_SECTOR_SIZE;
        drive->sectors = (u64)-1;

        char *desc = znprintf(MAXDESCSIZE, "DVD/CD [%s Drive %s %s %s]"
                              , s, vendor, product, rev);
        boot_add_cd(drive, desc, prio);
        return 0;
    }

    ret = scsi_is_ready(&dop);
    if (ret) {
        dprintf(1, "scsi_is_ready returned %d\n", ret);
        return ret;
    }

    struct cdbres_read_capacity capdata;
    ret = cdb_read_capacity(&dop, &capdata);
    if (ret)
        return ret;

    // READ CAPACITY returns the address of the last block.
    // We do not bother with READ CAPACITY(16) because BIOS does not support
    // 64-bit LBA anyway.
    drive->blksize = be32_to_cpu(capdata.blksize);
    if (drive->blksize != DISK_SECTOR_SIZE) {
        dprintf(1, "%s: unsupported block size %d\n", s, drive->blksize);
        return -1;
    }
    drive->sectors = (u64)be32_to_cpu(capdata.sectors) + 1;
    dprintf(1, "%s blksize=%d sectors=%d\n"
            , s, drive->blksize, (unsigned)drive->sectors);

    // We do not recover from USB stalls, so try to be safe and avoid
    // sending the command if the (obsolete, but still provided by QEMU)
    // fixed disk geometry page may not be supported.
    //
    // We could also send the command only to small disks (e.g. <504MiB)
    // but some old USB keys only support a very small subset of SCSI which
    // does not even include the MODE SENSE command!
    //
    if (! CONFIG_COREBOOT && memcmp(vendor, "QEMU", 5) == 0) {
        struct cdbres_mode_sense_geom geomdata;
        ret = cdb_mode_sense_geom(&dop, &geomdata);
        if (ret == 0) {
            u32 cylinders;
            cylinders = geomdata.cyl[0] << 16;
            cylinders |= geomdata.cyl[1] << 8;
            cylinders |= geomdata.cyl[2];
            if (cylinders && geomdata.heads &&
                drive->sectors <= 0xFFFFFFFFULL &&
                ((u32)drive->sectors % (geomdata.heads * cylinders) == 0)) {
                drive->pchs.cylinders = cylinders;
                drive->pchs.heads = geomdata.heads;
                drive->pchs.spt = (u32)drive->sectors / (geomdata.heads * cylinders);
            }
        }
    }

    char *desc = znprintf(MAXDESCSIZE, "%s Drive %s %s %s"
                          , s, vendor, product, rev);
    boot_add_hd(drive, desc, prio);
    return 0;
}

int
cdb_get_inquiry(struct disk_op_s *op, struct cdbres_inquiry *data)
{
    struct cdb_request_sense cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.command = CDB_CMD_INQUIRY;
    cmd.length = sizeof(*data);
    op->count = 1;
    op->buf_fl = data;
    return cdb_cmd_data(op, &cmd, sizeof(*data));
}

// Request SENSE
int
cdb_get_sense(struct disk_op_s *op, struct cdbres_request_sense *data)
{
    struct cdb_request_sense cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.command = CDB_CMD_REQUEST_SENSE;
    cmd.length = sizeof(*data);
    op->count = 1;
    op->buf_fl = data;
    return cdb_cmd_data(op, &cmd, sizeof(*data));
}

// Test unit ready
int
cdb_test_unit_ready(struct disk_op_s *op)
{
    struct cdb_request_sense cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.command = CDB_CMD_TEST_UNIT_READY;
    op->count = 0;
    op->buf_fl = NULL;
    return cdb_cmd_data(op, &cmd, 0);
}

// Request capacity
int
cdb_read_capacity(struct disk_op_s *op, struct cdbres_read_capacity *data)
{
    struct cdb_read_capacity cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.command = CDB_CMD_READ_CAPACITY;
    op->count = 1;
    op->buf_fl = data;
    return cdb_cmd_data(op, &cmd, sizeof(*data));
}

// Mode sense, geometry page.
int
cdb_mode_sense_geom(struct disk_op_s *op, struct cdbres_mode_sense_geom *data)
{
    struct cdb_mode_sense cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.command = CDB_CMD_MODE_SENSE;
    cmd.flags = 8; /* DBD */
    cmd.page = MODE_PAGE_HD_GEOMETRY;
    cmd.count = cpu_to_be16(sizeof(*data));
    op->count = 1;
    op->buf_fl = data;
    return cdb_cmd_data(op, &cmd, sizeof(*data));
}

// Read sectors.
int
cdb_read(struct disk_op_s *op)
{
    struct cdb_rwdata_10 cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.command = CDB_CMD_READ_10;
    cmd.lba = cpu_to_be32(op->lba);
    cmd.count = cpu_to_be16(op->count);
    return cdb_cmd_data(op, &cmd, GET_GLOBAL(op->drive_g->blksize));
}

// Write sectors.
int
cdb_write(struct disk_op_s *op)
{
    struct cdb_rwdata_10 cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.command = CDB_CMD_WRITE_10;
    cmd.lba = cpu_to_be32(op->lba);
    cmd.count = cpu_to_be16(op->count);
    return cdb_cmd_data(op, &cmd, GET_GLOBAL(op->drive_g->blksize));
}
