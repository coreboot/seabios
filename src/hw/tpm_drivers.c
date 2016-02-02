// Implementation of a TPM driver for the TPM TIS interface
//
// Copyright (C) 2006-2011 IBM Corporation
//
// Authors:
//     Stefan Berger <stefanb@linux.vnet.ibm.com>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "byteorder.h" // be32_to_cpu
#include "config.h" // CONFIG_TPM_TIS_SHA1THRESHOLD
#include "hw/tpm_drivers.h" // struct tpm_driver
#include "std/tcg.h" // TCG_RESPONSE_TIMEOUT
#include "output.h" // warn_timeout
#include "stacks.h" // yield
#include "string.h" // memcpy
#include "util.h" // timer_calc_usec
#include "x86.h" // readl

/* low level driver implementation */
struct tpm_driver {
    u32 *timeouts;
    u32 *durations;
    void (*set_timeouts)(u32 timeouts[4], u32 durations[3]);
    u32 (*probe)(void);
    TPMVersion (*get_tpm_version)(void);
    u32 (*init)(void);
    u32 (*activate)(u8 locty);
    u32 (*ready)(void);
    u32 (*senddata)(const u8 *const data, u32 len);
    u32 (*readresp)(u8 *buffer, u32 *len);
    u32 (*waitdatavalid)(void);
    u32 (*waitrespready)(enum tpmDurationType to_t);
};

extern struct tpm_driver tpm_drivers[];

#define TIS_DRIVER_IDX       0
#define TPM_NUM_DRIVERS      1

#define TPM_INVALID_DRIVER   0xf

static const u32 tis_default_timeouts[4] = {
    TIS_DEFAULT_TIMEOUT_A,
    TIS_DEFAULT_TIMEOUT_B,
    TIS_DEFAULT_TIMEOUT_C,
    TIS_DEFAULT_TIMEOUT_D,
};

static const u32 tpm_default_durations[3] = {
    TPM_DEFAULT_DURATION_SHORT,
    TPM_DEFAULT_DURATION_MEDIUM,
    TPM_DEFAULT_DURATION_LONG,
};

/* determined values */
static u32 tpm_default_dur[3];
static u32 tpm_default_to[4];

/* if device is not there, return '0', '1' otherwise */
static u32 tis_probe(void)
{
    if (!CONFIG_TCGBIOS)
        return 0;

    u32 rc = 0;
    u32 didvid = readl(TIS_REG(0, TIS_REG_DID_VID));

    if ((didvid != 0) && (didvid != 0xffffffff))
        rc = 1;

    /* TPM 2 has an interface register */
    u32 ifaceid = readl(TIS_REG(0, TIS_REG_IFACE_ID));

    if ((ifaceid & 0xf) != 0xf) {
        if ((ifaceid & 0xf) == 1) {
            /* CRB is active; no TIS */
            return 0;
        }
        if ((ifaceid & (1 << 13)) == 0) {
            /* TIS cannot be selected */
            return 0;
        }
        /* write of 0 to bits 17-18 selects TIS */
        writel(TIS_REG(0, TIS_REG_IFACE_ID), 0);
        /* since we only support TIS, we lock it */
        writel(TIS_REG(0, TIS_REG_IFACE_ID), (1 << 19));
    }

    return rc;
}

static TPMVersion tis_get_tpm_version(void)
{
    /* TPM 2 has an interface register */
    u32 ifaceid = readl(TIS_REG(0, TIS_REG_IFACE_ID));

    if ((ifaceid & 0xf) == 0) {
        /* TPM 2 */
        return TPM_VERSION_2;
    }
    return TPM_VERSION_1_2;
}

static u32 tis_init(void)
{
    if (!CONFIG_TCGBIOS)
        return 1;

    writeb(TIS_REG(0, TIS_REG_INT_ENABLE), 0);

    if (tpm_drivers[TIS_DRIVER_IDX].durations == NULL) {
        u32 *durations = tpm_default_dur;
        memcpy(durations, tpm_default_durations,
               sizeof(tpm_default_durations));
        tpm_drivers[TIS_DRIVER_IDX].durations = durations;
    }

    if (tpm_drivers[TIS_DRIVER_IDX].timeouts == NULL) {
        u32 *timeouts = tpm_default_to;
        memcpy(timeouts, tis_default_timeouts,
               sizeof(tis_default_timeouts));
        tpm_drivers[TIS_DRIVER_IDX].timeouts = timeouts;
    }

    return 1;
}


static void set_timeouts(u32 timeouts[4], u32 durations[3])
{
    if (!CONFIG_TCGBIOS)
        return;

    u32 *tos = tpm_drivers[TIS_DRIVER_IDX].timeouts;
    u32 *dus = tpm_drivers[TIS_DRIVER_IDX].durations;

    if (tos && tos != tis_default_timeouts && timeouts)
        memcpy(tos, timeouts, 4 * sizeof(u32));
    if (dus && dus != tpm_default_durations && durations)
        memcpy(dus, durations, 3 * sizeof(u32));
}


static u32 tis_wait_sts(u8 locty, u32 time, u8 mask, u8 expect)
{
    if (!CONFIG_TCGBIOS)
        return 0;

    u32 rc = 1;
    u32 end = timer_calc_usec(time);

    for (;;) {
        u8 sts = readb(TIS_REG(locty, TIS_REG_STS));
        if ((sts & mask) == expect) {
            rc = 0;
            break;
        }
        if (timer_check(end)) {
            warn_timeout();
            break;
        }
        yield();
    }
    return rc;
}

static u32 tis_activate(u8 locty)
{
    if (!CONFIG_TCGBIOS)
        return 0;

    u32 rc = 0;
    u8 acc;
    int l;
    u32 timeout_a = tpm_drivers[TIS_DRIVER_IDX].timeouts[TIS_TIMEOUT_TYPE_A];

    if (!(readb(TIS_REG(locty, TIS_REG_ACCESS)) &
          TIS_ACCESS_ACTIVE_LOCALITY)) {
        /* release locality in use top-downwards */
        for (l = 4; l >= 0; l--)
            writeb(TIS_REG(l, TIS_REG_ACCESS),
                   TIS_ACCESS_ACTIVE_LOCALITY);
    }

    /* request access to locality */
    writeb(TIS_REG(locty, TIS_REG_ACCESS), TIS_ACCESS_REQUEST_USE);

    acc = readb(TIS_REG(locty, TIS_REG_ACCESS));
    if ((acc & TIS_ACCESS_ACTIVE_LOCALITY)) {
        writeb(TIS_REG(locty, TIS_REG_STS), TIS_STS_COMMAND_READY);
        rc = tis_wait_sts(locty, timeout_a,
                          TIS_STS_COMMAND_READY, TIS_STS_COMMAND_READY);
    }

    return rc;
}

static u32 tis_find_active_locality(void)
{
    if (!CONFIG_TCGBIOS)
        return 0;

    u8 locty;

    for (locty = 0; locty <= 4; locty++) {
        if ((readb(TIS_REG(locty, TIS_REG_ACCESS)) &
             TIS_ACCESS_ACTIVE_LOCALITY))
            return locty;
    }

    tis_activate(0);

    return 0;
}

static u32 tis_ready(void)
{
    if (!CONFIG_TCGBIOS)
        return 0;

    u32 rc = 0;
    u8 locty = tis_find_active_locality();
    u32 timeout_b = tpm_drivers[TIS_DRIVER_IDX].timeouts[TIS_TIMEOUT_TYPE_B];

    writeb(TIS_REG(locty, TIS_REG_STS), TIS_STS_COMMAND_READY);
    rc = tis_wait_sts(locty, timeout_b,
                      TIS_STS_COMMAND_READY, TIS_STS_COMMAND_READY);

    return rc;
}

static u32 tis_senddata(const u8 *const data, u32 len)
{
    if (!CONFIG_TCGBIOS)
        return 0;

    u32 rc = 0;
    u32 offset = 0;
    u32 end_loop = 0;
    u16 burst = 0;
    u8 locty = tis_find_active_locality();
    u32 timeout_d = tpm_drivers[TIS_DRIVER_IDX].timeouts[TIS_TIMEOUT_TYPE_D];
    u32 end = timer_calc_usec(timeout_d);

    do {
        while (burst == 0) {
               burst = readl(TIS_REG(locty, TIS_REG_STS)) >> 8;
            if (burst == 0) {
                if (timer_check(end)) {
                    warn_timeout();
                    break;
                }
                yield();
            }
        }

        if (burst == 0) {
            rc = TCG_RESPONSE_TIMEOUT;
            break;
        }

        while (1) {
            writeb(TIS_REG(locty, TIS_REG_DATA_FIFO), data[offset++]);
            burst--;

            if (burst == 0 || offset == len)
                break;
        }

        if (offset == len)
            end_loop = 1;
    } while (end_loop == 0);

    return rc;
}

static u32 tis_readresp(u8 *buffer, u32 *len)
{
    if (!CONFIG_TCGBIOS)
        return 0;

    u32 rc = 0;
    u32 offset = 0;
    u32 sts;
    u8 locty = tis_find_active_locality();

    while (offset < *len) {
        buffer[offset] = readb(TIS_REG(locty, TIS_REG_DATA_FIFO));
        offset++;
        sts = readb(TIS_REG(locty, TIS_REG_STS));
        /* data left ? */
        if ((sts & TIS_STS_DATA_AVAILABLE) == 0)
            break;
    }

    *len = offset;

    return rc;
}


static u32 tis_waitdatavalid(void)
{
    if (!CONFIG_TCGBIOS)
        return 0;

    u32 rc = 0;
    u8 locty = tis_find_active_locality();
    u32 timeout_c = tpm_drivers[TIS_DRIVER_IDX].timeouts[TIS_TIMEOUT_TYPE_C];

    if (tis_wait_sts(locty, timeout_c, TIS_STS_VALID, TIS_STS_VALID) != 0)
        rc = 1;

    return rc;
}

static u32 tis_waitrespready(enum tpmDurationType to_t)
{
    if (!CONFIG_TCGBIOS)
        return 0;

    u32 rc = 0;
    u8 locty = tis_find_active_locality();
    u32 timeout = tpm_drivers[TIS_DRIVER_IDX].durations[to_t];

    writeb(TIS_REG(locty ,TIS_REG_STS), TIS_STS_TPM_GO);

    if (tis_wait_sts(locty, timeout,
                     TIS_STS_DATA_AVAILABLE, TIS_STS_DATA_AVAILABLE) != 0)
        rc = 1;

    return rc;
}


struct tpm_driver tpm_drivers[TPM_NUM_DRIVERS] = {
    [TIS_DRIVER_IDX] =
        {
            .timeouts      = NULL,
            .durations     = NULL,
            .set_timeouts  = set_timeouts,
            .probe         = tis_probe,
            .init          = tis_init,
            .activate      = tis_activate,
            .ready         = tis_ready,
            .senddata      = tis_senddata,
            .readresp      = tis_readresp,
            .waitdatavalid = tis_waitdatavalid,
            .waitrespready = tis_waitrespready,
        },
};

static u8 TPMHW_driver_to_use = TPM_INVALID_DRIVER;

TPMVersion
tpmhw_probe(void)
{
    unsigned int i;
    for (i = 0; i < TPM_NUM_DRIVERS; i++) {
        struct tpm_driver *td = &tpm_drivers[i];
        if (td->probe() != 0) {
            td->init();
            TPMHW_driver_to_use = i;
            return tis_get_tpm_version();
        }
    }
    return TPM_VERSION_NONE;
}

int
tpmhw_is_present(void)
{
    return TPMHW_driver_to_use != TPM_INVALID_DRIVER;
}

int
tpmhw_transmit(u8 locty, struct tpm_req_header *req,
               void *respbuffer, u32 *respbufferlen,
               enum tpmDurationType to_t)
{
    if (TPMHW_driver_to_use == TPM_INVALID_DRIVER)
        return -1;

    struct tpm_driver *td = &tpm_drivers[TPMHW_driver_to_use];

    u32 irc = td->activate(locty);
    if (irc != 0) {
        /* tpm could not be activated */
        return -1;
    }

    irc = td->senddata((void*)req, be32_to_cpu(req->totlen));
    if (irc != 0)
        return -1;

    irc = td->waitdatavalid();
    if (irc != 0)
        return -1;

    irc = td->waitrespready(to_t);
    if (irc != 0)
        return -1;

    irc = td->readresp(respbuffer, respbufferlen);
    if (irc != 0)
        return -1;

    td->ready();

    return 0;
}

void
tpmhw_set_timeouts(u32 timeouts[4], u32 durations[3])
{
    struct tpm_driver *td = &tpm_drivers[TPMHW_driver_to_use];
    td->set_timeouts(timeouts, durations);
}
