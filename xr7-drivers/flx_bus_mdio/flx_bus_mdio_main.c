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

#define DRV_NAME                "flx_bus_mdio"
#define DRV_VERSION             "1.11.1"

// Uncomment to enable debug messages
//#define DEBUG

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/version.h>

#include <flx_bus/flx_bus.h>

#include "flx_bus_mdio_types.h"
#include "flx_bus_mdio.h"
#include "flx_bus_mdio_phy.h"

#ifdef DEBUG

// For in_atomic, which should not be used from drivers
#include <linux/hardirq.h>

// A debugging aid to detect invalid usage
#define FLX_BUS_MDIO_ATOMIC_CHECK(dev) ({ \
    bool __atomic_check_ret = false; \
    if (WARN_ON_ONCE(in_atomic())) \
        __atomic_check_ret = true; \
    __atomic_check_ret; \
})

#else

#define FLX_BUS_MDIO_ATOMIC_CHECK(dev) ({ 0; })

#endif

// Module description information
MODULE_DESCRIPTION("Indirect register access via MDIO driver");
MODULE_AUTHOR("Flexibilis Oy");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);

/// Driver privates
static struct flx_bus_mdio_drv_priv drv_priv;

/**
 * Get access to driver privates
 */
struct flx_bus_mdio_drv_priv *flx_bus_mdio_get_drv_priv(void)
{
    return &drv_priv;
}

/**
 * Bus reset callback function.
 */
static int flx_bus_mdio_reset(struct flx_bus *bus)
{
    struct flx_bus_mdio_dev_priv *dp =
        container_of(bus, struct flx_bus_mdio_dev_priv, flx_bus);

    dev_dbg(&dp->pdev->dev, "Reset bus (no-op)\n");
    return 0;
}

/**
 * 16-bit bus read access through MDIO slave.
 */
static int flx_bus_mdio_slave_read_reg(struct flx_bus_mdio_dev_priv *dp,
                                       uint32_t addr)
{
    int ret;
    uint16_t addr_high = addr >> 16;
    uint16_t addr_low = addr & FLX_BUS_MDIO_SLAVE_AA0_ADDR;

    if (FLX_BUS_MDIO_ATOMIC_CHECK(&dp->pdev->dev))
        return -EIO;

    if (addr_high != dp->last_addr_high) {
        ret = mdiobus_write(dp->mdio_bus, dp->mdio_addr,
                            FLX_BUS_MDIO_SLAVE_REG_AA1,
                            addr_high);
        if (ret < 0) {
            dev_warn(&dp->pdev->dev, "Write failed to slave 0x%x\n",
                     dp->mdio_addr);
            return ret;
        }

        dp->last_addr_high = addr_high;
    }

    ret = mdiobus_write(dp->mdio_bus, dp->mdio_addr,
                        FLX_BUS_MDIO_SLAVE_REG_AA0,
                        addr_low | FLX_BUS_MDIO_SLAVE_AA0_READ);
    if (ret < 0) {
        dev_warn(&dp->pdev->dev, "Write failed to slave 0x%x\n",
                 dp->mdio_addr);
        return ret;
    }

    ret = mdiobus_read(dp->mdio_bus, dp->mdio_addr,
                       FLX_BUS_MDIO_SLAVE_REG_AD);
    if (ret < 0) {
        dev_warn(&dp->pdev->dev, "Read failed from slave 0x%x\n",
                 dp->mdio_addr);
        return ret;
    }

    dev_dbg(&dp->pdev->dev, "Read from bus address 0x%x value 0x%x\n",
            addr, ret);

    return ret;
}

/**
 * 16-bit bus write access through MDIO slave.
 */
static int flx_bus_mdio_slave_write_reg(struct flx_bus_mdio_dev_priv *dp,
                                        uint32_t addr, uint16_t value)
{
    int ret;
    uint16_t addr_high = addr >> 16;
    uint16_t addr_low = addr & FLX_BUS_MDIO_SLAVE_AA0_ADDR;

    if (FLX_BUS_MDIO_ATOMIC_CHECK(&dp->pdev->dev))
        return -EIO;

    ret = mdiobus_write(dp->mdio_bus, dp->mdio_addr,
                        FLX_BUS_MDIO_SLAVE_REG_AD, value);
    if (ret < 0) {
        dev_warn(&dp->pdev->dev, "Write failed to slave 0x%x\n",
                 dp->mdio_addr);
        return ret;
    }

    if (addr_high != dp->last_addr_high) {
        ret = mdiobus_write(dp->mdio_bus, dp->mdio_addr,
                            FLX_BUS_MDIO_SLAVE_REG_AA1,
                            addr_high);
        if (ret < 0) {
            dev_warn(&dp->pdev->dev, "Write failed to slave 0x%x\n",
                     dp->mdio_addr);
            return ret;
        }

        dp->last_addr_high = addr_high;
    }

    dev_dbg(&dp->pdev->dev, "Write to bus address 0x%x value 0x%x\n",
            addr, value);

    ret = mdiobus_write(dp->mdio_bus, dp->mdio_addr,
                        FLX_BUS_MDIO_SLAVE_REG_AA0,
                        addr_low | FLX_BUS_MDIO_SLAVE_AA0_WRITE);
    if (ret < 0) {
        dev_warn(&dp->pdev->dev, "Write failed to slave 0x%x\n",
                 dp->mdio_addr);
        return ret;
    }

    return 0;
}

/**
 * 16-bit bus read access through MDIO bridge.
 * @param dp Device privates.
 * @param addr Bus address to read.
 * @return Data or negative error code.
 */
static int flx_bus_mdio_bridge_read_reg(struct flx_bus_mdio_dev_priv *dp,
                                        uint32_t addr)
{
    int ret = -ENXIO;
    uint16_t phy_addr = (addr >> 16) & (PHY_MAX_ADDR - 1);

    if (FLX_BUS_MDIO_ATOMIC_CHECK(&dp->pdev->dev))
        return -EIO;

    if ((1u << phy_addr) & dp->phy_addr_mask) {
        dev_warn(&dp->mdio_bus->dev,
                 "Cannot read from bus address 0x%x"
                 ": PHY address 0x%x not usable\n",
                 addr, phy_addr);
        return -EINVAL;
    }

    ret = mdiobus_write(dp->mdio_bus, phy_addr,
                        FLX_BUS_MDIO_BRIDGE_REG_AA,
                        FLX_BUS_MDIO_BRIDGE_AA_READ |
                        FLX_BUS_MDIO_BRIDGE_AA_ADDR(addr));
    if (ret < 0) {
        dev_warn(&dp->mdio_bus->dev,
                 "Cannot read from bus address 0x%x"
                 ": write to register AA failed\n",
                 addr);
        return ret;
    }
    ret = mdiobus_read(dp->mdio_bus, phy_addr,
                       FLX_BUS_MDIO_BRIDGE_REG_AD);
#ifdef DEBUG_MDIO
    dev_dbg(&dp->mdio_bus->dev,
            "Read from bus address 0x%x"
            " PHY address 0x%02x AA 0x%04x : 0x%04x\n",
            phy_addr,
            FLX_BUS_MDIO_BRIDGE_AA_READ |
            FLX_BUS_MDIO_BRIDGE_AA_ADDR,
            value);
#endif

    return ret;
}

/**
 * 16-bit bus write access through MDIO bridge.
 * @param dp Device privates.
 * @param addr Bus address to write to.
 * @param value Data to write.
 * @return Zero on success or negative error code.
 */
static int flx_bus_mdio_bridge_write_reg(struct flx_bus_mdio_dev_priv *dp,
                                         uint32_t addr, uint16_t value)
{
    int ret = -ENXIO;
    uint16_t phy_addr = (addr >> 16) & (PHY_MAX_ADDR - 1);

    if (FLX_BUS_MDIO_ATOMIC_CHECK(&dp->pdev->dev))
        return -EIO;

    if ((1u << phy_addr) & dp->phy_addr_mask) {
        dev_warn(&dp->mdio_bus->dev, "Cannot write to bus address 0x%x"
                 ": PHY address 0x%x not usable\n",
                 addr, phy_addr);
                 return -EINVAL;
    }

#ifdef DEBUG_MDIO
    dev_dbg(&dp->mdio_bus->dev,
            "Write to bus address 0x%x"
            " PHY address 0x%02x AA 0x%04x : 0x%04x\n",
            phy_addr,
            FLX_BUS_MDIO_BRIDGE_AA_WRITE |
            FLX_BUS_MDIO_BRIDGE_AA_ADDR(addr),
            value);
#endif
    ret = mdiobus_write(dp->mdio_bus, phy_addr,
                        FLX_BUS_MDIO_BRIDGE_REG_AD,
                        value);
    if (ret < 0) {
        dev_warn(&dp->mdio_bus->dev, "Cannot write to bus address 0x%x"
                 ": write to register AA failed\n",
                 addr);
        return ret;
    }

    ret = mdiobus_write(dp->mdio_bus, phy_addr,
                        FLX_BUS_MDIO_BRIDGE_REG_AA,
                        FLX_BUS_MDIO_BRIDGE_AA_WRITE |
                        FLX_BUS_MDIO_BRIDGE_AA_ADDR(addr));

    return ret;
}

/**
 * Bus 16-bit read operation.
 * @param bus Bus instance
 * @param addr Bus address to read from
 * @param value Place for read value
 * @return Data or negative error code
 */
int flx_bus_mdio_read16(struct flx_bus *bus,
                        uint32_t addr, uint16_t *value)
{
    struct flx_bus_mdio_dev_priv *dp;
    int ret;

    dp = container_of(bus, struct flx_bus_mdio_dev_priv, flx_bus);

    mutex_lock(&dp->lock);

    if (dp->mdio_addr < 0)
        ret = flx_bus_mdio_bridge_read_reg(dp, addr);
    else
        ret = flx_bus_mdio_slave_read_reg(dp, addr);

    mutex_unlock(&dp->lock);

    if (ret < 0)
        return ret;

    *value = (uint16_t)ret;

    return 0;
}

/**
 * Bus 16-bit write operation.
 * @param bus Bus instance
 * @param addr Bus address to write to
 * @param value Value to write
 * @return Zero on success or negative error code.
 */
int flx_bus_mdio_write16(struct flx_bus *bus,
                         uint32_t addr, uint16_t value)
{
    struct flx_bus_mdio_dev_priv *dp;
    int ret;

    dp = container_of(bus, struct flx_bus_mdio_dev_priv, flx_bus);

    mutex_lock(&dp->lock);

    if (dp->mdio_addr < 0)
        ret = flx_bus_mdio_bridge_write_reg(dp, addr, value);
    else
        ret = flx_bus_mdio_slave_write_reg(dp, addr, value);

    mutex_unlock(&dp->lock);

    return ret;
}

/**
 * Indirect register access via MDIO device initialization.
 * @param pdev Platform device of the MDIO slave (or MDIO bridge).
 */
static int flx_bus_mdio_device_init(struct platform_device *pdev)
{
    int ret = -ENXIO;
    struct flx_bus_mdio_drv_priv *drv = flx_bus_mdio_get_drv_priv();
    struct flx_bus_mdio_dev_priv *dp = NULL;
    struct device_node *bus_node = NULL;
    uint32_t value = 0;
    unsigned long int dev_num = 0;

    dev_dbg(&pdev->dev, "New device\n");

    dev_num = find_first_zero_bit(drv->used_devices, MAX_DEVICES);
    if (dev_num >= MAX_DEVICES) {
        dev_warn(&pdev->dev, "Too many devices\n");
        goto err_too_many;
    }

    dp = kmalloc(sizeof(*dp), GFP_KERNEL);
    if (!dp) {
        dev_err(&pdev->dev, "Failed to allocate privates\n");
        ret = -ENOMEM;
        goto err_alloc_priv;
    }

    *dp = (struct flx_bus_mdio_dev_priv){
        .flx_bus = {
            .owner = THIS_MODULE,
            .name = DRV_NAME,
            .num = dev_num,
            .reset = &flx_bus_mdio_reset,
            .read16 = &flx_bus_mdio_read16,
            .write16 = &flx_bus_mdio_write16,
        },
        .mdio_addr = -1,
	.pdev = pdev,
    };

#ifdef CONFIG_OF
    bus_node = of_parse_phandle(pdev->dev.of_node, "mdio-bus", 0);
    if (!bus_node) {
        dev_err(&pdev->dev, "Missing mdio-bus in device tree\n");
        goto err_get_bus;
    }

    dp->mdio_bus = of_mdio_find_bus(bus_node);
    if (!dp->mdio_bus) {
        dev_err(&pdev->dev, "Failed to find MDIO bus\n");
        ret = -EPROBE_DEFER;
        goto err_get_bus;
    }

    ret = of_property_read_u32(pdev->dev.of_node, "mdio-addr", &value);
    if (ret) {
        dev_info(&pdev->dev, "Using MDIO bridge accesses\n");
    }
    else if (value >= PHY_MAX_ADDR) {
        dev_err(&pdev->dev, "Invalid mdio-addr value %u\n", value);
        ret = -EINVAL;
        goto err_mdio_addr;
    }
    else {
        dp->mdio_addr = value;
        dev_info(&pdev->dev, "Using MDIO slave accesses via 0x%x\n",
                 dp->mdio_addr);
    }
#else
    // Currently device tree support is required.
    dev_err(&pdev->dev, "No MDIO bus\n");
    ret = -ENODEV;
    goto err_get_bus;
#endif

    mutex_init(&dp->lock);
    set_bit(dp->flx_bus.num, drv->used_devices);
    INIT_LIST_HEAD(&dp->list);
    list_add(&dp->list, &drv->devices);

    pdev->dev.platform_data = dp;

    // Setup PHY access for MDIO bridge.
    if (dp->mdio_addr < 0) {
        ret = flx_bus_mdio_phy_init(dp);
        if (ret)
            goto err_phy_access_init;
    }
    else {
        // Ensure high address bits are known.
        ret = mdiobus_read(dp->mdio_bus, dp->mdio_addr,
                           FLX_BUS_MDIO_SLAVE_REG_AA1);
        if (ret < 0) {
            dev_err(&pdev->dev, "Read failed from slave 0x%x\n",
                    dp->mdio_addr);
            goto err_mdio_slave_access;
        }
        dp->last_addr_high = ret;
    }

    ret = flx_bus_register(&dp->flx_bus, &pdev->dev);
    if (ret) {
        dev_warn(&pdev->dev, "flx_bus_register failed\n");
        goto err_register_bus;
    }

    return 0;

err_register_bus:
    if (dp->mdio_addr < 0)
        flx_bus_mdio_phy_cleanup(dp);

err_mdio_slave_access:
err_phy_access_init:
    list_del(&dp->list);
    clear_bit(dp->flx_bus.num, drv->used_devices);

err_mdio_addr:
    put_device(&dp->mdio_bus->dev);

err_get_bus:
    kfree(dp);

err_alloc_priv:
err_too_many:
    return ret;
}

/**
 * Indirect register access via MDIO device cleanup.
 * @param pdev Platform device of the MDIO slave (or MDIO bridge).
 */
static void flx_bus_mdio_cleanup_device(struct platform_device *pdev)
{
    struct flx_bus_mdio_drv_priv *drv = flx_bus_mdio_get_drv_priv();
    struct flx_bus_mdio_dev_priv *dp = pdev->dev.platform_data;

    dev_dbg(&pdev->dev, "Remove device\n");

    flx_bus_unregister(&dp->flx_bus);

    if (dp->mdio_addr < 0)
        flx_bus_mdio_phy_cleanup(dp);

    list_del(&dp->list);
    clear_bit(dp->flx_bus.num, drv->used_devices);

    put_device(&dp->mdio_bus->dev);

    dp->pdev = NULL;
    pdev->dev.platform_data = NULL;
    kfree(dp);
}

#ifdef CONFIG_OF
static const struct of_device_id flx_bus_mdio_match[] = {
    { .compatible = "flx,bus-mdio" },
    { },
};
#endif

struct platform_driver flx_bus_mdio_driver = {
    .driver = {
        .name = "flx-bus-mdio",
        .owner = THIS_MODULE,
#ifdef CONFIG_OF
        .of_match_table = flx_bus_mdio_match,
#endif
    },
    .probe = &flx_bus_mdio_device_init,
};

/**
 * Initialize driver.
 */
static int __init flx_bus_mdio_init(void)
{
    int ret = 0;
    struct flx_bus_mdio_drv_priv *drv = flx_bus_mdio_get_drv_priv();

    pr_info(DRV_NAME ": Init driver\n");

    INIT_LIST_HEAD(&drv->devices);

    ret = platform_driver_register(&flx_bus_mdio_driver);
    if (ret) {
        pr_err(DRV_NAME ": Failed to register platform driver\n");
        goto err_register_driver;
    }

    pr_debug(DRV_NAME ": Driver init ready\n");

    return 0;

err_register_driver:
    return ret;
}

/**
 * Cleanup driver.
 */
static void __exit flx_bus_mdio_cleanup(void)
{
    struct flx_bus_mdio_drv_priv *drv = flx_bus_mdio_get_drv_priv();
    struct flx_bus_mdio_dev_priv *dp = NULL;
    struct flx_bus_mdio_dev_priv *tmp = NULL;

    pr_info(DRV_NAME ": Cleanup driver\n");

    list_for_each_entry_safe(dp, tmp, &drv->devices, list) {
        flx_bus_mdio_cleanup_device(dp->pdev);
    }

    platform_driver_unregister(&flx_bus_mdio_driver);

    pr_debug(DRV_NAME ": Driver cleanup ready\n");
}

// Module init and exit function
module_init(flx_bus_mdio_init);
module_exit(flx_bus_mdio_cleanup);

