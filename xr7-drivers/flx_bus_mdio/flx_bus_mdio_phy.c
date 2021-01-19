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

// Uncomment to enable debug messages
//#define DEBUG

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/phy.h>
#include <linux/mii.h>
#include <linux/of_mdio.h>
#include <linux/platform_device.h>

#include "flx_bus_mdio_types.h"
#include "flx_bus_mdio_phy.h"
#include "flx_bus_mdio.h"

/**
 * MDIO bridge PHY MDIO bus reset callback function.
 */
static int flx_bus_mdio_phy_bus_reset(struct mii_bus *mdio_bus)
{
    //struct flx_bus_mdio_dev_priv *dp = mdio_bus->priv;

    dev_dbg(&mdio_bus->dev, "Reset PHY MDIO bus (no-op)\n");

    return 0;
}

/**
 * MDIO bridge PHY MDIO bus read callback function.
 */
static int flx_bus_mdio_phy_bus_read(struct mii_bus *mdio_bus,
                                     int phy_addr, int regnum)
{
    struct flx_bus_mdio_dev_priv *dp = mdio_bus->priv;
    int ret = -ENXIO;

    if (!((1u << phy_addr) & dp->phy_addr_mask)) {
        dev_dbg(&mdio_bus->dev, "Read from non-existent PHY address 0x%x\n",
                phy_addr);
        // Must not return an error.
        return 0xffff;
    }

    mutex_lock(&dp->lock);

    ret = mdiobus_read(mdio_bus, phy_addr, regnum);

    mutex_unlock(&dp->lock);

    dev_dbg(&mdio_bus->dev, "Read from PHY 0x%x reg 0x%x value 0x%04x\n",
            phy_addr, regnum, ret);

    return ret;
}

/**
 * MDIO bridge PHY MDIO bus write callback function.
 */
static int flx_bus_mdio_phy_bus_write(struct mii_bus *mdio_bus,
                                      int phy_addr, int regnum,
                                      uint16_t value)
{
    struct flx_bus_mdio_dev_priv *dp = mdio_bus->priv;
    int ret = -ENXIO;

    if (!((1u << phy_addr) & dp->phy_addr_mask)) {
        dev_dbg(&mdio_bus->dev, "Write to non-existent PHY address 0x%x\n",
                phy_addr);
        // Must not return an error.
        return 0;
    }

    dev_dbg(&mdio_bus->dev, "Write to PHY 0x%x reg 0x%x value 0x%04x\n",
            phy_addr, regnum, value);

    mutex_lock(&dp->lock);

    ret = mdiobus_write(mdio_bus, phy_addr, regnum, value);

    mutex_unlock(&dp->lock);

    return ret;
}

/**
 * Initialize MDIO bridge PHY access part.
 * Create MDIO bus for the PHYs.
 * @param dp Devicd privates.
 */
int flx_bus_mdio_phy_init(struct flx_bus_mdio_dev_priv *dp)
{
    int ret = 0;
    struct platform_device *pdev = dp->pdev;
    struct mii_bus *mdio_bus = NULL;
    struct device_node *phy_bus_node = NULL;

    dev_dbg(&pdev->dev, "Init PHY MDIO bus\n");

    mdio_bus = mdiobus_alloc();
    if (!mdio_bus) {
        dev_warn(&pdev->dev, "mdiobus_alloc failed\n");
        ret = -ENOMEM;
        goto err_mdiobus_alloc;
    }

    mdio_bus->name = "flx-bus-mdio";
    mdio_bus->reset = &flx_bus_mdio_phy_bus_reset;
    mdio_bus->read = &flx_bus_mdio_phy_bus_read;
    mdio_bus->write = &flx_bus_mdio_phy_bus_write;
    mdio_bus->priv = dp;

    snprintf(mdio_bus->id, MII_BUS_ID_SIZE, "%s-%x",
             mdio_bus->name, dp->dev_num);

#ifdef CONFIG_OF
    phy_bus_node = of_get_child_by_name(pdev->dev.of_node, "phys");
    if (phy_bus_node) {
        ret = of_mdiobus_register(mdio_bus, phy_bus_node);
        // Handle only listed PHYs as PHY accesses.
        if (ret == 0) {
            dp->phy_addr_mask = ~mdio_bus->phy_mask;
        }
    }
    else {
        mdiobus_free(mdio_bus);
        mdio_bus = NULL;
        ret = 0;
    }
#else
    // Disable entirely for now.
    mdio_bus->phy_mask = PHY_MAX_ADDR - 1;
    ret = mdiobus_register(mdio_bus);
#endif
    if (ret) {
        dev_err(&pdev->dev, "Failed to register MDIO bus %s\n",
                mdio_bus->id);
        goto err_mdiobus_register;
    }

    dp->phy_mdio_bus = mdio_bus;

    return 0;

err_mdiobus_register:
    mdiobus_free(mdio_bus);
    dp->phy_mdio_bus = NULL;

err_mdiobus_alloc:
    return ret;
}

/**
 * Cleanup MDIO bridge PHY access part.
 * Removes the PHY MDIO bus from system.
 * @param dp Devicd privates.
 */
void flx_bus_mdio_phy_cleanup(struct flx_bus_mdio_dev_priv *dp)
{
    struct platform_device *pdev = dp->pdev;

    dev_dbg(&pdev->dev, "Cleanup PHY MDIO bus\n");

    if (dp->phy_mdio_bus) {
        mdiobus_unregister(dp->phy_mdio_bus);
        dp->phy_mdio_bus->priv = NULL;

        mdiobus_free(dp->phy_mdio_bus);
        dp->phy_mdio_bus = NULL;
    }
}

