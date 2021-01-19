/** @file
 */

/*

   XRS Linux driver

   Copyright (C) 2015 Flexibilis Oy

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

#ifndef FLX_XRS_TYPES_H
#define FLX_XRS_TYPES_H

#include <linux/if.h>
#include <linux/mutex.h>

#define DRV_NAME "flx_xrs"

/// Maximum number of XRS devices
#define FLX_XRS_MAX_DEVICES  32

/// Reset signal delay for HW in milliseconds
#define FLX_XRS_RESET_DELAY 100

// __devinit and friends disappeared in Linux 3.8.
#ifndef __devinit
#define __devinit
#define __devexit
#endif

struct flx_xrs_drv_priv;
struct flx_xrs_dev_priv;

/**
 * XRS operations for supporting different XRS register access methods.
 */
struct flx_xrs_ops {
    /// Function to read XRS register
    int (*read_reg) (struct flx_xrs_dev_priv *dp, int reg);
};

/**
 * Register access context.
 */
struct flx_xrs_reg_access {
#ifdef CONFIG_FLX_BUS
    struct flx_bus *flx_bus;            ///< indirect register access bus
#endif
    uint32_t addr;                      ///< indirect access bus address
};

/**
 * XRS device private information structure.
 */
struct flx_xrs_dev_priv {
    struct list_head list;              ///< linked list
    struct platform_device *pdev;       ///< XRS platform device
    struct device *this_dev;            ///< pointer to device with class
    unsigned int dev_num;               ///< number of this device

    struct flx_xrs_reg_access regs;     ///< register access
    struct flx_xrs_ops ops;             ///< register access operations

    struct mutex lock;                  ///< synchronize readyness changes
    bool ready;                         ///< HW and drivers ready for operation

    int reset;                          ///< reset GPIO number or negative
    int power_ok;                       ///< power_ok GPIO number or negative
    int irq;                            ///< interrupt number or negative
};

/**
 * XRS driver private information structure.
 */
struct flx_xrs_drv_priv {
    struct list_head devices;   ///< Our devices
    /// Used device numbers
    DECLARE_BITMAP(used_devices, FLX_XRS_MAX_DEVICES);
};

static inline int flx_xrs_read_reg(struct flx_xrs_dev_priv *dp, int reg)
{
    return dp->ops.read_reg(dp, reg);
}

const char *flx_xrs_type_str(int dev_id0);

#endif
