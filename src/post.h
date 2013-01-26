#ifndef __POST_H
#define __POST_H

void interface_init(void);
void device_hardware_setup(void);
void prepareboot(void);
void startBoot(void);
void reloc_preinit(void *f, void *arg);

#endif // __POST_H
