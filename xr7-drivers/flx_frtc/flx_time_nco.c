/** @file
 */

/*

   Flexibilis Real-Time Clock Linux driver

   Copyright (C) 2009 Flexibilis Oy

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License version 2
   as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

/// Uncomment to enable debug messages
//#define DEBUG

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/time.h>

#include "flx_time_nco_types.h"

// FLX general defines
#include <flx_pci/flx_pci.h>
#include <flx_pci_config/flx_modules.h>

/* Local function prototypes */
#include "flx_time_nco.h"

/**
 * Uncomment to enable reading subnanoseconds
 * (usually unavailable and irrelevant to accuracy).
 * This has nothing to do with step size, whose subnanoseconds
 * are always 32 bits wide.
 */
//#define SUPPORT_FRTC_SUBNANOSECONDS

int nco_adj_freq(struct flx_time_comp_priv *nco,
                 struct flx_time_freq_adjust_data *freq_adj_data)
{
    int timeout;
    uint64_t nsec_subnsec;

    /* Set initial step size */
    nsec_subnsec =
        ((uint64_t) nco->step_nsec << 32) |
        (uint64_t) nco->step_subnsec;

    /* Adjust data is in ppb (1e-9) - use scale factor */
    nsec_subnsec +=
        (int64_t) freq_adj_data->adjust * nco->adjust_scale_factor;

    /* The nsec register width is limited */
    if ((uint32_t)(nsec_subnsec >> 32) & ~(uint32_t)NCO_STEP_NSEC_MASK) {
        printk(KERN_DEBUG
               "%s: NCO frequency adjustment to invalid value.\n",
               flx_time_NAME);
        return -EINVAL;
    }

    nco->lock(nco);

    /* Record step size nanoseconds part for time adjustments */
    nco->cur_step_nsec = (uint32_t) (nsec_subnsec >> 32);

    /* Write data to hw */
    flx_nco_write32(nco, NCO_STEP_NSEC_REG,
                    nco->cur_step_nsec & NCO_STEP_NSEC_MASK);
    flx_nco_write32(nco, NCO_STEP_SUBNSEC_REG,
                    (uint32_t) nsec_subnsec);

    /* Write command to hw */
    flx_nco_write32(nco, NCO_CMD_REG, NCO_CMD_ADJUST_STEP);

    timeout = 100;
    while ((flx_nco_read32(nco, NCO_CMD_REG) & NCO_CMD_ADJUST_STEP) != 0) {
        if ((timeout--) == 0) {
            nco->unlock(nco);
            printk(KERN_DEBUG "%s: NCO read timeout.\n", flx_time_NAME);
            return -EIO;
        }
        nco->relax(nco);
    }

    nco->unlock(nco);

    return 0;
}

int nco_adj_time(struct flx_time_comp_priv *nco,
                 struct flx_time_clock_adjust_data *clk_adj_data)
{
    int timeout;

    int64_t adj_sec;
    int32_t adj_nsec;

    // Check for nsec value overflow
    if (clk_adj_data->adjust_time.nsec >= 1u << 31) {
	return -EINVAL;
    }

    adj_sec = clk_adj_data->adjust_time.sec;
    adj_nsec = clk_adj_data->adjust_time.nsec;

    if (clk_adj_data->sign < 0) {
        adj_sec = -adj_sec;
        adj_nsec = -adj_nsec;
    }

    nco->lock(nco);

    /*
     * FRTC >= 1.5 does not automatically add step size to the adjustment,
     * so do it here. Subnanoseconds are fine.
     * Handle all possible adjustments correctly.
     * Value written to nanoseconds step size register must be normalized
     * between 0 <= x < 1e9.
     */
    adj_nsec += nco->cur_step_nsec;
    while (adj_nsec < 0) {
        adj_nsec += 1000000000;
        adj_sec--;
    }
    while (adj_nsec >= 1000000000) {
        adj_nsec -= 1000000000;
        adj_sec++;
    }

    flx_nco_write32(nco, NCO_ADJ_SEC_HI_REG,
                    (uint32_t) ((adj_sec >> 32) & NCO_ADJ_SEC_HI_MASK));
    flx_nco_write32(nco, NCO_ADJ_SEC_REG,
                    (uint32_t) adj_sec);
    flx_nco_write32(nco, NCO_ADJ_NSEC_REG,
                    (uint32_t) adj_nsec & NCO_ADJ_NSEC_MASK);

    /* Write command to hw */
    flx_nco_write32(nco, NCO_CMD_REG, NCO_CMD_ADJUST_CLOCK);

    timeout = 100;
    while ((flx_nco_read32(nco, NCO_CMD_REG) & NCO_CMD_ADJUST_CLOCK) != 0) {
        if ((timeout--) == 0) {
            nco->unlock(nco);
            printk(KERN_DEBUG "%s: NCO adjust timeout.\n", flx_time_NAME);
            return -EACCES;
        }
        nco->relax(nco);
    }

    nco->unlock(nco);

    return 0;
}

int read_nco_time(struct flx_time_comp_priv *nco,
                  struct flx_time_get_data *data)
{
    int timeout;
    struct timespec ts;

    nco->lock(nco);

    // Read command
    flx_nco_write32(nco, NCO_CMD_REG, NCO_CMD_READ);

    timeout = 100;
    while ((flx_nco_read32(nco, NCO_CMD_REG) & NCO_CMD_READ) != 0) {
        if ((timeout--) == 0) {
            nco->unlock(nco);
            printk(KERN_DEBUG "%s: NCO read timeout.\n", flx_time_NAME);
            return -EACCES;
        }
        nco->relax(nco);
    }

    data->counter =
        ((uint64_t) flx_nco_read32(nco, NCO_CCCNT_HI_REG) << 32) |
         (uint64_t) flx_nco_read32(nco, NCO_CCCNT_REG);
    data->counter &= NCO_CC_MASK;

    // NCO is the time source, so we provide the same data for both structs.
    data->timestamp.sec =
        ((uint64_t) flx_nco_read32(nco, NCO_SEC_HI_REG) << 32) |
         (uint64_t) flx_nco_read32(nco, NCO_SEC_REG);
    data->timestamp.sec &= NCO_SEC_MASK;

    data->timestamp.nsec = flx_nco_read32(nco, NCO_NSEC_REG);
    data->timestamp.nsec &= NCO_NSEC_MASK;

    /*
     * It rarely makes any sense to read subnanoseconds here,
     * but in case they are available and desirable in some super fast
     * super accurate system reading can be enabled by a #define.
     */
#ifdef SUPPORT_FRTC_SUBNANOSECONDS
    data->timestamp.subnsec = flx_nco_read32(nco, NCO_SUBNSEC_REG);
    data->timestamp.subnsec &= NCO_SUBNSEC_MASK;
#else
    data->timestamp.subnsec = 0;
#endif

    nco->unlock(nco);

    // Normalize time.
    while (data->timestamp.nsec >= 1000000000u) {
        data->timestamp.nsec -= 1000000000u;
        data->timestamp.sec++;
    }

    // Return host system time as source_time
    getnstimeofday(&ts);

    data->source_time.sec = ts.tv_sec;
    data->source_time.nsec = ts.tv_nsec;
    data->source_time.subnsec = 0;

    return 0;
}

/*
 * Other local functions
 */

int init_nco_registers(struct flx_time_comp_priv *cp)
{
    struct timespec ts;
    int ret, tmp;
    struct flx_time_get_data read_time;
    struct flx_time_clock_adjust_data clk_adj_data;

    cp->cur_step_nsec = cp->step_nsec;

    tmp = flx_nco_read32(cp, GENERAL_REG);

    flx_nco_write32(cp, NCO_STEP_SUBNSEC_REG, cp->step_subnsec);
    flx_nco_write32(cp, NCO_STEP_NSEC_REG, cp->step_nsec);

    /*
     * Initialise the nco with the time of the host system.
     * That is rather inaccurate, but anyway we avoid starting
     * from zero (which would be also very ugly).
     */
    getnstimeofday(&ts);

    printk(KERN_DEBUG "%s: NCO using current time in init (%lli)\n",
           flx_time_NAME, (long long int)ts.tv_sec);

    /*
     * If there is a value already,
     * try to clear it to zero and the do normal init.
     */
    read_nco_time(cp, &read_time);

    clk_adj_data.sign = -1;
    clk_adj_data.adjust_time = read_time.timestamp;
    nco_adj_time(cp, &clk_adj_data);

    clk_adj_data.sign = 1;
    clk_adj_data.adjust_time.sec = ts.tv_sec;
    clk_adj_data.adjust_time.nsec = ts.tv_nsec;
    clk_adj_data.adjust_time.subnsec = 0;
    nco_adj_time(cp, &clk_adj_data);

    /* Write the above step to hw */
    flx_nco_write32(cp, NCO_CMD_REG, NCO_CMD_ADJUST_STEP);

    /* Verify that the nco started ok */
    ret = read_nco_time(cp, &read_time);
    if (read_time.timestamp.sec < ts.tv_sec) {
        printk(KERN_DEBUG
               "%s: NCO init failed (seconds not running properly: %lld).\n",
               flx_time_NAME, read_time.timestamp.sec);
        return -EFAULT;
    }
    if ((read_time.timestamp.sec == ts.tv_sec)
        && (read_time.timestamp.nsec == 0)) {
        printk(KERN_DEBUG
               "%s: NCO init failed (nanoseconds not running properly)\n",
               flx_time_NAME);
        return -EFAULT;
    }

    printk(KERN_DEBUG "%s: NCO using step size %u ns %u subns"
           " adjust_scale factor %u\n",
           flx_time_NAME,
           cp->step_nsec, cp->step_subnsec, cp->adjust_scale_factor);

    return 0;
}
