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
    char *description;
};

struct ipl_s {
    struct ipl_entry_s table[8];
    u16 count;
    u32 bootorder;
    u8 checkfloppysig;
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
void printf_bootdev(u16 bootdev);

// post_menu.c
void interactive_bootmenu();

#endif // __BOOT_H
