/* PCI CRS (current resources) definition. */
Scope(\_SB.PCI0) {

    Name(CRES, ResourceTemplate() {
        WordBusNumber(ResourceProducer, MinFixed, MaxFixed, PosDecode,
            0x0000,             // Address Space Granularity
            0x0000,             // Address Range Minimum
            0x00FF,             // Address Range Maximum
            0x0000,             // Address Translation Offset
            0x0100,             // Address Length
            ,, )
        IO(Decode16,
            0x0CF8,             // Address Range Minimum
            0x0CF8,             // Address Range Maximum
            0x01,               // Address Alignment
            0x08,               // Address Length
            )
        WordIO(ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
            0x0000,             // Address Space Granularity
            0x0000,             // Address Range Minimum
            0x0CF7,             // Address Range Maximum
            0x0000,             // Address Translation Offset
            0x0CF8,             // Address Length
            ,, , TypeStatic)
        WordIO(ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
            0x0000,             // Address Space Granularity
            0x0D00,             // Address Range Minimum
            0xFFFF,             // Address Range Maximum
            0x0000,             // Address Translation Offset
            0xF300,             // Address Length
            ,, , TypeStatic)
        DWordMemory(ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
            0x00000000,         // Address Space Granularity
            0x000A0000,         // Address Range Minimum
            0x000BFFFF,         // Address Range Maximum
            0x00000000,         // Address Translation Offset
            0x00020000,         // Address Length
            ,, , AddressRangeMemory, TypeStatic)
        DWordMemory(ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
            0x00000000,         // Address Space Granularity
            0xE0000000,         // Address Range Minimum
            0xFEBFFFFF,         // Address Range Maximum
            0x00000000,         // Address Translation Offset
            0x1EC00000,         // Address Length
            ,, PW32, AddressRangeMemory, TypeStatic)
    })

    Name(CR64, ResourceTemplate() {
        QWordMemory(ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
            0x00000000,          // Address Space Granularity
            0x8000000000,        // Address Range Minimum
            0xFFFFFFFFFF,        // Address Range Maximum
            0x00000000,          // Address Translation Offset
            0x8000000000,        // Address Length
            ,, PW64, AddressRangeMemory, TypeStatic)
    })

    Method(_CRS, 0) {
        /* see see acpi.h, struct bfld */
        External(BDAT, OpRegionObj)
        Field(BDAT, QWordAcc, NoLock, Preserve) {
            P0S, 64,
            P0E, 64,
            P0L, 64,
            P1S, 64,
            P1E, 64,
            P1L, 64,
        }
        Field(BDAT, DWordAcc, NoLock, Preserve) {
            P0SL, 32,
            P0SH, 32,
            P0EL, 32,
            P0EH, 32,
            P0LL, 32,
            P0LH, 32,
            P1SL, 32,
            P1SH, 32,
            P1EL, 32,
            P1EH, 32,
            P1LL, 32,
            P1LH, 32,
        }

        /* fixup 32bit pci io window */
        CreateDWordField(CRES, \_SB.PCI0.PW32._MIN, PS32)
        CreateDWordField(CRES, \_SB.PCI0.PW32._MAX, PE32)
        CreateDWordField(CRES, \_SB.PCI0.PW32._LEN, PL32)
        Store(P0SL, PS32)
        Store(P0EL, PE32)
        Store(P0LL, PL32)

        If (LAnd(LEqual(P1SL, 0x00), LEqual(P1SH, 0x00))) {
            Return (CRES)
        } Else {
            /* fixup 64bit pci io window */
            CreateQWordField(CR64, \_SB.PCI0.PW64._MIN, PS64)
            CreateQWordField(CR64, \_SB.PCI0.PW64._MAX, PE64)
            CreateQWordField(CR64, \_SB.PCI0.PW64._LEN, PL64)
            Store(P1S, PS64)
            Store(P1E, PE64)
            Store(P1L, PL64)
            /* add window and return result */
            ConcatenateResTemplate(CRES, CR64, Local0)
            Return (Local0)
        }
    }
}
