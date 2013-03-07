ACPI_EXTRACT_ALL_CODE ssdp_misc_aml

DefinitionBlock ("ssdt-misc.aml", "SSDT", 0x01, "BXPC", "BXSSDTSUSP", 0x1)
{

/****************************************************************
 * PCI memory ranges
 ****************************************************************/

    Scope(\) {
       ACPI_EXTRACT_NAME_DWORD_CONST acpi_pci32_start
       Name(P0S, 0x12345678)
       ACPI_EXTRACT_NAME_DWORD_CONST acpi_pci32_end
       Name(P0E, 0x12345678)
       ACPI_EXTRACT_NAME_BYTE_CONST acpi_pci64_valid
       Name(P1V, 0x12)
       ACPI_EXTRACT_NAME_BUFFER8 acpi_pci64_start
       Name(P1S, Buffer() { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 })
       ACPI_EXTRACT_NAME_BUFFER8 acpi_pci64_end
       Name(P1E, Buffer() { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 })
       ACPI_EXTRACT_NAME_BUFFER8 acpi_pci64_length
       Name(P1L, Buffer() { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 })
    }


/****************************************************************
 * Suspend
 ****************************************************************/

    Scope(\) {
    /*
     * S3 (suspend-to-ram), S4 (suspend-to-disk) and S5 (power-off) type codes:
     * must match piix4 emulation.
     */

        ACPI_EXTRACT_NAME_STRING acpi_s3_name
        Name(_S3, Package(0x04) {
            One,  /* PM1a_CNT.SLP_TYP */
            One,  /* PM1b_CNT.SLP_TYP */
            Zero,  /* reserved */
            Zero   /* reserved */
        })
        ACPI_EXTRACT_NAME_STRING acpi_s4_name
        ACPI_EXTRACT_PKG_START acpi_s4_pkg
        Name(_S4, Package(0x04) {
            0x2,  /* PM1a_CNT.SLP_TYP */
            0x2,  /* PM1b_CNT.SLP_TYP */
            Zero,  /* reserved */
            Zero   /* reserved */
        })
        Name(_S5, Package(0x04) {
            Zero,  /* PM1a_CNT.SLP_TYP */
            Zero,  /* PM1b_CNT.SLP_TYP */
            Zero,  /* reserved */
            Zero   /* reserved */
        })
    }
}
