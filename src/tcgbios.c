//  Implementation of the TCG BIOS extension according to the specification
//  described in specs found at
//  http://www.trustedcomputinggroup.org/resources/pc_client_work_group_specific_implementation_specification_for_conventional_bios
//
//  Copyright (C) 2006-2011, 2014, 2015 IBM Corporation
//
//  Authors:
//      Stefan Berger <stefanb@linux.vnet.ibm.com>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "bregs.h" // struct bregs
#include "byteorder.h" // cpu_to_*
#include "config.h" // CONFIG_TCGBIOS
#include "farptr.h" // MAKE_FLATPTR
#include "fw/paravirt.h" // runningOnXen
#include "hw/tpm_drivers.h" // tpm_drivers[]
#include "output.h" // dprintf
#include "sha1.h" // sha1
#include "std/acpi.h"  // RSDP_SIGNATURE, rsdt_descriptor
#include "std/smbios.h" // struct smbios_entry_point
#include "std/tcg.h" // TCG_PC_LOGOVERFLOW
#include "string.h" // checksum
#include "tcgbios.h"// tpm_*, prototypes
#include "util.h" // printf, get_keystroke
#include "stacks.h" // wait_threads, reset

static const u8 Startup_ST_CLEAR[] = { 0x00, TPM_ST_CLEAR };
static const u8 Startup_ST_STATE[] = { 0x00, TPM_ST_STATE };

static const u8 PhysicalPresence_CMD_ENABLE[]  = { 0x00, 0x20 };
static const u8 PhysicalPresence_CMD_DISABLE[] = { 0x01, 0x00 };
static const u8 PhysicalPresence_PRESENT[]     = { 0x00, 0x08 };
static const u8 PhysicalPresence_NOT_PRESENT_LOCK[] = { 0x00, 0x14 };

static const u8 CommandFlag_FALSE[1] = { 0x00 };
static const u8 CommandFlag_TRUE[1]  = { 0x01 };

static const u8 GetCapability_Permanent_Flags[] = {
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x01, 0x08
};

static const u8 GetCapability_STClear_Flags[] = {
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x01, 0x09
};

static const u8 GetCapability_OwnerAuth[] = {
    0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x01, 0x11
};

static const u8 GetCapability_Timeouts[] = {
    0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x01, 0x15
};

static const u8 GetCapability_Durations[] = {
    0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x01, 0x20
};


/****************************************************************
 * TPM state tracking
 ****************************************************************/

typedef struct {
    struct tcpa_descriptor_rev2 *tcpa;

    /* length of the TCPA log buffer */
    u32           log_area_minimum_length;

    /* start address of TCPA log buffer */
    u8 *          log_area_start_address;

    /* number of log entries written */
    u32           entry_count;

    /* address to write next log entry to */
    u8 *          log_area_next_entry;

    /* address of last entry written (need for TCG_StatusCheck) */
    u8 *          log_area_last_entry;
} tpm_state_t;

tpm_state_t tpm_state VARLOW;

typedef u8 tpm_ppi_code;


/****************************************************************
 * ACPI TCPA table interface
 ****************************************************************/

static struct tcpa_descriptor_rev2 *
find_tcpa_by_rsdp(struct rsdp_descriptor *rsdp)
{
    u32 ctr = 0;
    struct tcpa_descriptor_rev2 *tcpa = NULL;
    struct rsdt_descriptor *rsdt;
    u32 length;
    u16 off;

    rsdt   = (struct rsdt_descriptor *)rsdp->rsdt_physical_address;
    if (!rsdt)
        return NULL;

    length = rsdt->length;
    off = offsetof(struct rsdt_descriptor, entry);

    while ((off + sizeof(rsdt->entry[0])) <= length) {
        /* try all pointers to structures */
        tcpa = (struct tcpa_descriptor_rev2 *)(int)rsdt->entry[ctr];

        /* valid TCPA ACPI table ? */
        if (tcpa->signature == TCPA_SIGNATURE &&
            checksum((u8 *)tcpa, tcpa->length) == 0)
            break;

        tcpa = NULL;
        off += sizeof(rsdt->entry[0]);
        ctr++;
    }

    /* cache it */
    if (tcpa)
        tpm_state.tcpa = tcpa;

    return tcpa;
}

static struct tcpa_descriptor_rev2 *
find_tcpa_table(void)
{
    struct tcpa_descriptor_rev2 *tcpa = NULL;
    struct rsdp_descriptor *rsdp = RsdpAddr;

    if (tpm_state.tcpa)
        return tpm_state.tcpa;

    if (rsdp)
        tcpa = find_tcpa_by_rsdp(rsdp);

    if (!rsdp)
        dprintf(DEBUG_tcg,
                "TCGBIOS: RSDP was NOT found! -- Disabling interface.\n");
    else if (!tcpa)
        dprintf(DEBUG_tcg, "TCGBIOS: TCPA ACPI was NOT found!\n");

    return tcpa;
}

static u8 *
get_lasa_base_ptr(u32 *log_area_minimum_length)
{
    u8 *log_area_start_address = 0;
    struct tcpa_descriptor_rev2 *tcpa = find_tcpa_table();

    if (tcpa) {
        log_area_start_address = (u8 *)(long)tcpa->log_area_start_address;
        if (log_area_minimum_length)
            *log_area_minimum_length = tcpa->log_area_minimum_length;
    }

    return log_area_start_address;
}

/* clear the ACPI log */
static void
reset_acpi_log(void)
{
    tpm_state.log_area_start_address =
        get_lasa_base_ptr(&tpm_state.log_area_minimum_length);

    if (tpm_state.log_area_start_address)
        memset(tpm_state.log_area_start_address, 0,
               tpm_state.log_area_minimum_length);

    tpm_state.log_area_next_entry = tpm_state.log_area_start_address;
    tpm_state.log_area_last_entry = NULL;
    tpm_state.entry_count = 0;
}

/*
 * Extend the ACPI log with the given entry by copying the
 * entry data into the log.
 * Input
 *  pcpes : Pointer to the event 'header' to be copied into the log
 *  event : Pointer to the event 'body' to be copied into the log
 *
 * Output:
 *  Returns an error code in case of faiure, 0 in case of success
 */
static u32
tpm_log_event(struct pcpes *pcpes, const void *event)
{
    dprintf(DEBUG_tcg, "TCGBIOS: LASA = %p, next entry = %p\n",
            tpm_state.log_area_start_address, tpm_state.log_area_next_entry);

    if (tpm_state.log_area_next_entry == NULL)
        return TCG_PC_LOGOVERFLOW;

    u32 size = sizeof(*pcpes) + pcpes->eventdatasize;

    if ((tpm_state.log_area_next_entry + size - tpm_state.log_area_start_address) >
         tpm_state.log_area_minimum_length) {
        dprintf(DEBUG_tcg, "TCGBIOS: LOG OVERFLOW: size = %d\n", size);
        return TCG_PC_LOGOVERFLOW;
    }

    memcpy(tpm_state.log_area_next_entry, pcpes, sizeof(*pcpes));
    memcpy(tpm_state.log_area_next_entry + sizeof(*pcpes),
           event, pcpes->eventdatasize);

    tpm_state.log_area_last_entry = tpm_state.log_area_next_entry;
    tpm_state.log_area_next_entry += size;
    tpm_state.entry_count++;

    return 0;
}


/****************************************************************
 * Helper functions
 ****************************************************************/

u8 TPM_working VARLOW;

int
tpm_is_working(void)
{
    return CONFIG_TCGBIOS && TPM_working;
}

/*
 * Send a TPM command with the given ordinal. Append the given buffer
 * containing all data in network byte order to the command (this is
 * the custom part per command) and expect a response of the given size.
 * If a buffer is provided, the response will be copied into it.
 */
static u32
build_and_send_cmd(u8 locty, u32 ordinal, const u8 *append, u32 append_size,
                   u8 *resbuffer, u32 return_size, u32 *returnCode,
                   enum tpmDurationType to_t)
{
    struct {
        struct tpm_req_header trqh;
        u8 cmd[20];
    } PACKED req = {
        .trqh.tag = cpu_to_be16(TPM_TAG_RQU_CMD),
        .trqh.totlen = cpu_to_be32(sizeof(req.trqh) + append_size),
        .trqh.ordinal = cpu_to_be32(ordinal),
    };
    u8 obuffer[64];
    struct tpm_rsp_header *trsh = (struct tpm_rsp_header *)obuffer;
    u32 obuffer_len = sizeof(obuffer);
    memset(obuffer, 0x0, sizeof(obuffer));

    if (return_size > sizeof(obuffer) || append_size > sizeof(req.cmd)) {
        warn_internalerror();
        return TCG_FIRMWARE_ERROR;
    }
    if (append_size)
        memcpy(req.cmd, append, append_size);

    u32 rc = tpmhw_transmit(locty, &req.trqh, obuffer, &obuffer_len, to_t);
    if (rc)
        return rc;

    *returnCode = be32_to_cpu(trsh->errcode);

    if (resbuffer)
        memcpy(resbuffer, trsh, return_size);

    return 0;
}

static void
tpm_set_failure(void)
{
    u32 returnCode;

    /* we will try to deactivate the TPM now - ignoring all errors */
    build_and_send_cmd(0, TPM_ORD_PhysicalPresence,
                       PhysicalPresence_CMD_ENABLE,
                       sizeof(PhysicalPresence_CMD_ENABLE),
                       NULL, 0, &returnCode,
                       TPM_DURATION_TYPE_SHORT);

    build_and_send_cmd(0, TPM_ORD_PhysicalPresence,
                       PhysicalPresence_PRESENT,
                       sizeof(PhysicalPresence_PRESENT),
                       NULL, 0, &returnCode,
                       TPM_DURATION_TYPE_SHORT);

    build_and_send_cmd(0, TPM_ORD_SetTempDeactivated,
                       NULL, 0, NULL, 0, &returnCode,
                       TPM_DURATION_TYPE_SHORT);

    TPM_working = 0;
}

static u32
determine_timeouts(void)
{
    u32 rc;
    u32 returnCode;
    struct tpm_res_getcap_timeouts timeouts;
    struct tpm_res_getcap_durations durations;
    u32 i;

    rc = build_and_send_cmd(0, TPM_ORD_GetCapability,
                            GetCapability_Timeouts,
                            sizeof(GetCapability_Timeouts),
                            (u8 *)&timeouts, sizeof(timeouts),
                            &returnCode, TPM_DURATION_TYPE_SHORT);

    dprintf(DEBUG_tcg, "TCGBIOS: Return code from TPM_GetCapability(Timeouts)"
            " = 0x%08x\n", returnCode);

    if (rc || returnCode)
        goto err_exit;

    rc = build_and_send_cmd(0, TPM_ORD_GetCapability,
                            GetCapability_Durations,
                            sizeof(GetCapability_Durations),
                            (u8 *)&durations, sizeof(durations),
                            &returnCode, TPM_DURATION_TYPE_SHORT);

    dprintf(DEBUG_tcg, "TCGBIOS: Return code from TPM_GetCapability(Durations)"
            " = 0x%08x\n", returnCode);

    if (rc || returnCode)
        goto err_exit;

    for (i = 0; i < 3; i++)
        durations.durations[i] = be32_to_cpu(durations.durations[i]);

    for (i = 0; i < 4; i++)
        timeouts.timeouts[i] = be32_to_cpu(timeouts.timeouts[i]);

    dprintf(DEBUG_tcg, "TCGBIOS: timeouts: %u %u %u %u\n",
            timeouts.timeouts[0],
            timeouts.timeouts[1],
            timeouts.timeouts[2],
            timeouts.timeouts[3]);

    dprintf(DEBUG_tcg, "TCGBIOS: durations: %u %u %u\n",
            durations.durations[0],
            durations.durations[1],
            durations.durations[2]);

    tpmhw_set_timeouts(timeouts.timeouts, durations.durations);

    return 0;

err_exit:
    dprintf(DEBUG_tcg, "TCGBIOS: TPM malfunctioning (line %d).\n", __LINE__);

    tpm_set_failure();
    if (rc)
        return rc;
    return TCG_TCG_COMMAND_ERROR;
}

static u32
tpm_log_extend_event(struct pcpes *pcpes, const void *event)
{
    if (!tpm_is_working())
        return TCG_GENERAL_ERROR;

    if (pcpes->pcrindex >= 24)
        return TCG_INVALID_INPUT_PARA;

    struct tpm_req_extend tre = {
        .hdr.tag     = cpu_to_be16(TPM_TAG_RQU_CMD),
        .hdr.totlen  = cpu_to_be32(sizeof(tre)),
        .hdr.ordinal = cpu_to_be32(TPM_ORD_Extend),
        .pcrindex    = cpu_to_be32(pcpes->pcrindex),
    };
    memcpy(tre.digest, pcpes->digest, sizeof(tre.digest));

    struct tpm_rsp_extend rsp;
    u32 resp_length = sizeof(rsp);
    u32 rc = tpmhw_transmit(0, &tre.hdr, &rsp, &resp_length,
                            TPM_DURATION_TYPE_SHORT);
    if (rc || resp_length != sizeof(rsp)) {
        tpm_set_failure();
        return rc;
    }

    rc = tpm_log_event(pcpes, event);
    if (rc)
        tpm_set_failure();
    return rc;
}

static void
tpm_fill_hash(struct pcpes *pcpes, const void *hashdata, u32 hashdata_length)
{
    if (hashdata)
        sha1(hashdata, hashdata_length, pcpes->digest);
}

/*
 * Add a measurement to the log; the data at data_seg:data/length are
 * appended to the TCG_PCClientPCREventStruct
 *
 * Input parameters:
 *  pcrindex   : which PCR to extend
 *  event_type : type of event; specs section on 'Event Types'
 *  event       : pointer to info (e.g., string) to be added to log as-is
 *  event_length: length of the event
 *  hashdata    : pointer to the data to be hashed
 *  hashdata_length: length of the data to be hashed
 */
static u32
tpm_add_measurement_to_log(u32 pcrindex, u32 event_type,
                           const char *event, u32 event_length,
                           const u8 *hashdata, u32 hashdata_length)
{
    struct pcpes pcpes = {
        .pcrindex = pcrindex,
        .eventtype = event_type,
        .eventdatasize = event_length,
    };
    tpm_fill_hash(&pcpes, hashdata, hashdata_length);
    return tpm_log_extend_event(&pcpes, event);
}


/****************************************************************
 * Setup and Measurements
 ****************************************************************/

// Add an EV_ACTION measurement to the list of measurements
static u32
tpm_add_action(u32 pcrIndex, const char *string)
{
    u32 len = strlen(string);
    return tpm_add_measurement_to_log(pcrIndex, EV_ACTION,
                                      string, len, (u8 *)string, len);
}

/*
 * Add event separators for PCRs 0 to 7; specs on 'Measuring Boot Events'
 */
static u32
tpm_add_event_separators(void)
{
    u32 rc;
    u32 pcrIndex = 0;

    if (!tpm_is_working())
        return TCG_GENERAL_ERROR;

    static const u8 evt_separator[] = {0xff,0xff,0xff,0xff};
    while (pcrIndex <= 7) {
        rc = tpm_add_measurement_to_log(pcrIndex, EV_SEPARATOR,
                                        NULL, 0,
                                        (u8 *)evt_separator,
                                        sizeof(evt_separator));
        if (rc)
            break;
        pcrIndex ++;
    }

    return rc;
}

static u32
tpm_smbios_measure(void)
{
    if (!tpm_is_working())
        return TCG_GENERAL_ERROR;

    u32 rc;
    struct pcctes pcctes = {
        .eventid = 1,
        .eventdatasize = SHA1_BUFSIZE,
    };
    struct smbios_entry_point *sep = SMBiosAddr;

    dprintf(DEBUG_tcg, "TCGBIOS: SMBIOS at %p\n", sep);

    if (!sep)
        return 0;

    rc = sha1((const u8 *)sep->structure_table_address,
              sep->structure_table_length, pcctes.digest);
    if (rc)
        return rc;

    return tpm_add_measurement_to_log(1,
                                      EV_EVENT_TAG,
                                      (const char *)&pcctes, sizeof(pcctes),
                                      (u8 *)&pcctes, sizeof(pcctes));
}

static u32
tpm_startup(void)
{
    u32 rc;
    u32 returnCode;

    if (!tpm_is_working())
        return TCG_GENERAL_ERROR;

    dprintf(DEBUG_tcg, "TCGBIOS: Starting with TPM_Startup(ST_CLEAR)\n");
    rc = build_and_send_cmd(0, TPM_ORD_Startup,
                            Startup_ST_CLEAR, sizeof(Startup_ST_CLEAR),
                            NULL, 0, &returnCode, TPM_DURATION_TYPE_SHORT);

    dprintf(DEBUG_tcg, "Return code from TPM_Startup = 0x%08x\n",
            returnCode);

    if (CONFIG_COREBOOT) {
        /* with other firmware on the system the TPM may already have been
         * initialized
         */
        if (returnCode == TPM_INVALID_POSTINIT)
            returnCode = 0;
    }

    if (rc || returnCode)
        goto err_exit;

    rc = determine_timeouts();
    if (rc)
        goto err_exit;

    rc = build_and_send_cmd(0, TPM_ORD_SelfTestFull, NULL, 0,
                            NULL, 0, &returnCode, TPM_DURATION_TYPE_LONG);

    dprintf(DEBUG_tcg, "Return code from TPM_SelfTestFull = 0x%08x\n",
            returnCode);

    if (rc || returnCode)
        goto err_exit;

    rc = build_and_send_cmd(3, TSC_ORD_ResetEstablishmentBit, NULL, 0,
                            NULL, 0, &returnCode, TPM_DURATION_TYPE_SHORT);

    dprintf(DEBUG_tcg, "Return code from TSC_ResetEstablishmentBit = 0x%08x\n",
            returnCode);

    if (rc || (returnCode != 0 && returnCode != TPM_BAD_LOCALITY))
        goto err_exit;

    rc = tpm_smbios_measure();
    if (rc)
        goto err_exit;

    rc = tpm_add_action(2, "Start Option ROM Scan");
    if (rc)
        goto err_exit;

    return 0;

err_exit:
    dprintf(DEBUG_tcg, "TCGBIOS: TPM malfunctioning (line %d).\n", __LINE__);

    tpm_set_failure();
    if (rc)
        return rc;
    return TCG_TCG_COMMAND_ERROR;
}

/*
   initialize the TCPA ACPI subsystem; find the ACPI tables and determine
   where the TCPA table is.
 */
static void
tpm_acpi_init(void)
{
    int ret = tpmhw_probe();
    if (ret)
        return;
    TPM_working = 1;

    reset_acpi_log();
}

void
tpm_setup(void)
{
    if (!CONFIG_TCGBIOS)
        return;

    tpm_acpi_init();
    if (runningOnXen())
        return;

    tpm_startup();
}

void
tpm_prepboot(void)
{
    u32 rc;
    u32 returnCode;

    if (!tpm_is_working())
        return;

    rc = build_and_send_cmd(0, TPM_ORD_PhysicalPresence,
                            PhysicalPresence_CMD_ENABLE,
                            sizeof(PhysicalPresence_CMD_ENABLE),
                            NULL, 0, &returnCode, TPM_DURATION_TYPE_SHORT);
    if (rc || returnCode)
        goto err_exit;

    rc = build_and_send_cmd(0, TPM_ORD_PhysicalPresence,
                            PhysicalPresence_NOT_PRESENT_LOCK,
                            sizeof(PhysicalPresence_NOT_PRESENT_LOCK),
                            NULL, 0, &returnCode, TPM_DURATION_TYPE_SHORT);
    if (rc || returnCode)
        goto err_exit;

    rc = tpm_add_action(4, "Calling INT 19h");
    if (rc)
        goto err_exit;

    rc = tpm_add_event_separators();
    if (rc)
        goto err_exit;

    return;

err_exit:
    dprintf(DEBUG_tcg, "TCGBIOS: TPM malfunctioning (line %d).\n", __LINE__);

    tpm_set_failure();
}

/*
 * Add measurement to the log about an option rom
 */
u32
tpm_option_rom(const void *addr, u32 len)
{
    if (!tpm_is_working())
        return TCG_GENERAL_ERROR;

    u32 rc;
    struct pcctes_romex pcctes = {
        .eventid = 7,
        .eventdatasize = sizeof(u16) + sizeof(u16) + SHA1_BUFSIZE,
    };

    rc = sha1((const u8 *)addr, len, pcctes.digest);
    if (rc)
        return rc;

    return tpm_add_measurement_to_log(2,
                                      EV_EVENT_TAG,
                                      (const char *)&pcctes, sizeof(pcctes),
                                      (u8 *)&pcctes, sizeof(pcctes));
}

u32
tpm_add_bcv(u32 bootdrv, const u8 *addr, u32 length)
{
    if (!tpm_is_working())
        return TCG_GENERAL_ERROR;

    if (length < 0x200)
        return TCG_INVALID_INPUT_PARA;

    const char *string = "Booting BCV device 00h (Floppy)";
    if (bootdrv == 0x80)
        string = "Booting BCV device 80h (HDD)";
    u32 rc = tpm_add_action(4, string);
    if (rc)
        return rc;

    /* specs: see section 'Hard Disk Device or Hard Disk-Like Devices' */
    /* equivalent to: dd if=/dev/hda ibs=1 count=440 | sha1sum */
    string = "MBR";
    rc = tpm_add_measurement_to_log(4, EV_IPL,
                                    string, strlen(string),
                                    addr, 0x1b8);
    if (rc)
        return rc;

    /* equivalent to: dd if=/dev/hda ibs=1 count=72 skip=440 | sha1sum */
    string = "MBR PARTITION_TABLE";
    return tpm_add_measurement_to_log(5, EV_IPL_PARTITION_DATA,
                                      string, strlen(string),
                                      addr + 0x1b8, 0x48);
}

u32
tpm_add_cdrom(u32 bootdrv, const u8 *addr, u32 length)
{
    if (!tpm_is_working())
        return TCG_GENERAL_ERROR;

    u32 rc = tpm_add_action(4, "Booting from CD ROM device");
    if (rc)
        return rc;

    /* specs: see section 'El Torito' */
    const char *string = "EL TORITO IPL";
    return tpm_add_measurement_to_log(4, EV_IPL,
                                      string, strlen(string),
                                      addr, length);
}

u32
tpm_add_cdrom_catalog(const u8 *addr, u32 length)
{
    if (!tpm_is_working())
        return TCG_GENERAL_ERROR;

    u32 rc = tpm_add_action(4, "Booting from CD ROM device");
    if (rc)
        return rc;

    /* specs: see section 'El Torito' */
    const char *string = "BOOT CATALOG";
    return tpm_add_measurement_to_log(5, EV_IPL_PARTITION_DATA,
                                      string, strlen(string),
                                      addr, length);
}

void
tpm_s3_resume(void)
{
    u32 rc;
    u32 returnCode;

    if (!tpm_is_working())
        return;

    dprintf(DEBUG_tcg, "TCGBIOS: Resuming with TPM_Startup(ST_STATE)\n");

    rc = build_and_send_cmd(0, TPM_ORD_Startup,
                            Startup_ST_STATE, sizeof(Startup_ST_STATE),
                            NULL, 0, &returnCode, TPM_DURATION_TYPE_SHORT);

    dprintf(DEBUG_tcg, "TCGBIOS: ReturnCode from TPM_Startup = 0x%08x\n",
            returnCode);

    if (rc || returnCode)
        goto err_exit;

    return;

err_exit:
    dprintf(DEBUG_tcg, "TCGBIOS: TPM malfunctioning (line %d).\n", __LINE__);

    tpm_set_failure();
}


/****************************************************************
 * BIOS interface
 ****************************************************************/

u8 TPM_interface_shutdown VARLOW;

static inline void *input_buf32(struct bregs *regs)
{
    return MAKE_FLATPTR(regs->es, regs->di);
}

static inline void *output_buf32(struct bregs *regs)
{
    return MAKE_FLATPTR(regs->ds, regs->si);
}

static u32
hash_log_extend_event_int(const struct hleei_short *hleei_s,
                          struct hleeo *hleeo)
{
    u32 rc = 0;
    struct hleo hleo;
    struct hleei_long *hleei_l = (struct hleei_long *)hleei_s;
    const void *logdataptr;
    u32 logdatalen;
    struct pcpes *pcpes;
    u32 pcrindex;

    /* short or long version? */
    switch (hleei_s->ipblength) {
    case sizeof(struct hleei_short):
        /* short */
        logdataptr = hleei_s->logdataptr;
        logdatalen = hleei_s->logdatalen;
        pcrindex = hleei_s->pcrindex;
    break;

    case sizeof(struct hleei_long):
        /* long */
        logdataptr = hleei_l->logdataptr;
        logdatalen = hleei_l->logdatalen;
        pcrindex = hleei_l->pcrindex;
    break;

    default:
        /* bad input block */
        rc = TCG_INVALID_INPUT_PARA;
        goto err_exit;
    }

    pcpes = (struct pcpes *)logdataptr;

    if (pcpes->pcrindex >= 24 || pcpes->pcrindex != pcrindex
        || logdatalen != sizeof(*pcpes) + pcpes->eventdatasize) {
        rc = TCG_INVALID_INPUT_PARA;
        goto err_exit;
    }

    tpm_fill_hash(pcpes, hleei_s->hashdataptr, hleei_s->hashdatalen);
    rc = tpm_log_extend_event(pcpes, pcpes->event);
    if (rc)
        goto err_exit;

    hleeo->opblength = sizeof(struct hleeo);
    hleeo->reserved  = 0;
    hleeo->eventnumber = hleo.eventnumber;

err_exit:
    if (rc != 0) {
        hleeo->opblength = 4;
        hleeo->reserved  = 0;
    }

    return rc;
}

static u32
pass_through_to_tpm_int(struct pttti *pttti, struct pttto *pttto)
{
    u32 rc = 0;
    struct tpm_req_header *trh = (void*)pttti->tpmopin;

    if (pttti->ipblength < sizeof(struct pttti) + sizeof(trh)
        || pttti->ipblength != sizeof(struct pttti) + be32_to_cpu(trh->totlen)
        || pttti->opblength < sizeof(struct pttto)) {
        rc = TCG_INVALID_INPUT_PARA;
        goto err_exit;
    }

    u32 resbuflen = pttti->opblength - offsetof(struct pttto, tpmopout);
    rc = tpmhw_transmit(0, trh, pttto->tpmopout, &resbuflen,
                        TPM_DURATION_TYPE_LONG /* worst case */);
    if (rc)
        goto err_exit;

    pttto->opblength = offsetof(struct pttto, tpmopout) + resbuflen;
    pttto->reserved  = 0;

err_exit:
    if (rc != 0) {
        pttto->opblength = 4;
        pttto->reserved = 0;
    }

    return rc;
}

static u32
shutdown_preboot_interface(void)
{
    TPM_interface_shutdown = 1;
    return 0;
}

static u32
hash_log_event_int(const struct hlei *hlei, struct hleo *hleo)
{
    u32 rc = 0;
    u16 size;
    struct pcpes *pcpes;

    size = hlei->ipblength;
    if (size != sizeof(*hlei)) {
        rc = TCG_INVALID_INPUT_PARA;
        goto err_exit;
    }

    pcpes = (struct pcpes *)hlei->logdataptr;

    if (pcpes->pcrindex >= 24 || pcpes->pcrindex != hlei->pcrindex
        || pcpes->eventtype != hlei->logeventtype
        || hlei->logdatalen != sizeof(*pcpes) + pcpes->eventdatasize) {
        rc = TCG_INVALID_INPUT_PARA;
        goto err_exit;
    }

    tpm_fill_hash(pcpes, hlei->hashdataptr, hlei->hashdatalen);
    rc = tpm_log_event(pcpes, pcpes->event);
    if (rc)
        goto err_exit;

    /* updating the log was fine */
    hleo->opblength = sizeof(struct hleo);
    hleo->reserved  = 0;
    hleo->eventnumber = tpm_state.entry_count;

err_exit:
    if (rc != 0) {
        hleo->opblength = 2;
        hleo->reserved = 0;
    }

    return rc;
}

static u32
hash_all_int(const struct hai *hai, u8 *hash)
{
    if (hai->ipblength != sizeof(struct hai) ||
        hai->hashdataptr == 0 ||
        hai->hashdatalen == 0 ||
        hai->algorithmid != TPM_ALG_SHA)
        return TCG_INVALID_INPUT_PARA;

    return sha1((const u8 *)hai->hashdataptr, hai->hashdatalen, hash);
}

static u32
tss_int(struct ti *ti, struct to *to)
{
    to->opblength = sizeof(struct to);
    to->reserved  = 0;

    return TCG_PC_UNSUPPORTED;
}

static u32
compact_hash_log_extend_event_int(u8 *buffer,
                                  u32 info,
                                  u32 length,
                                  u32 pcrindex,
                                  u32 *edx_ptr)
{
    struct pcpes pcpes = {
        .pcrindex      = pcrindex,
        .eventtype     = EV_COMPACT_HASH,
        .eventdatasize = sizeof(info),
    };

    tpm_fill_hash(&pcpes, buffer, length);
    u32 rc = tpm_log_extend_event(&pcpes, &info);
    if (rc == 0)
        *edx_ptr = tpm_state.entry_count;

    return rc;
}

void VISIBLE32FLAT
tpm_interrupt_handler32(struct bregs *regs)
{
    if (!CONFIG_TCGBIOS)
        return;

    set_cf(regs, 0);

    if (TPM_interface_shutdown && regs->al) {
        regs->eax = TCG_INTERFACE_SHUTDOWN;
        return;
    }

    switch ((enum irq_ids)regs->al) {
    case TCG_StatusCheck:
        if (!tpmhw_is_present()) {
            /* no TPM available */
            regs->eax = TCG_PC_TPM_NOT_PRESENT;
        } else {
            regs->eax = 0;
            regs->ebx = TCG_MAGIC;
            regs->ch = TCG_VERSION_MAJOR;
            regs->cl = TCG_VERSION_MINOR;
            regs->edx = 0x0;
            regs->esi = (u32)tpm_state.log_area_start_address;
            regs->edi = (u32)tpm_state.log_area_last_entry;
        }
        break;

    case TCG_HashLogExtendEvent:
        regs->eax =
            hash_log_extend_event_int(
                  (struct hleei_short *)input_buf32(regs),
                  (struct hleeo *)output_buf32(regs));
        break;

    case TCG_PassThroughToTPM:
        regs->eax =
            pass_through_to_tpm_int((struct pttti *)input_buf32(regs),
                                    (struct pttto *)output_buf32(regs));
        break;

    case TCG_ShutdownPreBootInterface:
        regs->eax = shutdown_preboot_interface();
        break;

    case TCG_HashLogEvent:
        regs->eax = hash_log_event_int((struct hlei*)input_buf32(regs),
                                       (struct hleo*)output_buf32(regs));
        break;

    case TCG_HashAll:
        regs->eax =
            hash_all_int((struct hai*)input_buf32(regs),
                          (u8 *)output_buf32(regs));
        break;

    case TCG_TSS:
        regs->eax = tss_int((struct ti*)input_buf32(regs),
                            (struct to*)output_buf32(regs));
        break;

    case TCG_CompactHashLogExtendEvent:
        regs->eax =
          compact_hash_log_extend_event_int((u8 *)input_buf32(regs),
                                            regs->esi,
                                            regs->ecx,
                                            regs->edx,
                                            &regs->edx);
        break;

    default:
        set_cf(regs, 1);
    }

    return;
}


/****************************************************************
 * TPM Configuration Menu
 ****************************************************************/

static u32
read_stclear_flags(char *buf, int buf_len)
{
    u32 rc;
    u32 returnCode;
    struct tpm_res_getcap_stclear_flags stcf;

    memset(buf, 0, buf_len);

    rc = build_and_send_cmd(0, TPM_ORD_GetCapability,
                            GetCapability_STClear_Flags,
                            sizeof(GetCapability_STClear_Flags),
                            (u8 *)&stcf, sizeof(stcf),
                            &returnCode, TPM_DURATION_TYPE_SHORT);

    dprintf(DEBUG_tcg, "TCGBIOS: Return code from TPM_GetCapability() "
            "= 0x%08x\n", returnCode);

    if (rc || returnCode)
        goto err_exit;

    memcpy(buf, &stcf.stclear_flags, buf_len);

    return 0;

err_exit:
    dprintf(DEBUG_tcg, "TCGBIOS: TPM malfunctioning (line %d).\n", __LINE__);

    tpm_set_failure();
    if (rc)
        return rc;
    return TCG_TCG_COMMAND_ERROR;
}

static u32
assert_physical_presence(int verbose)
{
    u32 rc = 0;
    u32 returnCode;
    struct tpm_stclear_flags stcf;

    rc = read_stclear_flags((char *)&stcf, sizeof(stcf));
    if (rc) {
        dprintf(DEBUG_tcg,
                "Error reading STClear flags: 0x%08x\n", rc);
        return rc;
    }

    if (stcf.flags[STCLEAR_FLAG_IDX_PHYSICAL_PRESENCE])
        /* physical presence already asserted */
        return 0;

    rc = build_and_send_cmd(0, TPM_ORD_PhysicalPresence,
                            PhysicalPresence_CMD_ENABLE,
                            sizeof(PhysicalPresence_CMD_ENABLE),
                            NULL, 0, &returnCode, TPM_DURATION_TYPE_SHORT);

    dprintf(DEBUG_tcg,
           "Return code from TSC_PhysicalPresence(CMD_ENABLE) = 0x%08x\n",
           returnCode);

    if (rc || returnCode) {
        if (verbose)
            printf("Error: Could not enable physical presence.\n\n");
        goto err_exit;
    }

    rc = build_and_send_cmd(0, TPM_ORD_PhysicalPresence,
                            PhysicalPresence_PRESENT,
                            sizeof(PhysicalPresence_PRESENT),
                            NULL, 0, &returnCode, TPM_DURATION_TYPE_SHORT);

    dprintf(DEBUG_tcg,
           "Return code from TSC_PhysicalPresence(PRESENT) = 0x%08x\n",
           returnCode);

    if (rc || returnCode) {
        if (verbose)
            printf("Error: Could not set presence flag.\n\n");
        goto err_exit;
    }

    return 0;

err_exit:
    dprintf(DEBUG_tcg, "TCGBIOS: TPM malfunctioning (line %d).\n", __LINE__);

    tpm_set_failure();
    if (rc)
        return rc;
    return TCG_TCG_COMMAND_ERROR;
}

static u32
read_permanent_flags(char *buf, int buf_len)
{
    u32 rc;
    u32 returnCode;
    struct tpm_res_getcap_perm_flags pf;

    memset(buf, 0, buf_len);

    rc = build_and_send_cmd(0, TPM_ORD_GetCapability,
                            GetCapability_Permanent_Flags,
                            sizeof(GetCapability_Permanent_Flags),
                            (u8 *)&pf, sizeof(pf),
                            &returnCode, TPM_DURATION_TYPE_SHORT);

    dprintf(DEBUG_tcg, "TCGBIOS: Return code from TPM_GetCapability() "
            "= 0x%08x\n", returnCode);

    if (rc || returnCode)
        goto err_exit;

    memcpy(buf, &pf.perm_flags, buf_len);

    return 0;

err_exit:
    dprintf(DEBUG_tcg, "TCGBIOS: TPM malfunctioning (line %d).\n", __LINE__);

    tpm_set_failure();
    if (rc)
        return rc;
    return TCG_TCG_COMMAND_ERROR;
}

static u32
read_has_owner(int *has_owner)
{
    u32 rc;
    u32 returnCode;
    struct tpm_res_getcap_ownerauth oauth;

    rc = build_and_send_cmd(0, TPM_ORD_GetCapability,
                            GetCapability_OwnerAuth,
                            sizeof(GetCapability_OwnerAuth),
                            (u8 *)&oauth, sizeof(oauth),
                            &returnCode, TPM_DURATION_TYPE_SHORT);

    dprintf(DEBUG_tcg, "TCGBIOS: Return code from TPM_GetCapability() "
            "= 0x%08x\n", returnCode);

    if (rc || returnCode)
        goto err_exit;

    *has_owner = oauth.flag;

    return 0;

err_exit:
    dprintf(DEBUG_tcg,"TCGBIOS: TPM malfunctioning (line %d).\n", __LINE__);

    tpm_set_failure();
    if (rc)
        return rc;
    return TCG_TCG_COMMAND_ERROR;
}

static u32
enable_tpm(int enable, u32 *returnCode, int verbose)
{
    u32 rc;
    struct tpm_permanent_flags pf;

    rc = read_permanent_flags((char *)&pf, sizeof(pf));
    if (rc)
        return rc;

    if (pf.flags[PERM_FLAG_IDX_DISABLE] && !enable)
        return 0;

    rc = assert_physical_presence(verbose);
    if (rc) {
        dprintf(DEBUG_tcg, "TCGBIOS: Asserting physical presence failed.\n");
        return rc;
    }

    rc = build_and_send_cmd(0, enable ? TPM_ORD_PhysicalEnable
                                      : TPM_ORD_PhysicalDisable,
                            NULL, 0, NULL, 0, returnCode,
                            TPM_DURATION_TYPE_SHORT);
    if (enable)
        dprintf(DEBUG_tcg, "Return code from TPM_PhysicalEnable = 0x%08x\n",
                *returnCode);
    else
        dprintf(DEBUG_tcg, "Return code from TPM_PhysicalDisable = 0x%08x\n",
                *returnCode);

    if (rc || *returnCode)
        goto err_exit;

    return 0;

err_exit:
    if (enable)
        dprintf(DEBUG_tcg, "TCGBIOS: Enabling the TPM failed.\n");
    else
        dprintf(DEBUG_tcg, "TCGBIOS: Disabling the TPM failed.\n");

    dprintf(DEBUG_tcg, "TCGBIOS: TPM malfunctioning (line %d).\n", __LINE__);

    tpm_set_failure();
    if (rc)
        return rc;
    return TCG_TCG_COMMAND_ERROR;
}

static u32
activate_tpm(int activate, int allow_reset, u32 *returnCode, int verbose)
{
    u32 rc;
    struct tpm_permanent_flags pf;

    rc = read_permanent_flags((char *)&pf, sizeof(pf));
    if (rc)
        return rc;

    if (pf.flags[PERM_FLAG_IDX_DEACTIVATED] && !activate)
        return 0;

    if (pf.flags[PERM_FLAG_IDX_DISABLE])
        return 0;

    rc = assert_physical_presence(verbose);
    if (rc) {
        dprintf(DEBUG_tcg, "TCGBIOS: Asserting physical presence failed.\n");
        return rc;
    }

    rc = build_and_send_cmd(0, TPM_ORD_PhysicalSetDeactivated,
                            activate ? CommandFlag_FALSE
                                     : CommandFlag_TRUE,
                            activate ? sizeof(CommandFlag_FALSE)
                                     : sizeof(CommandFlag_TRUE),
                            NULL, 0, returnCode, TPM_DURATION_TYPE_SHORT);

    dprintf(DEBUG_tcg,
            "Return code from PhysicalSetDeactivated(%d) = 0x%08x\n",
            activate ? 0 : 1, *returnCode);

    if (rc || *returnCode)
        goto err_exit;

    if (activate && allow_reset) {
        if (verbose) {
            printf("Requiring a reboot to activate the TPM.\n");

            msleep(2000);
        }
        reset();
    }

    return 0;

err_exit:
    dprintf(DEBUG_tcg, "TCGBIOS: TPM malfunctioning (line %d).\n", __LINE__);

    tpm_set_failure();
    if (rc)
        return rc;
    return TCG_TCG_COMMAND_ERROR;
}

static u32
enable_activate(int allow_reset, u32 *returnCode, int verbose)
{
    u32 rc;

    rc = enable_tpm(1, returnCode, verbose);
    if (rc)
        return rc;

    rc = activate_tpm(1, allow_reset, returnCode, verbose);

    return rc;
}

static u32
force_clear(int enable_activate_before, int enable_activate_after,
            u32 *returnCode, int verbose)
{
    u32 rc;
    int has_owner;

    rc = read_has_owner(&has_owner);
    if (rc)
        return rc;
    if (!has_owner) {
        if (verbose)
            printf("TPM does not have an owner.\n");
        return 0;
    }

    if (enable_activate_before) {
        rc = enable_activate(0, returnCode, verbose);
        if (rc) {
            dprintf(DEBUG_tcg,
                    "TCGBIOS: Enabling/activating the TPM failed.\n");
            return rc;
        }
    }

    rc = assert_physical_presence(verbose);
    if (rc) {
        dprintf(DEBUG_tcg, "TCGBIOS: Asserting physical presence failed.\n");
        return rc;
    }

    rc = build_and_send_cmd(0, TPM_ORD_ForceClear,
                            NULL, 0, NULL, 0, returnCode,
                            TPM_DURATION_TYPE_SHORT);

    dprintf(DEBUG_tcg, "Return code from TPM_ForceClear() = 0x%08x\n",
            *returnCode);

    if (rc || *returnCode)
        goto err_exit;

    if (!enable_activate_after) {
        if (verbose)
            printf("Owner successfully cleared.\n"
                   "You will need to enable/activate the TPM again.\n\n");
        return 0;
    }

    enable_activate(1, returnCode, verbose);

    return 0;

err_exit:
    dprintf(DEBUG_tcg, "TCGBIOS: TPM malfunctioning (line %d).\n", __LINE__);

    tpm_set_failure();
    if (rc)
        return rc;
    return TCG_TCG_COMMAND_ERROR;
}

static u32
set_owner_install(int allow, u32 *returnCode, int verbose)
{
    u32 rc;
    int has_owner;
    struct tpm_permanent_flags pf;

    rc = read_has_owner(&has_owner);
    if (rc)
        return rc;
    if (has_owner) {
        if (verbose)
            printf("Must first remove owner.\n");
        return 0;
    }

    rc = read_permanent_flags((char *)&pf, sizeof(pf));
    if (rc)
        return rc;

    if (pf.flags[PERM_FLAG_IDX_DISABLE]) {
        if (verbose)
            printf("TPM must first be enable.\n");
        return 0;
    }

    rc = assert_physical_presence(verbose);
    if (rc) {
        dprintf(DEBUG_tcg, "TCGBIOS: Asserting physical presence failed.\n");
        return rc;
    }

    rc = build_and_send_cmd(0, TPM_ORD_SetOwnerInstall,
                            (allow) ? CommandFlag_TRUE :
                                      CommandFlag_FALSE,
                            sizeof(CommandFlag_TRUE),
                            NULL, 0, returnCode, TPM_DURATION_TYPE_SHORT);

    dprintf(DEBUG_tcg, "Return code from TPM_SetOwnerInstall() = 0x%08x\n",
            *returnCode);

    if (rc || *returnCode)
        goto err_exit;

    if (verbose)
        printf("Installation of owner %s.\n", allow ? "enabled" : "disabled");

    return 0;

err_exit:
    dprintf(DEBUG_tcg, "TCGBIOS: TPM malfunctioning (line %d).\n", __LINE__);
    tpm_set_failure();
    if (rc)
        return rc;
    return TCG_TCG_COMMAND_ERROR;
}

static u32
tpm_process_cfg(tpm_ppi_code msgCode, int verbose, u32 *returnCode)
{
    u32 rc = 0;

    switch (msgCode) {
        case TPM_PPI_OP_NOOP: /* no-op */
            break;

        case TPM_PPI_OP_ENABLE:
            rc = enable_tpm(1, returnCode, verbose);
            break;

        case TPM_PPI_OP_DISABLE:
            rc = enable_tpm(0, returnCode, verbose);
            break;

        case TPM_PPI_OP_ACTIVATE:
            rc = activate_tpm(1, 1, returnCode, verbose);
            break;

        case TPM_PPI_OP_DEACTIVATE:
            rc = activate_tpm(0, 1, returnCode, verbose);
            break;

        case TPM_PPI_OP_CLEAR:
            rc = force_clear(1, 0, returnCode, verbose);
            break;

        case TPM_PPI_OP_SET_OWNERINSTALL_TRUE:
            rc = set_owner_install(1, returnCode, verbose);
            break;

        case TPM_PPI_OP_SET_OWNERINSTALL_FALSE:
            rc = set_owner_install(0, returnCode, verbose);
            break;

        default:
            break;
    }

    if (rc)
        printf("Op %d: An error occurred: 0x%x TPM return code: 0x%x\n",
               msgCode, rc, *returnCode);

    return rc;
}

static int
get_tpm_state(void)
{
    int state = 0;
    struct tpm_permanent_flags pf;
    int has_owner;

    if (read_permanent_flags((char *)&pf, sizeof(pf)) ||
        read_has_owner(&has_owner))
        return ~0;

    if (!pf.flags[PERM_FLAG_IDX_DISABLE])
        state |= TPM_STATE_ENABLED;

    if (!pf.flags[PERM_FLAG_IDX_DEACTIVATED])
        state |= TPM_STATE_ACTIVE;

    if (has_owner) {
        state |= TPM_STATE_OWNED;
    } else {
        if (pf.flags[PERM_FLAG_IDX_OWNERSHIP])
            state |= TPM_STATE_OWNERINSTALL;
    }

    return state;
}

static void
show_tpm_menu(int state, int next_scancodes[7])
{
    int i = 0;

    printf("\nThe current state of the TPM is:\n");

    if (state & TPM_STATE_ENABLED)
        printf("  Enabled");
    else
        printf("  Disabled");

    if (state & TPM_STATE_ACTIVE)
        printf(" and active\n");
    else
        printf(" and deactivated\n");

    if (state & TPM_STATE_OWNED)
        printf("  Ownership has been taken\n");
    else {
        printf("  Ownership has not been taken\n");
        if (state & TPM_STATE_OWNERINSTALL)
            printf("  A user can take ownership of the TPM\n");
        else
            printf("  Taking ownership of the TPM has been disabled\n");
    }

    if ((state & (TPM_STATE_ENABLED | TPM_STATE_ACTIVE)) !=
        (TPM_STATE_ENABLED | TPM_STATE_ACTIVE)) {
        printf("\nNote: To make use of all functionality, the TPM must be "
               "enabled and active.\n");
    }

    printf("\nAvailable options are:\n");
    if (state & TPM_STATE_ENABLED) {
        printf(" d. Disable the TPM\n");
        next_scancodes[i++] = 32;

        if (state & TPM_STATE_ACTIVE) {
            printf(" v. Deactivate the TPM\n");
            next_scancodes[i++] = 47;

            if (state & TPM_STATE_OWNERINSTALL) {
                printf(" p. Prevent installation of an owner\n");
                next_scancodes[i++] = 25;
            } else {
                printf(" s. Allow installation of an owner\n");
                next_scancodes[i++] = 31;
            }
        } else {
            printf(" a. Activate the TPM\n");
            next_scancodes[i++] = 30;
        }

    } else {
        printf(" e. Enable the TPM\n");
        next_scancodes[i++] = 18;
    }

    if (state & TPM_STATE_OWNED) {
        printf(" c. Clear ownership\n");
        next_scancodes[i++] = 46;
    }

    next_scancodes[i++] = 0;
}

void
tpm_menu(void)
{
    if (!CONFIG_TCGBIOS)
        return;

    int scancode, next_scancodes[7];
    u32 rc, returnCode;
    tpm_ppi_code msgCode;
    int state = 0, i;
    int waitkey;

    while (get_keystroke(0) >= 0)
        ;
    wait_threads();

    printf("The Trusted Platform Module (TPM) is a hardware device in "
           "this machine.\n"
           "It can help verify the integrity of system software.\n\n");

    for (;;) {
        if ((state = get_tpm_state()) != ~0) {
            show_tpm_menu(state, next_scancodes);
        } else {
            printf("TPM is not working correctly.\n");
            return;
        }

        printf("\nIf no change is desired or if this menu was reached by "
               "mistake, press ESC to\n"
               "reboot the machine.\n");

        msgCode = TPM_PPI_OP_NOOP;

        waitkey = 1;

        while (waitkey) {
            while ((scancode = get_keystroke(1000)) == ~0)
                ;

            switch (scancode) {
            case 1:
                // ESC
                reset();
                break;
            case 18: /* e. enable */
                msgCode = TPM_PPI_OP_ENABLE;
                break;
            case 32: /* d. disable */
                msgCode = TPM_PPI_OP_DISABLE;
                break;
            case 30: /* a. activate */
                msgCode = TPM_PPI_OP_ACTIVATE;
                break;
            case 47: /* v. deactivate */
                msgCode = TPM_PPI_OP_DEACTIVATE;
                break;
            case 46: /* c. clear owner */
                msgCode = TPM_PPI_OP_CLEAR;
                break;
            case 25: /* p. prevent ownerinstall */
                msgCode = TPM_PPI_OP_SET_OWNERINSTALL_FALSE;
                break;
            case 31: /* s. allow ownerinstall */
                msgCode = TPM_PPI_OP_SET_OWNERINSTALL_TRUE;
                break;
            default:
                continue;
            }

            /*
             * Using the next_scancodes array, check whether the
             * pressed key is currently a valid option.
             */
            for (i = 0; i < sizeof(next_scancodes); i++) {
                if (next_scancodes[i] == 0)
                    break;

                if (next_scancodes[i] == scancode) {
                    rc = tpm_process_cfg(msgCode, 1, &returnCode);

                    if (rc)
                        printf("An error occurred: 0x%x\n", rc);
                    waitkey = 0;
                    break;
                }
            }
        }
    }
}
