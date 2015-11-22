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

static u8 evt_separator[] = {0xff,0xff,0xff,0xff};


/****************************************************************
 * TPM state tracking
 ****************************************************************/

typedef struct {
    u8            tpm_probed:1;
    u8            tpm_found:1;
    u8            tpm_working:1;
    u8            if_shutdown:1;
    u8            tpm_driver_to_use:4;
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

tpm_state_t tpm_state VARLOW = {
    .tpm_driver_to_use = TPM_INVALID_DRIVER,
};

static u32
is_preboot_if_shutdown(void)
{
    return tpm_state.if_shutdown;
}


/****************************************************************
 * TPM hardware interface
 ****************************************************************/

static u32
is_tpm_present(void)
{
    u32 rc = 0;
    unsigned int i;

    for (i = 0; i < TPM_NUM_DRIVERS; i++) {
        struct tpm_driver *td = &tpm_drivers[i];
        if (td->probe() != 0) {
            td->init();
            tpm_state.tpm_driver_to_use = i;
            rc = 1;
            break;
        }
    }

    return rc;
}

static void
probe_tpm(void)
{
    if (!tpm_state.tpm_probed) {
        tpm_state.tpm_probed = 1;
        tpm_state.tpm_found = (is_tpm_present() != 0);
        tpm_state.tpm_working = tpm_state.tpm_found;
    }
}

static int
has_working_tpm(void)
{
    probe_tpm();

    return tpm_state.tpm_working;
}

static u32
transmit(u8 locty, const struct iovec iovec[],
         u8 *respbuffer, u32 *respbufferlen,
         enum tpmDurationType to_t)
{
    u32 rc = 0;
    u32 irc;
    struct tpm_driver *td;
    unsigned int i;

    if (tpm_state.tpm_driver_to_use == TPM_INVALID_DRIVER)
        return TCG_FATAL_COM_ERROR;

    td = &tpm_drivers[tpm_state.tpm_driver_to_use];

    irc = td->activate(locty);
    if (irc != 0) {
        /* tpm could not be activated */
        return TCG_FATAL_COM_ERROR;
    }

    for (i = 0; iovec[i].length; i++) {
        irc = td->senddata(iovec[i].data,
                           iovec[i].length);
        if (irc != 0)
            return TCG_FATAL_COM_ERROR;
    }

    irc = td->waitdatavalid();
    if (irc != 0)
        return TCG_FATAL_COM_ERROR;

    irc = td->waitrespready(to_t);
    if (irc != 0)
        return TCG_FATAL_COM_ERROR;

    irc = td->readresp(respbuffer,
                       respbufferlen);
    if (irc != 0)
        return TCG_FATAL_COM_ERROR;

    td->ready();

    return rc;
}


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
    else
        tpm_state.if_shutdown = 1;

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

static void tpm_set_failure(void);

/*
 * Extend the ACPI log with the given entry by copying the
 * entry data into the log.
 * Input
 *  pcpes : Pointer to the event 'header' to be copied into the log
 *  event : Pointer to the event 'body' to be copied into the log
 *  event_length: Length of the event array
 *  entry_count : optional pointer to get the current entry count
 *
 * Output:
 *  Returns an error code in case of faiure, 0 in case of success
 */
static u32
tpm_extend_acpi_log(struct pcpes *pcpes,
                    const char *event, u32 event_length,
                    u16 *entry_count)
{
    u32 size;

    if (!has_working_tpm())
        return TCG_GENERAL_ERROR;

    dprintf(DEBUG_tcg, "TCGBIOS: LASA = %p, next entry = %p\n",
            tpm_state.log_area_start_address, tpm_state.log_area_next_entry);

    if (tpm_state.log_area_next_entry == NULL) {

        tpm_set_failure();

        return TCG_PC_LOGOVERFLOW;
    }

    size = offsetof(struct pcpes, event) + event_length;

    if ((tpm_state.log_area_next_entry + size - tpm_state.log_area_start_address) >
         tpm_state.log_area_minimum_length) {
        dprintf(DEBUG_tcg, "TCGBIOS: LOG OVERFLOW: size = %d\n", size);

        tpm_set_failure();

        return TCG_PC_LOGOVERFLOW;
    }

    pcpes->eventdatasize = event_length;

    memcpy(tpm_state.log_area_next_entry, pcpes, offsetof(struct pcpes, event));
    memcpy(tpm_state.log_area_next_entry + offsetof(struct pcpes, event),
           event, event_length);

    tpm_state.log_area_last_entry = tpm_state.log_area_next_entry;
    tpm_state.log_area_next_entry += size;
    tpm_state.entry_count++;

    if (entry_count)
        *entry_count = tpm_state.entry_count;

    return 0;
}


/****************************************************************
 * Helper functions
 ****************************************************************/

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
    u32 rc;
    u8 obuffer[64];
    struct tpm_req_header trqh;
    struct tpm_rsp_header *trsh = (struct tpm_rsp_header *)obuffer;
    struct iovec iovec[3] = {{ 0 }};
    u32 obuffer_len = sizeof(obuffer);

    if (return_size > sizeof(obuffer)) {
        dprintf(DEBUG_tcg, "TCGBIOS: size of requested response too big.");
        return TCG_FIRMWARE_ERROR;
    }

    trqh.tag = cpu_to_be16(TPM_TAG_RQU_CMD);
    trqh.totlen = cpu_to_be32(TPM_REQ_HEADER_SIZE + append_size);
    trqh.ordinal = cpu_to_be32(ordinal);

    iovec[0].data   = &trqh;
    iovec[0].length = TPM_REQ_HEADER_SIZE;

    if (append_size) {
        iovec[1].data   = append;
        iovec[1].length = append_size;
    }

    memset(obuffer, 0x0, sizeof(obuffer));

    rc = transmit(locty, iovec, obuffer, &obuffer_len, to_t);
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

    tpm_state.tpm_working = 0;
}

static u32
determine_timeouts(void)
{
    u32 rc;
    u32 returnCode;
    struct tpm_res_getcap_timeouts timeouts;
    struct tpm_res_getcap_durations durations;
    struct tpm_driver *td = &tpm_drivers[tpm_state.tpm_driver_to_use];
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


    td->set_timeouts(timeouts.timeouts, durations.durations);

    return 0;

err_exit:
    dprintf(DEBUG_tcg, "TCGBIOS: TPM malfunctioning (line %d).\n", __LINE__);

    tpm_set_failure();
    if (rc)
        return rc;
    return TCG_TCG_COMMAND_ERROR;
}

static u32
pass_through_to_tpm(u8 locty, const u8 *cmd, u32 cmd_length,
                    u8 *resp, u32 *resp_length)
{
    struct iovec iovec[2] = {{ 0 }};
    const u32 *tmp;

    if (cmd_length < TPM_REQ_HEADER_SIZE)
        return TCG_INVALID_INPUT_PARA;

    iovec[0].data = cmd;
    tmp = (const u32 *)&((u8 *)iovec[0].data)[2];
    iovec[0].length = cpu_to_be32(*tmp);

    if (cmd_length != iovec[0].length)
        return TCG_INVALID_INPUT_PARA;

    return transmit(locty, iovec, resp, resp_length,
                    TPM_DURATION_TYPE_LONG /* worst case */);

}

static u32
tpm_extend(u8 *hash, u32 pcrindex)
{
    u32 rc;
    struct tpm_req_extend tre = {
        .tag      = cpu_to_be16(TPM_TAG_RQU_CMD),
        .totlen   = cpu_to_be32(sizeof(tre)),
        .ordinal  = cpu_to_be32(TPM_ORD_Extend),
        .pcrindex = cpu_to_be32(pcrindex),
    };
    struct tpm_rsp_extend rsp;
    u32 resp_length = sizeof(rsp);

    memcpy(tre.digest, hash, sizeof(tre.digest));

    rc = pass_through_to_tpm(0, (u8 *)&tre, sizeof(tre),
                             (u8 *)&rsp, &resp_length);

    if (rc || resp_length != sizeof(rsp))
        tpm_set_failure();

    return rc;
}

static u32
hash_log_event(const void *hashdata, u32 hashdata_length,
               struct pcpes *pcpes,
               const char *event, u32 event_length,
               u16 *entry_count)
{
    u32 rc = 0;

    if (pcpes->pcrindex >= 24)
        return TCG_INVALID_INPUT_PARA;

    if (hashdata) {
        rc = sha1(hashdata, hashdata_length, pcpes->digest);
        if (rc)
            return rc;
    }

    return tpm_extend_acpi_log(pcpes, event, event_length, entry_count);
}

static u32
hash_log_extend_event(const void *hashdata, u32 hashdata_length,
                      struct pcpes *pcpes,
                      const char *event, u32 event_length,
                      u32 pcrindex, u16 *entry_count)
{
    u32 rc;

    rc = hash_log_event(hashdata, hashdata_length, pcpes,
                        event, event_length, entry_count);
    if (rc)
        return rc;

    return tpm_extend(pcpes->digest, pcrindex);
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
    };
    u16 entry_count;

    return hash_log_extend_event(hashdata, hashdata_length, &pcpes,
                                 event, event_length, pcrindex,
                                 &entry_count);
}


/****************************************************************
 * Setup and Measurements
 ****************************************************************/

/*
 * Add a measurement to the list of measurements
 * pcrIndex   : PCR to be extended
 * event_type : type of event; specs section on 'Event Types'
 * data       : additional parameter; used as parameter for
 *              'action index'
 */
static u32
tpm_add_measurement(u32 pcrIndex,
                    u16 event_type,
                    const char *string)
{
    u32 rc;
    u32 len;

    switch (event_type) {
    case EV_SEPARATOR:
        len = sizeof(evt_separator);
        rc = tpm_add_measurement_to_log(pcrIndex, event_type,
                                        (char *)NULL, 0,
                                        (u8 *)evt_separator, len);
        break;

    case EV_ACTION:
        rc = tpm_add_measurement_to_log(pcrIndex, event_type,
                                        string, strlen(string),
                                        (u8 *)string, strlen(string));
        break;

    default:
        rc = TCG_INVALID_INPUT_PARA;
    }

    return rc;
}

static u32
tpm_calling_int19h(void)
{
    if (!CONFIG_TCGBIOS)
        return 0;

    if (!has_working_tpm())
        return TCG_GENERAL_ERROR;

    return tpm_add_measurement(4, EV_ACTION,
                               "Calling INT 19h");
}

/*
 * Add event separators for PCRs 0 to 7; specs on 'Measuring Boot Events'
 */
static u32
tpm_add_event_separators(void)
{
    u32 rc;
    u32 pcrIndex = 0;

    if (!CONFIG_TCGBIOS)
        return 0;

    if (!has_working_tpm())
        return TCG_GENERAL_ERROR;

    while (pcrIndex <= 7) {
        rc = tpm_add_measurement(pcrIndex, EV_SEPARATOR, NULL);
        if (rc)
            break;
        pcrIndex ++;
    }

    return rc;
}

/*
 * Add measurement to the log about option rom scan
 */
static u32
tpm_start_option_rom_scan(void)
{
    if (!CONFIG_TCGBIOS)
        return 0;

    if (!has_working_tpm())
        return TCG_GENERAL_ERROR;

    return tpm_add_measurement(2, EV_ACTION,
                               "Start Option ROM Scan");
}

static u32
tpm_smbios_measure(void)
{
    if (!CONFIG_TCGBIOS)
        return 0;

    if (!has_working_tpm())
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

    if (!has_working_tpm())
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

    rc = tpm_start_option_rom_scan();
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
    tpm_state.if_shutdown = 0;
    tpm_state.tpm_probed = 0;
    tpm_state.tpm_found = 0;
    tpm_state.tpm_working = 0;

    if (!has_working_tpm()) {
        tpm_state.if_shutdown = 1;
        return;
    }

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

    if (!CONFIG_TCGBIOS)
        return;

    if (!has_working_tpm())
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

    rc = tpm_calling_int19h();
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
    if (!CONFIG_TCGBIOS)
        return 0;

    if (!has_working_tpm())
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

/*
 * Add a measurement regarding the boot device (CDRom, Floppy, HDD) to
 * the list of measurements.
 */
static u32
tpm_add_bootdevice(u32 bootcd, u32 bootdrv)
{
    const char *string;

    if (!CONFIG_TCGBIOS)
        return 0;

    if (!has_working_tpm())
        return TCG_GENERAL_ERROR;

    switch (bootcd) {
    case 0:
        switch (bootdrv) {
        case 0:
            string = "Booting BCV device 00h (Floppy)";
            break;

        case 0x80:
            string = "Booting BCV device 80h (HDD)";
            break;

        default:
            string = "Booting unknown device";
            break;
        }

        break;

    default:
        string = "Booting from CD ROM device";
    }

    return tpm_add_measurement_to_log(4, EV_ACTION,
                                      string, strlen(string),
                                      (u8 *)string, strlen(string));
}

/*
 * Add a measurement related to Initial Program Loader to the log.
 * Creates two log entries.
 *
 * Input parameter:
 *  bootcd : 0: MBR of hdd, 1: boot image, 2: boot catalog of El Torito
 *  addr   : address where the IP data are located
 *  length : IP data length in bytes
 */
static u32
tpm_ipl(enum ipltype bootcd, const u8 *addr, u32 length)
{
    u32 rc;
    const char *string;

    switch (bootcd) {
    case IPL_EL_TORITO_1:
        /* specs: see section 'El Torito' */
        string = "EL TORITO IPL";
        rc = tpm_add_measurement_to_log(4, EV_IPL,
                                        string, strlen(string),
                                        addr, length);
        break;

    case IPL_EL_TORITO_2:
        /* specs: see section 'El Torito' */
        string = "BOOT CATALOG";
        rc = tpm_add_measurement_to_log(5, EV_IPL_PARTITION_DATA,
                                        string, strlen(string),
                                        addr, length);
        break;

    default:
        /* specs: see section 'Hard Disk Device or Hard Disk-Like Devices' */
        /* equivalent to: dd if=/dev/hda ibs=1 count=440 | sha1sum */
        string = "MBR";
        rc = tpm_add_measurement_to_log(4, EV_IPL,
                                        string, strlen(string),
                                        addr, 0x1b8);

        if (rc)
            break;

        /* equivalent to: dd if=/dev/hda ibs=1 count=72 skip=440 | sha1sum */
        string = "MBR PARTITION_TABLE";
        rc = tpm_add_measurement_to_log(5, EV_IPL_PARTITION_DATA,
                                        string, strlen(string),
                                        addr + 0x1b8, 0x48);
    }

    return rc;
}

u32
tpm_add_bcv(u32 bootdrv, const u8 *addr, u32 length)
{
    if (!CONFIG_TCGBIOS)
        return 0;

    if (!has_working_tpm())
        return TCG_GENERAL_ERROR;

    u32 rc = tpm_add_bootdevice(0, bootdrv);
    if (rc)
        return rc;

    return tpm_ipl(IPL_BCV, addr, length);
}

u32
tpm_add_cdrom(u32 bootdrv, const u8 *addr, u32 length)
{
    if (!CONFIG_TCGBIOS)
        return 0;

    if (!has_working_tpm())
        return TCG_GENERAL_ERROR;

    u32 rc = tpm_add_bootdevice(1, bootdrv);
    if (rc)
        return rc;

    return tpm_ipl(IPL_EL_TORITO_1, addr, length);
}

u32
tpm_add_cdrom_catalog(const u8 *addr, u32 length)
{
    if (!CONFIG_TCGBIOS)
        return 0;

    if (!has_working_tpm())
        return TCG_GENERAL_ERROR;

    u32 rc = tpm_add_bootdevice(1, 0);
    if (rc)
        return rc;

    return tpm_ipl(IPL_EL_TORITO_2, addr, length);
}

void
tpm_s3_resume(void)
{
    u32 rc;
    u32 returnCode;

    if (!CONFIG_TCGBIOS)
        return;

    if (!has_working_tpm())
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

    if (is_preboot_if_shutdown() != 0) {
        rc = TCG_INTERFACE_SHUTDOWN;
        goto err_exit;
    }

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

    if (pcpes->pcrindex >= 24 ||
        pcpes->pcrindex != pcrindex ||
        logdatalen != offsetof(struct pcpes, event) + pcpes->eventdatasize) {
        rc = TCG_INVALID_INPUT_PARA;
        goto err_exit;
    }

    rc = hash_log_extend_event(hleei_s->hashdataptr, hleei_s->hashdatalen,
                               pcpes,
                               (char *)&pcpes->event, pcpes->eventdatasize,
                               pcrindex, NULL);
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
    u32 resbuflen = 0;
    struct tpm_req_header *trh;

    if (is_preboot_if_shutdown()) {
        rc = TCG_INTERFACE_SHUTDOWN;
        goto err_exit;
    }

    trh = (struct tpm_req_header *)pttti->tpmopin;

    if (pttti->ipblength < sizeof(struct pttti) + TPM_REQ_HEADER_SIZE ||
        pttti->opblength < sizeof(struct pttto) ||
        be32_to_cpu(trh->totlen)  + sizeof(struct pttti) > pttti->ipblength ) {
        rc = TCG_INVALID_INPUT_PARA;
        goto err_exit;
    }

    resbuflen = pttti->opblength - offsetof(struct pttto, tpmopout);

    rc = pass_through_to_tpm(0, pttti->tpmopin,
                             pttti->ipblength - offsetof(struct pttti, tpmopin),
                             pttto->tpmopout, &resbuflen);

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
    u32 rc = 0;

    if (!is_preboot_if_shutdown()) {
        tpm_state.if_shutdown = 1;
    } else {
        rc = TCG_INTERFACE_SHUTDOWN;
    }

    return rc;
}

static u32
hash_log_event_int(const struct hlei *hlei, struct hleo *hleo)
{
    u32 rc = 0;
    u16 size;
    struct pcpes *pcpes;
    u16 entry_count;

    if (is_preboot_if_shutdown() != 0) {
        rc = TCG_INTERFACE_SHUTDOWN;
        goto err_exit;
    }

    size = hlei->ipblength;
    if (size != sizeof(*hlei)) {
        rc = TCG_INVALID_INPUT_PARA;
        goto err_exit;
    }

    pcpes = (struct pcpes *)hlei->logdataptr;

    if (pcpes->pcrindex >= 24 ||
        pcpes->pcrindex  != hlei->pcrindex ||
        pcpes->eventtype != hlei->logeventtype ||
        hlei->logdatalen !=
           offsetof(struct pcpes, event) + pcpes->eventdatasize) {
        rc = TCG_INVALID_INPUT_PARA;
        goto err_exit;
    }

    rc = hash_log_event(hlei->hashdataptr, hlei->hashdatalen,
                        pcpes, (char *)&pcpes->event, pcpes->eventdatasize,
                        &entry_count);
    if (rc)
        goto err_exit;

    /* updating the log was fine */
    hleo->opblength = sizeof(struct hleo);
    hleo->reserved  = 0;
    hleo->eventnumber = entry_count;

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
    if (is_preboot_if_shutdown() != 0)
        return TCG_INTERFACE_SHUTDOWN;

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
    u32 rc = 0;

    if (is_preboot_if_shutdown() == 0) {
        rc = TCG_PC_UNSUPPORTED;
    } else {
        rc = TCG_INTERFACE_SHUTDOWN;
    }

    to->opblength = sizeof(struct to);
    to->reserved  = 0;

    return rc;
}

static u32
compact_hash_log_extend_event_int(u8 *buffer,
                                  u32 info,
                                  u32 length,
                                  u32 pcrindex,
                                  u32 *edx_ptr)
{
    u32 rc = 0;
    struct pcpes pcpes = {
        .pcrindex      = pcrindex,
        .eventtype     = EV_COMPACT_HASH,
        .eventdatasize = sizeof(info),
        .event         = info,
    };
    u16 entry_count;

    if (is_preboot_if_shutdown() != 0)
        return TCG_INTERFACE_SHUTDOWN;

    rc = hash_log_extend_event(buffer, length,
                               &pcpes,
                               (char *)&pcpes.event, pcpes.eventdatasize,
                               pcpes.pcrindex, &entry_count);

    if (rc == 0)
        *edx_ptr = entry_count;

    return rc;
}

void VISIBLE32FLAT
tpm_interrupt_handler32(struct bregs *regs)
{
    if (!CONFIG_TCGBIOS)
        return;

    set_cf(regs, 0);

    if (!has_working_tpm()) {
        regs->eax = TCG_GENERAL_ERROR;
        return;
    }

    switch ((enum irq_ids)regs->al) {
    case TCG_StatusCheck:
        if (is_tpm_present() == 0) {
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
