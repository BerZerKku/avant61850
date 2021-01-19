/** @file
 */

/*

   Indirect register access via I2C Linux driver

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

#define DRV_NAME                "flx_bus_i2c"
#define DRV_VERSION             "1.11.1"

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
#include <linux/i2c.h>
#include <linux/list.h>
#include <linux/version.h>
#include <linux/of_platform.h>

#include <flx_bus/flx_bus.h>

/// Uncomment to enable binding via I2C slave addresses.
//#define FLX_BUS_I2C_USE_IDTABLE

#define MAX_DEVICES 32

#ifdef DEBUG

// For in_atomic, which should not be used from drivers
#include <linux/hardirq.h>

// A debugging aid to detect invalid usage
#define FLX_BUS_I2C_ATOMIC_CHECK(dev) ({ \
    bool __atomic_check_ret = false; \
    if (WARN_ON_ONCE(in_atomic())) \
        __atomic_check_ret = true; \
    __atomic_check_ret; \
})

#else

#define FLX_BUS_I2C_ATOMIC_CHECK(dev) ({ 0; })

#endif

// Module description information
MODULE_DESCRIPTION("Indirect register access via I2C driver");
MODULE_AUTHOR("Flexibilis Oy");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);

// __devinit and friends disappeared in Linux 3.8.
#ifndef __devinit
#define __devinit
#define __devexit
#endif

/**
 * Driver context
 */
struct flx_bus_i2c_drv_priv {
    struct list_head devices;                   ///< Our devices
    DECLARE_BITMAP(used_devices, MAX_DEVICES);  ///< Used device numbers
};

/**
 * Device privates
 */
struct flx_bus_i2c_dev_priv {
    struct i2c_client *i2c_client;              ///< I2C client
    struct flx_bus flx_bus;                     ///< flx_bus context
    struct list_head list;                      ///< Linked list
};

/// Driver privates
static struct flx_bus_i2c_drv_priv drv_priv;

/**
 * Get access to driver privates
 */
static inline struct flx_bus_i2c_drv_priv *get_drv_priv(void)
{
    return &drv_priv;
}

/**
 * Bus reset callback.
 * @param bus Bus intance
 */
static int flx_bus_i2c_reset(struct flx_bus *bus)
{
    struct flx_bus_i2c_dev_priv *dp =
        container_of(bus, struct flx_bus_i2c_dev_priv, flx_bus);

    dev_dbg(&dp->i2c_client->dev, "Reset bus (no-op)\n");

    return 0;
}

/**
 * Bus 16-bit read operation.
 * @param bus Bus instance
 * @param addr Bus address to read from
 * @param value Place for read value
 */
static int flx_bus_i2c_read16(struct flx_bus *bus,
                              uint32_t addr, uint16_t *value)
{
    struct flx_bus_i2c_dev_priv *dp =
        container_of(bus, struct flx_bus_i2c_dev_priv, flx_bus);
    int ret = 0;
    uint8_t data[] = {
        // Bus address and read/write bit
        (addr >> 24) & 0xff,
        (addr >> 16) & 0xff,
        (addr >> 8) & 0xff,
        ((addr >> 0) & 0xfe) | 0x01,
        // Data
        0, 0,
    };
    struct i2c_msg msgs[] = {
        // Write bus address
        {
            .addr = dp->i2c_client->addr,
            .flags = dp->i2c_client->flags,
            .len = 4,
            .buf = &data[0],
        },
        // Read bus data
        {
            .addr = dp->i2c_client->addr,
            .flags = dp->i2c_client->flags | I2C_M_RD,
            .len = 2,
            .buf = &data[4],
        },
    };

    FLX_BUS_I2C_ATOMIC_CHECK();

    ret = i2c_transfer(dp->i2c_client->adapter, msgs, ARRAY_SIZE(msgs));
    if (ret != ARRAY_SIZE(msgs)) {
        dev_err(&dp->i2c_client->dev,
                "Failed to read from bus address 0x%x : %i\n",
                addr, ret);
        return -EIO;
    }

    *value = ((uint16_t)data[4] << 8) | data[5];

    dev_dbg(&dp->i2c_client->dev,
            "Read from bus address 0x%x value 0x%04x\n",
            addr, (unsigned int)*value);

    return 0;
}

/**
 * Bus 16-bit write operation
 * @param addr Bus address to write to
 * @param value Value to write
 */
static int flx_bus_i2c_write16(struct flx_bus *bus,
                               uint32_t addr, uint16_t value)
{
    struct flx_bus_i2c_dev_priv *dp =
        container_of(bus, struct flx_bus_i2c_dev_priv, flx_bus);
    int ret = 0;
    uint8_t data[] = {
        // Bus address and read/write bit
        (addr >> 24) & 0xff,
        (addr >> 16) & 0xff,
        (addr >> 8) & 0xff,
        (addr >> 0) & 0xfe,
        // Data
        (value >> 8) & 0xff,
        value & 0xff,
    };
    struct i2c_msg msgs[] = {
        // Write bus address and data
        {
            .addr = dp->i2c_client->addr,
            .flags = dp->i2c_client->flags,
            .len = sizeof(data),
            .buf = &data[0],
        },
    };

    FLX_BUS_I2C_ATOMIC_CHECK();

    dev_dbg(&dp->i2c_client->dev,
            "Write to bus address 0x%x value 0x%04x\n",
            addr, (unsigned int)value);

    ret = i2c_transfer(dp->i2c_client->adapter, msgs, ARRAY_SIZE(msgs));
    if (ret != ARRAY_SIZE(msgs)) {
        dev_err(&dp->i2c_client->dev,
                "Failed to write to bus address 0x%x : %i\n",
                addr, ret);
        return -EIO;
    }

    return 0;
}

/**
 * I2C slave probe function.
 * @param client I2C client.
 * @param id I2C client identification information.
 */
static int __devinit flx_bus_i2c_device_init(struct i2c_client *client,
                                             const struct i2c_device_id *id)
{
    struct flx_bus_i2c_drv_priv *drv = get_drv_priv();
    struct flx_bus_i2c_dev_priv *dp = NULL;
    unsigned long int dev_num = 0;
    int ret = -ENXIO;

    dev_info(&client->dev, "New I2C slave 0x%x flags 0x%x\n",
             client->addr, client->flags);

    dev_num = find_first_zero_bit(drv->used_devices, MAX_DEVICES);
    if (dev_num >= MAX_DEVICES) {
        dev_warn(&client->dev, "Too many devices\n");
        goto err_too_many;
    }

    dp = kmalloc(sizeof(*dp), GFP_KERNEL);
    if (!dp) {
        dev_err(&client->dev, "Failed to allocate privates\n");
        ret = -ENOMEM;
        goto err_alloc_priv;
    }

    *dp = (struct flx_bus_i2c_dev_priv) {
        .i2c_client = client,
        .flx_bus = {
            .owner = THIS_MODULE,
            .name = DRV_NAME,
            .num = dev_num,
            .reset = &flx_bus_i2c_reset,
            .read16 = &flx_bus_i2c_read16,
            .write16 = &flx_bus_i2c_write16,
        },
    };

    INIT_LIST_HEAD(&dp->list);
    list_add(&dp->list, &drv->devices);
    set_bit(dp->flx_bus.num, drv->used_devices);

    ret = flx_bus_register(&dp->flx_bus, &client->dev);
    if (ret)
        goto err_register_bus;

    return 0;

err_register_bus:
    clear_bit(dp->flx_bus.num, drv->used_devices);
    list_del(&dp->list);

    dp->i2c_client = NULL;
    kfree(dp);

err_alloc_priv:
err_too_many:
    return ret;
}

/**
 * I2C slave cleanup function.
 * @param client I2C client instance.
 */
static int __devexit flx_bus_i2c_device_cleanup(struct i2c_client *client)
{
    struct flx_bus_i2c_drv_priv *drv = get_drv_priv();
    struct flx_bus_i2c_dev_priv *dp = NULL;
    struct flx_bus_i2c_dev_priv *tmp = NULL;
    int ret = -EINVAL;

    list_for_each_entry(tmp, &drv->devices, list) {
        if (tmp->i2c_client == client)
            dp = tmp;
        break;
    }

    if (!dp) {
        dev_err(&client->dev, "Failed to find virtual MDIO bus"
                " for I2C client 0x%hx\n", client->addr);
        return -ENXIO;
    }

    dev_info(&client->dev, "Remove I2C slave 0x%x\n", client->addr);

    flx_bus_unregister(&dp->flx_bus);

    clear_bit(dp->flx_bus.num, drv->used_devices);
    list_del(&dp->list);
    dp->i2c_client = NULL;

    kfree(dp);

    return ret;
}

/**
 * I2C ID table of I2C slaves for indirect register access.
 */
static const struct i2c_device_id flx_bus_i2c_idtable[] = {
#ifdef FLX_BUS_I2C_USE_IDTABLE
    { "flx-bus-i2c", 0x24 },
    { "flx-bus-i2c", 0x34 },
    { "flx-bus-i2c", 0x64 },
    { "flx-bus-i2c", 0x74 },
#endif
    { },
};

MODULE_DEVICE_TABLE(i2c, flx_bus_i2c_idtable);

#ifdef CONFIG_OF
/**
 * Device tree match table.
 */
static const struct of_device_id flx_bus_i2c_match[] = {
    { .compatible = "flx,bus-i2c" },
    { },
};
#endif

/**
 * I2C slave driver definition.
 */
static struct i2c_driver flx_bus_i2c_driver = {
    .driver = {
        .name = "flx-bus-i2c",
        .owner = THIS_MODULE,
        .bus = &flx_bus_type,
#ifdef CONFIG_OF
        .of_match_table = flx_bus_i2c_match,
#endif
    },
    .id_table = flx_bus_i2c_idtable,
    .probe = &flx_bus_i2c_device_init,
    .remove = &flx_bus_i2c_device_cleanup,
};

/**
 * Initialize driver.
 */
static int __init flx_bus_i2c_init(void)
{
    int ret = 0;
    struct flx_bus_i2c_drv_priv *drv = get_drv_priv();

    pr_info(DRV_NAME ": Init driver\n");

    INIT_LIST_HEAD(&drv->devices);

    ret = i2c_add_driver(&flx_bus_i2c_driver);
    if (ret) {
        pr_warn(DRV_NAME ": Failed to register i2c driver\n");
        goto err_i2c_driver;
    }

    pr_debug(DRV_NAME ": Driver ready\n");

err_i2c_driver:
    return ret;
}

/**
 * Cleanup driver.
 */
static void __exit flx_bus_i2c_cleanup(void)
{
    pr_info(DRV_NAME ": Driver cleanup\n");

    i2c_del_driver(&flx_bus_i2c_driver);

    pr_debug(DRV_NAME ": Driver cleanup done\n");
}

// Module init and exit function
module_init(flx_bus_i2c_init);
module_exit(flx_bus_i2c_cleanup);
