#ifndef _VIRTIO_PCI_H
#define _VIRTIO_PCI_H

#include "x86.h" // inl
#include "biosvar.h" // GET_LOWFLAT

/* A 32-bit r/o bitmask of the features supported by the host */
#define VIRTIO_PCI_HOST_FEATURES        0

/* A 32-bit r/w bitmask of features activated by the guest */
#define VIRTIO_PCI_GUEST_FEATURES       4

/* A 32-bit r/w PFN for the currently selected queue */
#define VIRTIO_PCI_QUEUE_PFN            8

/* A 16-bit r/o queue size for the currently selected queue */
#define VIRTIO_PCI_QUEUE_NUM            12

/* A 16-bit r/w queue selector */
#define VIRTIO_PCI_QUEUE_SEL            14

/* A 16-bit r/w queue notifier */
#define VIRTIO_PCI_QUEUE_NOTIFY         16

/* An 8-bit device status register.  */
#define VIRTIO_PCI_STATUS               18

/* An 8-bit r/o interrupt status register.  Reading the value will return the
 * current contents of the ISR and will also clear it.  This is effectively
 * a read-and-acknowledge. */
#define VIRTIO_PCI_ISR                  19

/* The bit of the ISR which indicates a device configuration change. */
#define VIRTIO_PCI_ISR_CONFIG           0x2

/* The remaining space is defined by each driver as the per-driver
 * configuration space */
#define VIRTIO_PCI_CONFIG               20

/* Virtio ABI version, this must match exactly */
#define VIRTIO_PCI_ABI_VERSION          0

struct vp_device {
    unsigned int ioaddr;
};

static inline u32 vp_get_features(struct vp_device *vp)
{
    return inl(GET_LOWFLAT(vp->ioaddr) + VIRTIO_PCI_HOST_FEATURES);
}

static inline void vp_set_features(struct vp_device *vp, u32 features)
{
    outl(features, GET_LOWFLAT(vp->ioaddr) + VIRTIO_PCI_GUEST_FEATURES);
}

static inline void vp_get(struct vp_device *vp, unsigned offset,
                     void *buf, unsigned len)
{
   int ioaddr = GET_LOWFLAT(vp->ioaddr);
   u8 *ptr = buf;
   unsigned i;

   for (i = 0; i < len; i++)
           ptr[i] = inb(ioaddr + VIRTIO_PCI_CONFIG + offset + i);
}

static inline u8 vp_get_status(struct vp_device *vp)
{
    return inb(GET_LOWFLAT(vp->ioaddr) + VIRTIO_PCI_STATUS);
}

static inline void vp_set_status(struct vp_device *vp, u8 status)
{
   if (status == 0)        /* reset */
           return;
   outb(status, GET_LOWFLAT(vp->ioaddr) + VIRTIO_PCI_STATUS);
}

static inline u8 vp_get_isr(struct vp_device *vp)
{
    return inb(GET_LOWFLAT(vp->ioaddr) + VIRTIO_PCI_ISR);
}

static inline void vp_reset(struct vp_device *vp)
{
   int ioaddr = GET_LOWFLAT(vp->ioaddr);

   outb(0, ioaddr + VIRTIO_PCI_STATUS);
   (void)inb(ioaddr + VIRTIO_PCI_ISR);
}

static inline void vp_notify(struct vp_device *vp, int queue_index)
{
    outw(queue_index, GET_LOWFLAT(vp->ioaddr) + VIRTIO_PCI_QUEUE_NOTIFY);
}

static inline void vp_del_vq(struct vp_device *vp, int queue_index)
{
   int ioaddr = GET_LOWFLAT(vp->ioaddr);

   /* select the queue */
   outw(queue_index, ioaddr + VIRTIO_PCI_QUEUE_SEL);

   /* deactivate the queue */
   outl(0, ioaddr + VIRTIO_PCI_QUEUE_PFN);
}

struct pci_device;
struct vring_virtqueue;
void vp_init_simple(struct vp_device *vp, struct pci_device *pci);
int vp_find_vq(struct vp_device *vp, int queue_index,
               struct vring_virtqueue **p_vq);
#endif /* _VIRTIO_PCI_H_ */
