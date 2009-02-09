// Storage for boot definitions.
#ifndef __BOOT_H
#define __BOOT_H


/****************************************************************
 * Initial Program Load (IPL)
 ****************************************************************/

struct ipl_entry_s {
    u16 type;
    u16 flags;
    u32 vector;
    const char *description;
};

struct ipl_s {
    struct ipl_entry_s bev[8];
    struct ipl_entry_s bcv[8];
    int bevcount, bcvcount;
    int bcv_override;
    u32 bootorder;
    int checkfloppysig;
};

#define IPL_TYPE_FLOPPY      0x01
#define IPL_TYPE_HARDDISK    0x02
#define IPL_TYPE_CDROM       0x03
#define IPL_TYPE_BEV         0x80


/****************************************************************
 * Function defs
 ****************************************************************/

// boot.c
extern struct ipl_s IPL;
void boot_setup();
void add_bev(u16 seg, u16 bev, u16 desc);
void add_bcv(u16 seg, u16 ip, u16 desc);
void add_bcv_hd(int driveid, const char *desc);
void boot_prep();

#endif // __BOOT_H
