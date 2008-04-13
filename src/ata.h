// Low level ATA disk definitions
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#ifndef __ATA_H
#define __ATA_H

#include "types.h" // u16
#include "atabits.h" // ATA_CB_DH_DEV1

// Function definitions
void ata_reset(int driveid);
int ata_cmd_data(int driveid, u16 command, u32 lba, u16 count, void *far_buffer);
int ata_cmd_packet(int driveid, u8 *cmdbuf, u8 cmdlen
                   , u32 length, void *far_buffer);
int cdrom_read(int driveid, u32 lba, u32 count, void *far_buffer);
int cdrom_read_512(int driveid, u32 lba, u32 count, void *far_buffer);
void hard_drive_setup();

#endif /* __ATA_H */
