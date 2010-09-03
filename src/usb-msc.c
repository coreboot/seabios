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
#include "boot.h" // add_bcv_internal

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

// Low-level usb command transmit function.
int
usb_cmd_data(struct disk_op_s *op, void *cdbcmd, u16 blocksize)
{
    dprintf(16, "usb_cmd_data id=%p write=%d count=%d bs=%d buf=%p\n"
            , op->drive_g, 0, op->count, blocksize, op->buf_fl);
    struct usbdrive_s *udrive_g = container_of(
        op->drive_g, struct usbdrive_s, drive);
    struct usb_pipe *bulkin = GET_GLOBAL(udrive_g->bulkin);
    struct usb_pipe *bulkout = GET_GLOBAL(udrive_g->bulkout);

    // Setup command block wrapper.
    u32 bytes = blocksize * op->count;
    struct cbw_s cbw;
    memset(&cbw, 0, sizeof(cbw));
    cbw.dCBWSignature = CBW_SIGNATURE;
    cbw.dCBWTag = 999; // XXX
    cbw.dCBWDataTransferLength = bytes;
    cbw.bmCBWFlags = USB_DIR_IN; // XXX
    cbw.bCBWLUN = 0; // XXX
    cbw.bCBWCBLength = USB_CDB_SIZE;
    memcpy(cbw.CBWCB, cdbcmd, USB_CDB_SIZE);

    // Transfer cbw to device.
    int ret = usb_send_bulk(bulkout, USB_DIR_OUT
                            , MAKE_FLATPTR(GET_SEG(SS), &cbw), sizeof(cbw));
    if (ret)
        goto fail;

    // Transfer data from device.
    ret = usb_send_bulk(bulkin, USB_DIR_IN, op->buf_fl, bytes);
    if (ret)
        goto fail;

    // Transfer csw info.
    struct csw_s csw;
    ret = usb_send_bulk(bulkin, USB_DIR_IN
                        , MAKE_FLATPTR(GET_SEG(SS), &csw), sizeof(csw));
    if (ret)
        goto fail;

    if (!csw.bCSWStatus)
        return DISK_RET_SUCCESS;
    if (csw.bCSWStatus == 2)
        goto fail;

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
    case CMD_FORMAT:
    case CMD_WRITE:
        return DISK_RET_EWRITEPROTECT;
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
setup_drive_cdrom(struct disk_op_s *op)
{
    op->drive_g->blksize = CDROM_SECTOR_SIZE;
    op->drive_g->sectors = (u64)-1;
    map_cd_drive(op->drive_g);
    return 0;
}

static int
setup_drive_hd(struct disk_op_s *op)
{
    struct cdbres_read_capacity info;
    int ret = cdb_read_capacity(op, &info);
    if (ret)
        return ret;
    // XXX - retry for some timeout?

    u32 blksize = ntohl(info.blksize), sectors = ntohl(info.sectors);
    if (blksize != DISK_SECTOR_SIZE) {
        if (blksize == CDROM_SECTOR_SIZE)
            return setup_drive_cdrom(op);
        dprintf(1, "Unsupported USB MSC block size %d\n", blksize);
        return -1;
    }
    op->drive_g->blksize = blksize;
    op->drive_g->sectors = sectors;
    dprintf(1, "USB MSC blksize=%d sectors=%d\n", blksize, sectors);

    // Setup disk geometry translation.
    setup_translation(op->drive_g);

    // Register with bcv system.
    add_bcv_internal(op->drive_g);

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
    char *desc = malloc_tmphigh(MAXDESCSIZE);
    struct usbdrive_s *udrive_g = malloc_fseg(sizeof(*udrive_g));
    if (!udrive_g || !desc) {
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

    // Validate drive and find block size and sector count.
    struct disk_op_s dop;
    memset(&dop, 0, sizeof(dop));
    dop.drive_g = &udrive_g->drive;
    struct cdbres_inquiry data;
    int ret = cdb_get_inquiry(&dop, &data);
    if (ret)
        goto fail;
    char vendor[sizeof(data.vendor)+1], product[sizeof(data.product)+1];
    char rev[sizeof(data.rev)+1];
    int pdt = data.pdt & 0x1f;
    int removable = !!(data.removable & 0x80);
    dprintf(1, "USB MSC vendor='%s' product='%s' rev='%s'"
            " type=%d removable=%d\n"
            , strtcpy(vendor, data.vendor, sizeof(vendor))
            , strtcpy(product, data.product, sizeof(product))
            , strtcpy(rev, data.rev, sizeof(rev))
            , pdt, removable);
    udrive_g->drive.removable = removable;

    if (pdt == USB_MSC_TYPE_CDROM)
        ret = setup_drive_cdrom(&dop);
    else
        ret = setup_drive_hd(&dop);
    if (ret)
        goto fail;

    snprintf(desc, MAXDESCSIZE, "USB Drive %s %s %s", vendor, product, rev);
    udrive_g->drive.desc = desc;

    return 0;
fail:
    dprintf(1, "Unable to configure USB MSC device.\n");
    free(desc);
    free(udrive_g);
    return -1;
}
