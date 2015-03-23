//  Implementation of the TCG BIOS extension according to the specification
//  described in
//  https://www.trustedcomputinggroup.org/specs/PCClient/TCG_PCClientImplementationforBIOS_1-20_1-00.pdf
//
//  Copyright (C) 2006-2011, 2014 IBM Corporation
//
//  Authors:
//      Stefan Berger <stefanb@linux.vnet.ibm.com>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.


#include "config.h"

#include "types.h"
#include "byteorder.h" // cpu_to_*
#include "hw/tpm_drivers.h" // tpm_drivers[]
#include "farptr.h" // MAKE_FLATPTR
#include "string.h" // checksum
#include "tcgbios.h"// tpm_*, prototypes
#include "util.h" // printf, get_keystroke
#include "output.h" // dprintf
#include "std/acpi.h"  // RSDP_SIGNATURE, rsdt_descriptor
#include "bregs.h" // struct bregs


static const u8 Startup_ST_CLEAR[2] = { 0x00, TPM_ST_CLEAR };
static const u8 Startup_ST_STATE[2] = { 0x00, TPM_ST_STATE };

static const u8 PhysicalPresence_CMD_ENABLE[2]  = { 0x00, 0x20 };
static const u8 PhysicalPresence_CMD_DISABLE[2] = { 0x01, 0x00 };
static const u8 PhysicalPresence_PRESENT[2]     = { 0x00, 0x08 };
static const u8 PhysicalPresence_NOT_PRESENT_LOCK[2] = { 0x00, 0x14 };

static const u8 CommandFlag_FALSE[1] = { 0x00 };
static const u8 CommandFlag_TRUE[1]  = { 0x01 };

static const u8 GetCapability_Permanent_Flags[12] = {
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x01, 0x08
};

static const u8 GetCapability_OwnerAuth[12] = {
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


#define RSDP_CAST(ptr)   ((struct rsdp_descriptor *)ptr)


/* helper functions */

static inline void *input_buf32(struct bregs *regs)
{
    return MAKE_FLATPTR(regs->es, regs->di);
}

static inline void *output_buf32(struct bregs *regs)
{
    return MAKE_FLATPTR(regs->ds, regs->si);
}


typedef struct {
    u8            tpm_probed:1;
    u8            tpm_found:1;
    u8            tpm_working:1;
    u8            if_shutdown:1;
    u8            tpm_driver_to_use:4;
} tpm_state_t;


static tpm_state_t tpm_state = {
    .tpm_driver_to_use = TPM_INVALID_DRIVER,
};


/********************************************************
  Extensions for TCG-enabled BIOS
 *******************************************************/


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

    return tcpa;
}


static struct tcpa_descriptor_rev2 *
find_tcpa_table(void)
{
    struct tcpa_descriptor_rev2 *tcpa = NULL;
    struct rsdp_descriptor *rsdp = RsdpAddr;

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
    u32 log_area_minimum_length;
    u8 *log_area_start_address = get_lasa_base_ptr(&log_area_minimum_length);

    if (log_area_start_address)
        memset(log_area_start_address, 0x0, log_area_minimum_length);
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


/*
 * Send a TPM command with the given ordinal. Append the given buffer
 * containing all data in network byte order to the command (this is
 * the custom part per command) and expect a response of the given size.
 * If a buffer is provided, the response will be copied into it.
 */
static u32
build_and_send_cmd_od(u32 ordinal, const u8 *append, u32 append_size,
                      u8 *resbuffer, u32 return_size, u32 *returnCode,
                      const u8 *otherdata, u32 otherdata_size,
                      enum tpmDurationType to_t)
{
#define MAX_APPEND_SIZE   12
#define MAX_RESPONSE_SIZE sizeof(struct tpm_res_getcap_perm_flags)
    u32 rc;
    u8 ibuffer[TPM_REQ_HEADER_SIZE + MAX_APPEND_SIZE];
    u8 obuffer[MAX_RESPONSE_SIZE];
    struct tpm_req_header *trqh = (struct tpm_req_header *)ibuffer;
    struct tpm_rsp_header *trsh = (struct tpm_rsp_header *)obuffer;
    u8 locty = 0;
    struct iovec iovec[3];
    u32 obuffer_len = sizeof(obuffer);
    u32 idx = 1;

    if (append_size > MAX_APPEND_SIZE ||
        return_size > MAX_RESPONSE_SIZE) {
        dprintf(DEBUG_tcg, "TCGBIOS: size of requested buffers too big.");
        return TCG_FIRMWARE_ERROR;
    }

    iovec[0].data   = trqh;
    iovec[0].length = TPM_REQ_HEADER_SIZE + append_size;

    if (otherdata) {
        iovec[1].data   = (void *)otherdata;
        iovec[1].length = otherdata_size;
        idx = 2;
    }

    iovec[idx].data   = NULL;
    iovec[idx].length = 0;

    memset(ibuffer, 0x0, sizeof(ibuffer));
    memset(obuffer, 0x0, sizeof(obuffer));

    trqh->tag     = cpu_to_be16(0xc1);
    trqh->totlen  = cpu_to_be32(TPM_REQ_HEADER_SIZE + append_size + otherdata_size);
    trqh->ordinal = cpu_to_be32(ordinal);

    if (append_size)
        memcpy((char *)trqh + sizeof(*trqh),
               append, append_size);

    rc = transmit(locty, iovec, obuffer, &obuffer_len, to_t);
    if (rc)
        return rc;

    *returnCode = be32_to_cpu(trsh->errcode);

    if (resbuffer)
        memcpy(resbuffer, trsh, return_size);

    return 0;
}


static u32
build_and_send_cmd(u32 ordinal, const u8 *append, u32 append_size,
                   u8 *resbuffer, u32 return_size, u32 *returnCode,
                   enum tpmDurationType to_t)
{
    return build_and_send_cmd_od(ordinal, append, append_size,
                                 resbuffer, return_size, returnCode,
                                 NULL, 0, to_t);
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

    rc = build_and_send_cmd(TPM_ORD_GetCapability,
                            GetCapability_Timeouts,
                            sizeof(GetCapability_Timeouts),
                            (u8 *)&timeouts,
                            sizeof(struct tpm_res_getcap_timeouts),
                            &returnCode, TPM_DURATION_TYPE_SHORT);

    dprintf(DEBUG_tcg, "TCGBIOS: Return code from TPM_GetCapability(Timeouts)"
            " = 0x%08x\n", returnCode);

    if (rc || returnCode)
        goto err_exit;

    rc = build_and_send_cmd(TPM_ORD_GetCapability,
                            GetCapability_Durations,
                            sizeof(GetCapability_Durations),
                            (u8 *)&durations,
                            sizeof(struct tpm_res_getcap_durations),
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

    tpm_state.tpm_working = 0;
    if (rc)
        return rc;
    return TCG_TCG_COMMAND_ERROR;
}


static u32
tpm_startup(void)
{
    u32 rc;
    u32 returnCode;

    if (!has_working_tpm())
        return TCG_GENERAL_ERROR;

    dprintf(DEBUG_tcg, "TCGBIOS: Starting with TPM_Startup(ST_CLEAR)\n");
    rc = build_and_send_cmd(TPM_ORD_Startup,
                            Startup_ST_CLEAR, sizeof(Startup_ST_CLEAR),
                            NULL, 10, &returnCode, TPM_DURATION_TYPE_SHORT);

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

    rc = build_and_send_cmd(TPM_ORD_SelfTestFull, NULL, 0,
                            NULL, 10, &returnCode, TPM_DURATION_TYPE_LONG);

    dprintf(DEBUG_tcg, "Return code from TPM_SelfTestFull = 0x%08x\n",
            returnCode);

    if (rc || returnCode)
        goto err_exit;

    rc = build_and_send_cmd(TSC_ORD_ResetEstablishmentBit, NULL, 0,
                            NULL, 10, &returnCode, TPM_DURATION_TYPE_SHORT);

    dprintf(DEBUG_tcg, "Return code from TSC_ResetEstablishmentBit = 0x%08x\n",
            returnCode);

    if (rc || (returnCode != 0 && returnCode != TPM_BAD_LOCALITY))
        goto err_exit;

    rc = determine_timeouts();
    if (rc)
        goto err_exit;

    return 0;

err_exit:
    dprintf(DEBUG_tcg, "TCGBIOS: TPM malfunctioning (line %d).\n", __LINE__);

    tpm_state.tpm_working = 0;
    if (rc)
        return rc;
    return TCG_TCG_COMMAND_ERROR;
}


u32
tpm_start(void)
{
    if (!CONFIG_TCGBIOS)
        return 0;

    tpm_acpi_init();

    return tpm_startup();
}


u32
tpm_leave_bios(void)
{
    u32 rc;
    u32 returnCode;

    if (!CONFIG_TCGBIOS)
        return 0;

    if (!has_working_tpm())
        return TCG_GENERAL_ERROR;

    rc = build_and_send_cmd(TPM_ORD_PhysicalPresence,
                            PhysicalPresence_CMD_ENABLE,
                            sizeof(PhysicalPresence_CMD_ENABLE),
                            NULL, 10, &returnCode, TPM_DURATION_TYPE_SHORT);
    if (rc || returnCode)
        goto err_exit;

    rc = build_and_send_cmd(TPM_ORD_PhysicalPresence,
                            PhysicalPresence_NOT_PRESENT_LOCK,
                            sizeof(PhysicalPresence_NOT_PRESENT_LOCK),
                            NULL, 10, &returnCode, TPM_DURATION_TYPE_SHORT);
    if (rc || returnCode)
        goto err_exit;

    return 0;

err_exit:
    dprintf(DEBUG_tcg, "TCGBIOS: TPM malfunctioning (line %d).\n", __LINE__);

    tpm_state.tpm_working = 0;
    if (rc)
        return rc;
    return TCG_TCG_COMMAND_ERROR;
}


u32
tpm_s3_resume(void)
{
    u32 rc;
    u32 returnCode;

    if (!CONFIG_TCGBIOS)
        return 0;

    if (!has_working_tpm())
        return TCG_GENERAL_ERROR;

    dprintf(DEBUG_tcg, "TCGBIOS: Resuming with TPM_Startup(ST_STATE)\n");

    rc = build_and_send_cmd(TPM_ORD_Startup,
                            Startup_ST_STATE, sizeof(Startup_ST_STATE),
                            NULL, 10, &returnCode, TPM_DURATION_TYPE_SHORT);

    dprintf(DEBUG_tcg, "TCGBIOS: ReturnCode from TPM_Startup = 0x%08x\n",
            returnCode);

    if (rc || returnCode)
        goto err_exit;

    return 0;

err_exit:
    dprintf(DEBUG_tcg, "TCGBIOS: TPM malfunctioning (line %d).\n", __LINE__);

    tpm_state.tpm_working = 0;
    if (rc)
        return rc;
    return TCG_TCG_COMMAND_ERROR;
}
