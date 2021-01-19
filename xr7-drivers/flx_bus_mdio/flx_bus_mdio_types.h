/** @file
 */

/*

   Indirect register access via MDIO Linux driver

   Copyright (C) 2014 Flexibilis Oy

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

#ifndef FLX_BUS_MDIO_TYPES_H
#define FLX_BUS_MDIO_TYPES_H

#include <linux/phy.h>
#include <linux/mii.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/list.h>

#include <flx_bus/flx_bus.h>

#define DRV_NAME "flx_bus_mdio"

// __devinit and friends disappeared in Linux 3.8.
#ifndef __devinit
#define __devinit
#define __devexit
#endif

#define MAX_DEVICES 32

/**
 * Device privates
 */
struct flx_bus_mdio_dev_priv {
    struct list_head list;      ///< Linked list
    struct platform_device *pdev;       ///< Associated platform device
    unsigned int dev_num;       ///< Device number
    struct mii_bus *mdio_bus;   ///< MDIO bus from CPU for device access
    int mdio_addr;              ///< MDIO slave address (MDIO slave),
                                ///< or -1 (MDIO bridge)
    uint16_t last_addr_high;    ///< Last AA1 value (MDIO slave)
    struct flx_bus flx_bus;     ///< flx_bus context
    struct mutex lock;          ///< Mutex for access synchronization

    // PHY access (MDIO bridge)
    uint32_t phy_addr_mask;     ///< Detected own PHY IDs on MMD port
    struct mii_bus *phy_mdio_bus;       ///< MDIO bus for PHY access
};

#define flx_bus_mdio_to_dp(h) \
    container_of((h), struct flx_bus_mdio_dev_priv, handle)

/**
 * Driver context
 */
struct flx_bus_mdio_drv_priv {
    struct list_head devices;   ///< Linked list of devices
    DECLARE_BITMAP(used_devices, MAX_DEVICES);  ///< Used device numbers
};

/**
 * Get access to driver privates.
 */
struct flx_bus_mdio_drv_priv *flx_bus_mdio_get_drv_priv(void);

#endif
