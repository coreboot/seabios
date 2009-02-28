// MPTable generation (on emulators)
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2006 Fabrice Bellard
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // dprintf
#include "memmap.h" // bios_table_cur_addr
#include "config.h" // CONFIG_*
#include "mptable.h" // MPTABLE_SIGNATURE

void
mptable_init(void)
{
    if (! CONFIG_MPTABLE)
        return;

    dprintf(3, "init MPTable\n");

    int smp_cpus = smp_probe();
    if (smp_cpus <= 1)
        // Building an mptable on uniprocessor machines confuses some OSes.
        return;

    u32 start = ALIGN(bios_table_cur_addr, 16);
    int length = (sizeof(struct mptable_floating_s)
                  + sizeof(struct mptable_config_s)
                  + sizeof(struct mpt_cpu) * smp_cpus
                  + sizeof(struct mpt_bus)
                  + sizeof(struct mpt_ioapic)
                  + sizeof(struct mpt_intsrc) * 16);
    if (start + length > bios_table_end_addr) {
        dprintf(1, "No room for MPTABLE!\n");
        return;
    }

    /* floating pointer structure */
    struct mptable_floating_s *floating = (void*)start;
    memset(floating, 0, sizeof(*floating));
    struct mptable_config_s *config = (void*)&floating[1];
    floating->signature = MPTABLE_SIGNATURE;
    floating->physaddr = (u32)config;
    floating->length = 1;
    floating->spec_rev = 4;
    floating->checksum = -checksum(floating, sizeof(*floating));

    // Config structure.
    memset(config, 0, sizeof(*config));
    config->signature = MPCONFIG_SIGNATURE;
    config->length = length - sizeof(*floating);
    config->spec = 4;
    memcpy(config->oemid, CONFIG_CPUNAME8, sizeof(config->oemid));
    memcpy(config->productid, "0.1         ", sizeof(config->productid));
    config->entrycount = smp_cpus + 2 + 16;
    config->lapic = BUILD_APIC_ADDR;

    // CPU definitions.
    struct mpt_cpu *cpus = (void*)&config[1];
    int i;
    for (i = 0; i < smp_cpus; i++) {
        struct mpt_cpu *cpu = &cpus[i];
        memset(cpu, 0, sizeof(*cpu));
        cpu->type = MPT_TYPE_CPU;
        cpu->apicid = i;
        cpu->apicver = 0x11;
        /* cpu flags: enabled, bootstrap cpu */
        cpu->cpuflag = (i == 0 ? 3 : 1);
        cpu->cpufeature = 0x600;
        cpu->featureflag = 0x201;
    }

    /* isa bus */
    struct mpt_bus *bus = (void*)&cpus[smp_cpus];
    memset(bus, 0, sizeof(*bus));
    bus->type = MPT_TYPE_BUS;
    memcpy(bus->bustype, "ISA   ", sizeof(bus->bustype));

    /* ioapic */
    u8 ioapic_id = smp_cpus;
    struct mpt_ioapic *ioapic = (void*)&bus[1];
    memset(ioapic, 0, sizeof(*ioapic));
    ioapic->type = MPT_TYPE_IOAPIC;
    ioapic->apicid = ioapic_id;
    ioapic->apicver = 0x11;
    ioapic->flags = 1; // enable
    ioapic->apicaddr = BUILD_IOAPIC_ADDR;

    /* irqs */
    struct mpt_intsrc *intsrcs = (void *)&ioapic[1];
    for(i = 0; i < 16; i++) {
        struct mpt_intsrc *isrc = &intsrcs[i];
        memset(isrc, 0, sizeof(*isrc));
        isrc->type = MPT_TYPE_INTSRC;
        isrc->srcbusirq = i;
        isrc->dstapic = ioapic_id;
        isrc->dstirq = i;
    }

    // Set checksum.
    config->checksum = -checksum(config, config->length);

    dprintf(1, "MP table addr=0x%x MPC table addr=0x%x size=0x%x\n",
            (u32)floating, (u32)config, length);
}
