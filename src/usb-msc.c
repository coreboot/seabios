// Code for handling USB Mass Storage Controller devices.
//
// Copyright (C) 2010  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // dprintf
#include "config.h" // CONFIG_USB_MSC
#include "usb-msc.h" // usb_msc_init
#include "usb.h" // struct usb_s
#include "biosvar.h" // GET_GLOBAL
#include "blockcmd.h" // cdb_read
#include "disk.h" // DTYPE_USB
#include "boot.h" // boot_add_hd

struct usbdrive_s {
    struct drive_s drive;
    struct usb_pipe *bulkin, *bulkout;
};


/****************************************************************
 * Bulk-only drive command processing
 ****************************************************************/

#define USB_CDB_SIZE 12

#define CBW_SIGNATURE 0x43425355 // USBC

struct cbw_s {
    u32 dCBWSignature;
    u32 dCBWTag;
    u32 dCBWDataTransferLength;
    u8 bmCBWFlags;
    u8 bCBWLUN;
    u8 bCBWCBLength;
    u8 CBWCB[16];
} PACKED;

#define CSW_SIGNATURE 0x53425355 // USBS

struct csw_s {
    u32 dCSWSignature;
    u32 dCSWTag;
    u32 dCSWDataResidue;
    u8 bCSWStatus;
} PACKED;

static int
usb_msc_send(struct usbdrive_s *udrive_g, int dir, void *buf, u32 bytes)
{
    struct usb_pipe *pipe;
    if (dir == USB_DIR_OUT)
        pipe = GET_GLOBAL(udrive_g->bulkout);
    else
        pipe = GET_GLOBAL(udrive_g->bulkin);
    return usb_send_bulk(pipe, dir, buf, bytes);
}

// Low-level usb command transmit function.
int
usb_cmd_data(struct disk_op_s *op, void *cdbcmd, u16 blocksize)
{
    if (!CONFIG_USB_MSC)
        return 0;

    dprintf(16, "usb_cmd_data id=%p write=%d count=%d bs=%d buf=%p\n"
            , op->drive_g, 0, op->count, blocksize, op->buf_fl);
    struct usbdrive_s *udrive_g = container_of(
        op->drive_g, struct usbdrive_s, drive);

    // Setup command block wrapper.
    u32 bytes = blocksize * op->count;
    struct cbw_s cbw;
    memset(&cbw, 0, sizeof(cbw));
    memcpy(cbw.CBWCB, cdbcmd, USB_CDB_SIZE);
    cbw.dCBWSignature = CBW_SIGNATURE;
    cbw.dCBWTag = 999; // XXX
    cbw.dCBWDataTransferLength = bytes;
    cbw.bmCBWFlags = (cbw.CBWCB[0] == CDB_CMD_WRITE_10) ? USB_DIR_OUT : USB_DIR_IN;
    cbw.bCBWLUN = 0; // XXX
    cbw.bCBWCBLength = USB_CDB_SIZE;

    // Transfer cbw to device.
    int ret = usb_msc_send(udrive_g, USB_DIR_OUT
                            , MAKE_FLATPTR(GET_SEG(SS), &cbw), sizeof(cbw));
    if (ret)
        goto fail;

    // Transfer data to/from device.
    if (bytes) {
        ret = usb_msc_send(udrive_g, cbw.bmCBWFlags, op->buf_fl, bytes);
        if (ret)
            goto fail;
    }

    // Transfer csw info.
    struct csw_s csw;
    ret = usb_msc_send(udrive_g, USB_DIR_IN
                        , MAKE_FLATPTR(GET_SEG(SS), &csw), sizeof(csw));
    if (ret)
        goto fail;

    if (!csw.bCSWStatus)
        return DISK_RET_SUCCESS;
    if (csw.bCSWStatus == 2)
        goto fail;

    if (blocksize)
        op->count -= csw.dCSWDataResidue / blocksize;
    return DISK_RET_EBADTRACK;

fail:
    // XXX - reset connection
    dprintf(1, "USB transmission failed\n");
    op->count = 0;
    return DISK_RET_EBADTRACK;
}


/****************************************************************
 * Drive ops
 ****************************************************************/

// 16bit command demuxer for ATAPI cdroms.
int
process_usb_op(struct disk_op_s *op)
{
    if (!CONFIG_USB_MSC)
        return 0;
    switch (op->command) {
    case CMD_READ:
        return cdb_read(op);
    case CMD_WRITE:
        return cdb_write(op);
    case CMD_FORMAT:
    case CMD_RESET:
    case CMD_ISREADY:
    case CMD_VERIFY:
    case CMD_SEEK:
        return DISK_RET_SUCCESS;
    default:
        op->count = 0;
        return DISK_RET_EPARAM;
    }
}


/****************************************************************
 * Setup
 ****************************************************************/

static int
setup_drive_cdrom(struct drive_s *drive, char *desc)
{
    drive->sectors = (u64)-1;
    struct usb_pipe *pipe = container_of(
        drive, struct usbdrive_s, drive)->bulkout;
    int prio = bootprio_find_usb(pipe->cntl->pci, pipe->path);
    boot_add_cd(drive, desc, prio);
    return 0;
}

static int
setup_drive_hd(struct drive_s *drive, char *desc)
{
    if (drive->blksize != DISK_SECTOR_SIZE) {
        dprintf(1, "Unsupported USB MSC block size %d\n", drive->blksize);
        return -1;
    }

    // Register with bcv system.
    struct usb_pipe *pipe = container_of(
        drive, struct usbdrive_s, drive)->bulkout;
    int prio = bootprio_find_usb(pipe->cntl->pci, pipe->path);
    boot_add_hd(drive, desc, prio);
    return 0;
}

// Configure a usb msc device.
int
usb_msc_init(struct usb_pipe *pipe
             , struct usb_interface_descriptor *iface, int imax)
{
    if (!CONFIG_USB_MSC)
        return -1;

    // Verify right kind of device
    if ((iface->bInterfaceSubClass != US_SC_SCSI &&
	 iface->bInterfaceSubClass != US_SC_ATAPI_8070 &&
	 iface->bInterfaceSubClass != US_SC_ATAPI_8020)
        || iface->bInterfaceProtocol != US_PR_BULK) {
        dprintf(1, "Unsupported MSC USB device (subclass=%02x proto=%02x)\n"
                , iface->bInterfaceSubClass, iface->bInterfaceProtocol);
        return -1;
    }

    // Allocate drive structure.
    struct usbdrive_s *udrive_g = malloc_fseg(sizeof(*udrive_g));
    if (!udrive_g) {
        warn_noalloc();
        goto fail;
    }
    memset(udrive_g, 0, sizeof(*udrive_g));
    udrive_g->drive.type = DTYPE_USB;

    // Find bulk in and bulk out endpoints.
    struct usb_endpoint_descriptor *indesc = findEndPointDesc(
        iface, imax, USB_ENDPOINT_XFER_BULK, USB_DIR_IN);
    struct usb_endpoint_descriptor *outdesc = findEndPointDesc(
        iface, imax, USB_ENDPOINT_XFER_BULK, USB_DIR_OUT);
    if (!indesc || !outdesc)
        goto fail;
    udrive_g->bulkin = alloc_bulk_pipe(pipe, indesc);
    udrive_g->bulkout = alloc_bulk_pipe(pipe, outdesc);
    if (!udrive_g->bulkin || !udrive_g->bulkout)
        goto fail;

    int ret, pdt;
    char *desc = NULL;
    ret = scsi_init_drive(&udrive_g->drive, "USB MSC", &pdt, &desc);
    if (ret)
        goto fail;

    if (pdt == SCSI_TYPE_CDROM)
        ret = setup_drive_cdrom(&udrive_g->drive, desc);
    else
        ret = setup_drive_hd(&udrive_g->drive, desc);

    if (ret)
        goto fail;

    return 0;
fail:
    dprintf(1, "Unable to configure USB MSC device.\n");
    free(udrive_g);
    return -1;
}
