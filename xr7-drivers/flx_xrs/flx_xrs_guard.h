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

#ifndef FLX_XRS_GUARD_H
#define FLX_XRS_GUARD_H

#include "flx_xrs_types.h"

/**
 * XRS interrupt guard device private information structure.
 * Guard devices are used to protect CPU from choking on interrupt load
 * until HW has been initialized.
 */
struct flx_xrs_guard_dev_priv {
    struct list_head list;              ///< linked list
    struct platform_device *pdev;       ///< XRS platform device
    struct device *this_dev;            ///< pointer to device with class
    unsigned int dev_num;               ///< number of this device

    struct mutex lock;                  ///< synchronize readyness changes
    bool ready;                         ///< HW and drivers ready for operation

    int reset;                          ///< reset GPIO number or negative
    int power_ok;                       ///< power_ok GPIO number or negative
    int irq;                            ///< interrupt number or negative
};

/**
 * XRS driver private information structure.
 */
struct flx_xrs_guard_drv_priv {
    struct list_head devices;   ///< Our devices
    /// Used device numbers
    DECLARE_BITMAP(used_devices, FLX_XRS_MAX_DEVICES);
};

int __init flx_xrs_guard_init(void);
void flx_xrs_guard_cleanup(void);

#endif
