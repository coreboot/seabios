ACPI_EXTRACT_ALL_CODE ssdp_pcihp_aml

DefinitionBlock ("ssdt-pcihp.aml", "SSDT", 0x01, "BXPC", "BXSSDTPCIHP", 0x1)
{

/****************************************************************
 * PCI hotplug
 ****************************************************************/

    /* Objects supplied by DSDT */
    External (\_SB.PCI0, DeviceObj)
    External (\_SB.PCI0.PCEJ, MethodObj)

    Scope(\_SB.PCI0) {
        /* Bulk generated PCI hotplug devices */
        // Method _EJ0 can be patched by BIOS to EJ0_
        // at runtime, if the slot is detected to not support hotplug.
        // Extract the offset of the address dword and the
        // _EJ0 name to allow this patching.
#define hotplug_slot(slot)                              \
        Device (S##slot) {                              \
           ACPI_EXTRACT_NAME_DWORD_CONST aml_adr_dword  \
           Name (_ADR, 0x##slot##0000)                  \
           ACPI_EXTRACT_METHOD_STRING aml_ej0_name      \
           Method (_EJ0, 1) { Return(PCEJ(0x##slot)) }  \
           Name (_SUN, 0x##slot)                        \
        }

        hotplug_slot(01)
        hotplug_slot(02)
        hotplug_slot(03)
        hotplug_slot(04)
        hotplug_slot(05)
        hotplug_slot(06)
        hotplug_slot(07)
        hotplug_slot(08)
        hotplug_slot(09)
        hotplug_slot(0a)
        hotplug_slot(0b)
        hotplug_slot(0c)
        hotplug_slot(0d)
        hotplug_slot(0e)
        hotplug_slot(0f)
        hotplug_slot(10)
        hotplug_slot(11)
        hotplug_slot(12)
        hotplug_slot(13)
        hotplug_slot(14)
        hotplug_slot(15)
        hotplug_slot(16)
        hotplug_slot(17)
        hotplug_slot(18)
        hotplug_slot(19)
        hotplug_slot(1a)
        hotplug_slot(1b)
        hotplug_slot(1c)
        hotplug_slot(1d)
        hotplug_slot(1e)
        hotplug_slot(1f)
    }
}
