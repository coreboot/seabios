// Disk setup and access
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "disk.h" // struct ata_s
#include "biosvar.h" // GET_GLOBAL
#include "cmos.h" // inb_cmos
#include "util.h" // dprintf

struct drives_s Drives VAR16_32;


/****************************************************************
 * Disk geometry translation
 ****************************************************************/

static u8
get_translation(int driveid)
{
    u8 type = GET_GLOBAL(Drives.drives[driveid].type);
    if (! CONFIG_COREBOOT && type == DTYPE_ATA) {
        // Emulators pass in the translation info via nvram.
        u8 ataid = GET_GLOBAL(Drives.drives[driveid].cntl_id);
        u8 channel = ataid / 2;
        u8 translation = inb_cmos(CMOS_BIOS_DISKTRANSFLAG + channel/2);
        translation >>= 2 * (ataid % 4);
        translation &= 0x03;
        return translation;
    }

    // On COREBOOT, use a heuristic to determine translation type.
    u16 heads = GET_GLOBAL(Drives.drives[driveid].pchs.heads);
    u16 cylinders = GET_GLOBAL(Drives.drives[driveid].pchs.cylinders);
    u16 spt = GET_GLOBAL(Drives.drives[driveid].pchs.spt);

    if (cylinders <= 1024 && heads <= 16 && spt <= 63)
        return TRANSLATION_NONE;
    if (cylinders * heads <= 131072)
        return TRANSLATION_LARGE;
    return TRANSLATION_LBA;
}

void
setup_translation(int driveid)
{
    u8 translation = get_translation(driveid);
    SET_GLOBAL(Drives.drives[driveid].translation, translation);

    u8 ataid = GET_GLOBAL(Drives.drives[driveid].cntl_id);
    u8 channel = ataid / 2;
    u8 slave = ataid % 2;
    u16 heads = GET_GLOBAL(Drives.drives[driveid].pchs.heads);
    u16 cylinders = GET_GLOBAL(Drives.drives[driveid].pchs.cylinders);
    u16 spt = GET_GLOBAL(Drives.drives[driveid].pchs.spt);
    u64 sectors = GET_GLOBAL(Drives.drives[driveid].sectors);

    dprintf(1, "ata%d-%d: PCHS=%u/%d/%d translation="
            , channel, slave, cylinders, heads, spt);
    switch (translation) {
    case TRANSLATION_NONE:
        dprintf(1, "none");
        break;
    case TRANSLATION_LBA:
        dprintf(1, "lba");
        spt = 63;
        if (sectors > 63*255*1024) {
            heads = 255;
            cylinders = 1024;
            break;
        }
        u32 sect = (u32)sectors / 63;
        heads = sect / 1024;
        if (heads>128)
            heads = 255;
        else if (heads>64)
            heads = 128;
        else if (heads>32)
            heads = 64;
        else if (heads>16)
            heads = 32;
        else
            heads = 16;
        cylinders = sect / heads;
        break;
    case TRANSLATION_RECHS:
        dprintf(1, "r-echs");
        // Take care not to overflow
        if (heads==16) {
            if (cylinders>61439)
                cylinders=61439;
            heads=15;
            cylinders = (u16)((u32)(cylinders)*16/15);
        }
        // then go through the large bitshift process
    case TRANSLATION_LARGE:
        if (translation == TRANSLATION_LARGE)
            dprintf(1, "large");
        while (cylinders > 1024) {
            cylinders >>= 1;
            heads <<= 1;

            // If we max out the head count
            if (heads > 127)
                break;
        }
        break;
    }
    // clip to 1024 cylinders in lchs
    if (cylinders > 1024)
        cylinders = 1024;
    dprintf(1, " LCHS=%d/%d/%d\n", cylinders, heads, spt);

    SET_GLOBAL(Drives.drives[driveid].lchs.heads, heads);
    SET_GLOBAL(Drives.drives[driveid].lchs.cylinders, cylinders);
    SET_GLOBAL(Drives.drives[driveid].lchs.spt, spt);
}


/****************************************************************
 * Drive mapping
 ****************************************************************/

// Fill in Fixed Disk Parameter Table (located in ebda).
static void
fill_fdpt(int driveid)
{
    if (driveid > 1)
        return;

    u16 nlc   = GET_GLOBAL(Drives.drives[driveid].lchs.cylinders);
    u16 nlh   = GET_GLOBAL(Drives.drives[driveid].lchs.heads);
    u16 nlspt = GET_GLOBAL(Drives.drives[driveid].lchs.spt);

    u16 npc   = GET_GLOBAL(Drives.drives[driveid].pchs.cylinders);
    u16 nph   = GET_GLOBAL(Drives.drives[driveid].pchs.heads);
    u16 npspt = GET_GLOBAL(Drives.drives[driveid].pchs.spt);

    struct fdpt_s *fdpt = &get_ebda_ptr()->fdpt[driveid];
    fdpt->precompensation = 0xffff;
    fdpt->drive_control_byte = 0xc0 | ((nph > 8) << 3);
    fdpt->landing_zone = npc;
    fdpt->cylinders = nlc;
    fdpt->heads = nlh;
    fdpt->sectors = nlspt;

    if (nlc == npc && nlh == nph && nlspt == npspt)
        // no logical CHS mapping used, just physical CHS
        // use Standard Fixed Disk Parameter Table (FDPT)
        return;

    // complies with Phoenix style Translated Fixed Disk Parameter
    // Table (FDPT)
    fdpt->phys_cylinders = npc;
    fdpt->phys_heads = nph;
    fdpt->phys_sectors = npspt;
    fdpt->a0h_signature = 0xa0;

    // Checksum structure.
    fdpt->checksum -= checksum(fdpt, sizeof(*fdpt));

    if (driveid == 0)
        SET_IVT(0x41, get_ebda_seg()
                , offsetof(struct extended_bios_data_area_s, fdpt[0]));
    else
        SET_IVT(0x46, get_ebda_seg()
                , offsetof(struct extended_bios_data_area_s, fdpt[1]));
}

// Map a drive (that was registered via add_bcv_hd)
void
map_hd_drive(int driveid)
{
    // fill hdidmap
    u8 hdcount = GET_BDA(hdcount);
    if (hdcount >= ARRAY_SIZE(Drives.idmap[0]))
        return;
    dprintf(3, "Mapping hd driveid %d to %d\n", driveid, hdcount);
    SET_GLOBAL(Drives.idmap[0][hdcount], driveid);
    SET_BDA(hdcount, hdcount + 1);

    // Fill "fdpt" structure.
    fill_fdpt(hdcount);
}

// Map a cd
void
map_cd_drive(int driveid)
{
    // fill cdidmap
    u8 cdcount = GET_GLOBAL(Drives.cdcount);
    if (cdcount >= ARRAY_SIZE(Drives.idmap[1]))
        return;
    dprintf(3, "Mapping cd driveid %d to %d\n", driveid, cdcount);
    SET_GLOBAL(Drives.idmap[1][cdcount], driveid);
    SET_GLOBAL(Drives.cdcount, cdcount+1);
}


/****************************************************************
 * Setup
 ****************************************************************/

void
drive_setup()
{
    memset(&Drives, 0, sizeof(Drives));
    memset(&Drives.idmap, 0xff, sizeof(Drives.idmap));
}
